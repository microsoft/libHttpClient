// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// Retry, backoff, Retry-After and fail-fast coverage for HC_CALL (Source/HTTP/httpcall.cpp).
//
// Responses come from a scripted HCSetHttpCallPerformFunction provider keyed by URL. Time is driven
// through the internal HC_CALL clock / perform-delay test hooks: the delay hook records every delay
// the retry logic computes, advances a fake clock by that amount (as if the wait happened) and then
// schedules the next attempt with no real delay. Nothing in this file sleeps through real backoff.
//
// Unit-test builds compile httpcall.cpp with HC_UNITTEST_API, which zeroes the backoff jitter and
// sets MIN_HTTP_TIMEOUT_IN_MS to 0, so the expected delays below are exact.
//

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "utils.h"
#include "Platform/ExternalHttpProvider.h"
#include <httpClient/httpProvider.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

constexpr uint32_t c_waitTimeoutMs{ 30000 };

// Expected delays, mirroring the constants in httpcall.cpp.
constexpr uint32_t c_maxBackoffMs{ 60000 };               // MAX_DELAY_TIME_IN_SEC
constexpr uint32_t c_minDelayForInternalErrorMs{ 10000 }; // MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS

// Effectively "never" for a real XTaskQueue delayed callback; used when a test wants a retry to stay
// pending until it is canceled.
constexpr uint32_t c_parkedRealDelayMs{ 10 * 60 * 1000 };

class Signal
{
public:
    void Set()
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_set = true;
        m_cv.notify_all();
    }

    bool Wait(uint32_t timeoutMs)
    {
        std::unique_lock<std::mutex> lock{ m_mutex };
        return m_cv.wait_for(lock, std::chrono::milliseconds{ timeoutMs }, [this] { return m_set; });
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_set{ false };
};

using Headers = std::vector<std::pair<std::string, std::string>>;

// One scripted attempt. The last entry of a script repeats for any further attempts.
struct ScriptedResponse
{
    uint32_t statusCode{ 200 };
    Headers headers{};
    std::string body{};
    HRESULT networkErrorCode{ S_OK };
    uint32_t platformNetworkErrorCode{ 0 };
    std::string platformNetworkErrorMessage{};
    HRESULT asyncResult{ S_OK };   // Result the provider completes the perform XAsyncBlock with
    uint32_t elapsedMs{ 0 };       // Fake time the attempt takes before its response "arrives"
};

ScriptedResponse Status(uint32_t statusCode, Headers headers = {})
{
    ScriptedResponse response{};
    response.statusCode = statusCode;
    response.headers = std::move(headers);
    return response;
}

ScriptedResponse NetworkError(HRESULT networkErrorCode)
{
    ScriptedResponse response{};
    response.statusCode = 0;
    response.networkErrorCode = networkErrorCode;
    return response;
}

// Response state of the call as seen by the provider at the start of an attempt.
struct AttemptObservation
{
    uint32_t statusCode{ 0 };
    uint32_t numHeaders{ 0 };
    size_t bodySize{ 0 };
    HRESULT networkErrorCode{ S_OK };
    uint32_t platformNetworkErrorCode{ 0 };
    std::string platformNetworkErrorMessage{};
};

struct DelayRecord
{
    std::string url;
    HC_CALL::PerformDelayReason reason;
    uint32_t delayInMs;
};

std::string GetUrl(HCCallHandle call)
{
    char const* method{ nullptr };
    char const* url{ nullptr };
    if (FAILED(HCHttpCallRequestGetUrl(call, &method, &url)) || url == nullptr)
    {
        return std::string{};
    }
    return url;
}

// Owns everything a retry test touches in global state: the scripted perform callback, the fake
// clock and the perform-delay hook. Initialize() installs them and HCInitialize()s; the destructor
// always restores the defaults (even when a VERIFY throws), so tests stay isolated.
class RetryTestEnvironment
{
public:
    enum class DelayMode
    {
        Skip,        // Record the delay, advance the fake clock by it, then run the attempt immediately
        SkipNoClock, // Record the delay and run the attempt immediately without advancing the clock
    };

    RetryTestEnvironment() = default;
    RetryTestEnvironment(RetryTestEnvironment const&) = delete;
    RetryTestEnvironment& operator=(RetryTestEnvironment const&) = delete;

    ~RetryTestEnvironment()
    {
        // Unblock any attempt parked in the delay hook before tearing anything down.
        m_releaseParked.Set();

        if (m_initialized)
        {
            HCCleanup();
        }
        HC_CALL::SetPerformDelayTestHook(nullptr, nullptr);
        HC_CALL::SetClockTestHook(nullptr, nullptr);
        ExternalHttpProvider::Get().ClearCallback();
    }

    HRESULT Initialize()
    {
        m_clockBase = chrono_clock_t::now();
        HC_CALL::SetClockTestHook(&RetryTestEnvironment::Now, this);
        HC_CALL::SetPerformDelayTestHook(&RetryTestEnvironment::OnPerformDelay, this);

        HRESULT hr = HCSetHttpCallPerformFunction(&RetryTestEnvironment::Perform, this);
        if (SUCCEEDED(hr))
        {
            hr = HCInitialize(nullptr);
            m_initialized = SUCCEEDED(hr);
        }
        return hr;
    }

    void SetScript(std::string const& url, std::vector<ScriptedResponse> script)
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_endpoints[url].script = std::move(script);
    }

    uint32_t AttemptCount(std::string const& url)
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        auto it = m_endpoints.find(url);
        return it == m_endpoints.end() ? 0u : static_cast<uint32_t>(it->second.observations.size());
    }

    std::vector<AttemptObservation> Observations(std::string const& url)
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        auto it = m_endpoints.find(url);
        return it == m_endpoints.end() ? std::vector<AttemptObservation>{} : it->second.observations;
    }

    // Delays the retry logic computed for url with the given reason, in order.
    std::vector<uint32_t> Delays(std::string const& url, HC_CALL::PerformDelayReason reason)
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        std::vector<uint32_t> delays;
        for (auto const& record : m_delays)
        {
            if (record.url == url && record.reason == reason)
            {
                delays.push_back(record.delayInMs);
            }
        }
        return delays;
    }

    std::vector<uint32_t> RetryDelays(std::string const& url)
    {
        return Delays(url, HC_CALL::PerformDelayReason::Retry);
    }

    void AdvanceClock(uint32_t ms)
    {
        m_clockOffsetMs.fetch_add(ms);
    }

    uint64_t ClockOffsetMs() const
    {
        return m_clockOffsetMs.load();
    }

    void SetDelayMode(DelayMode mode)
    {
        m_delayMode.store(mode);
    }

    // The first retry of url blocks in the delay hook (after the retry decision and Retry-After cache
    // update, before the retry is scheduled) until ReleaseParkedRetry().
    void ParkFirstRetryUntilReleased(std::string const& url)
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_parkUrl = url;
        m_parkUntilReleased = true;
    }

    // The first retry of url is scheduled with a real delay long enough that it is still pending
    // when the test cancels it.
    void ParkFirstRetryWithLongRealDelay(std::string const& url)
    {
        std::lock_guard<std::mutex> lock{ m_mutex };
        m_parkUrl = url;
        m_parkUntilReleased = false;
    }

    bool WaitForParkedRetry()
    {
        return m_parked.Wait(c_waitTimeoutMs);
    }

    void ReleaseParkedRetry()
    {
        m_releaseParked.Set();
    }

private:
    struct Endpoint
    {
        std::vector<ScriptedResponse> script;
        std::vector<AttemptObservation> observations;
    };

    static chrono_clock_t::time_point CALLBACK Now(void* context)
    {
        auto pthis = static_cast<RetryTestEnvironment*>(context);
        return pthis->m_clockBase + std::chrono::milliseconds{ static_cast<int64_t>(pthis->m_clockOffsetMs.load()) };
    }

    static void CALLBACK OnPerformDelay(void* context, HC_CALL* call, HC_CALL::PerformDelayReason reason, uint32_t* delayInMs)
    {
        auto pthis = static_cast<RetryTestEnvironment*>(context);
        std::string url = GetUrl(call);

        bool park{ false };
        bool parkUntilReleased{ false };
        {
            std::lock_guard<std::mutex> lock{ pthis->m_mutex };
            pthis->m_delays.push_back(DelayRecord{ url, reason, *delayInMs });
            if (reason == HC_CALL::PerformDelayReason::Retry && !pthis->m_parkUrl.empty() && url == pthis->m_parkUrl)
            {
                park = true;
                parkUntilReleased = pthis->m_parkUntilReleased;
                pthis->m_parkUrl.clear(); // Only the first retry parks
            }
        }

        if (park && !parkUntilReleased)
        {
            *delayInMs = c_parkedRealDelayMs;
            pthis->m_parked.Set();
            return;
        }

        if (park)
        {
            pthis->m_parked.Set();
            pthis->m_releaseParked.Wait(c_waitTimeoutMs);
        }

        if (pthis->m_delayMode.load() == DelayMode::Skip)
        {
            pthis->AdvanceClock(*delayInMs);
        }
        *delayInMs = 0;
    }

    static void CALLBACK Perform(HCCallHandle call, XAsyncBlock* asyncBlock, void* context, HCPerformEnv /*env*/)
    {
        auto pthis = static_cast<RetryTestEnvironment*>(context);

        AttemptObservation observation{};
        HCHttpCallResponseGetStatusCode(call, &observation.statusCode);
        HCHttpCallResponseGetNumHeaders(call, &observation.numHeaders);
        HCHttpCallResponseGetResponseBodyBytesSize(call, &observation.bodySize);
        HCHttpCallResponseGetNetworkErrorCode(call, &observation.networkErrorCode, &observation.platformNetworkErrorCode);
        char const* message{ nullptr };
        HCHttpCallResponseGetPlatformNetworkErrorMessage(call, &message);
        observation.platformNetworkErrorMessage = message ? message : "";

        std::string url = GetUrl(call);
        ScriptedResponse response{};
        bool scripted{ false };
        {
            std::lock_guard<std::mutex> lock{ pthis->m_mutex };
            auto it = pthis->m_endpoints.find(url);
            if (it != pthis->m_endpoints.end() && !it->second.script.empty())
            {
                auto& endpoint = it->second;
                size_t index = std::min(endpoint.observations.size(), endpoint.script.size() - 1);
                response = endpoint.script[index];
                endpoint.observations.push_back(observation);
                scripted = true;
            }
        }

        if (!scripted)
        {
            XAsyncComplete(asyncBlock, E_UNEXPECTED, 0);
            return;
        }

        pthis->AdvanceClock(response.elapsedMs);

        HCHttpCallResponseSetStatusCode(call, response.statusCode);
        for (auto const& header : response.headers)
        {
            HCHttpCallResponseSetHeader(call, header.first.c_str(), header.second.c_str());
        }
        if (!response.body.empty())
        {
            HCHttpCallResponseSetResponseBodyBytes(call, reinterpret_cast<uint8_t const*>(response.body.data()), response.body.size());
        }
        if (response.networkErrorCode != S_OK)
        {
            HCHttpCallResponseSetNetworkErrorCode(call, response.networkErrorCode, response.platformNetworkErrorCode);
        }
        if (!response.platformNetworkErrorMessage.empty())
        {
            HCHttpCallResponseSetPlatformNetworkErrorMessage(call, response.platformNetworkErrorMessage.c_str());
        }

        XAsyncComplete(asyncBlock, response.asyncResult, 0);
    }

    std::mutex m_mutex;
    std::map<std::string, Endpoint> m_endpoints;
    std::vector<DelayRecord> m_delays;

    chrono_clock_t::time_point m_clockBase{};
    std::atomic<uint64_t> m_clockOffsetMs{ 0 };
    std::atomic<DelayMode> m_delayMode{ DelayMode::Skip };

    std::string m_parkUrl;
    bool m_parkUntilReleased{ false };
    Signal m_parked;
    Signal m_releaseParked;

    bool m_initialized{ false };
};

// Closes the call handle when the test leaves scope (including when a VERIFY throws).
class Call
{
public:
    Call() = default;
    Call(Call const&) = delete;
    Call& operator=(Call const&) = delete;
    ~Call()
    {
        if (m_handle)
        {
            HCHttpCallCloseHandle(m_handle);
        }
    }

    HRESULT Create(std::string const& url)
    {
        RETURN_IF_FAILED(HCHttpCallCreate(&m_handle));
        return HCHttpCallRequestSetUrl(m_handle, "GET", url.c_str());
    }

    HCCallHandle Get() const { return m_handle; }

    uint32_t StatusCode() const
    {
        uint32_t statusCode{ 0 };
        HCHttpCallResponseGetStatusCode(m_handle, &statusCode);
        return statusCode;
    }

    uint32_t PerformCount() const
    {
        uint32_t performCount{ 0 };
        HCHttpCallGetPerformCount(m_handle, &performCount);
        return performCount;
    }

    HRESULT NetworkErrorCode() const
    {
        HRESULT networkErrorCode{ S_OK };
        uint32_t platformNetworkErrorCode{ 0 };
        HCHttpCallResponseGetNetworkErrorCode(m_handle, &networkErrorCode, &platformNetworkErrorCode);
        return networkErrorCode;
    }

    std::string Header(char const* name) const
    {
        char const* value{ nullptr };
        HCHttpCallResponseGetHeader(m_handle, name, &value);
        return value ? value : "<missing>";
    }

private:
    HCCallHandle m_handle{ nullptr };
};

// A perform in flight on its own thread-pool queue. Waits are bounded so a hang fails the test
// instead of the run. If the perform never completes, its state is deliberately leaked because
// libHttpClient may still reference the XAsyncBlock.
class PerformOperation
{
public:
    PerformOperation() : m_state{ new State{} } {}
    PerformOperation(PerformOperation const&) = delete;
    PerformOperation& operator=(PerformOperation const&) = delete;

    ~PerformOperation()
    {
        if (!m_started || m_state->Completed())
        {
            if (m_state->queue)
            {
                XTaskQueueCloseHandle(m_state->queue);
            }
            delete m_state;
        }
    }

    HRESULT Start(HCCallHandle call)
    {
        RETURN_IF_FAILED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &m_state->queue));
        m_state->asyncBlock.queue = m_state->queue;
        m_state->asyncBlock.context = m_state;
        m_state->asyncBlock.callback = &State::OnCompleted;
        HRESULT hr = HCHttpCallPerformAsync(call, &m_state->asyncBlock);
        m_started = SUCCEEDED(hr);
        return hr;
    }

    // Returns the perform result, or E_PENDING if it did not complete within the timeout.
    HRESULT Wait(uint32_t timeoutMs = c_waitTimeoutMs)
    {
        if (!m_state->done.Wait(timeoutMs))
        {
            return E_PENDING;
        }
        return XAsyncGetStatus(&m_state->asyncBlock, false);
    }

    void Cancel()
    {
        XAsyncCancel(&m_state->asyncBlock);
    }

private:
    struct State
    {
        XAsyncBlock asyncBlock{};
        XTaskQueueHandle queue{ nullptr };
        Signal done;
        std::atomic<bool> completed{ false };

        bool Completed() const { return completed.load(); }

        static void CALLBACK OnCompleted(XAsyncBlock* async)
        {
            auto state = static_cast<State*>(async->context);
            state->completed.store(true);
            state->done.Set();
        }
    };

    State* m_state;
    bool m_started{ false };
};

HRESULT PerformAndWait(Call const& call)
{
    PerformOperation operation;
    RETURN_IF_FAILED(operation.Start(call.Get()));
    return operation.Wait();
}

std::string UrlFor(char const* name, uint32_t value = 0)
{
    return std::string{ "https://retrytests.example/" } + name + "/" + std::to_string(value);
}

void VerifyDelays(std::vector<uint32_t> const& expected, std::vector<uint32_t> const& actual)
{
    VERIFY_ARE_EQUAL(expected.size(), actual.size());
    for (size_t i = 0; i < expected.size() && i < actual.size(); ++i)
    {
        VERIFY_ARE_EQUAL(expected[i], actual[i]);
    }
}

} // namespace

DEFINE_TEST_CLASS(RetryTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(RetryTests);

    DEFINE_TEST_CASE(TestRetryOnRetryableStatusThenSuccess)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRetryOnRetryableStatusThenSuccess);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        for (uint32_t status : { 408u, 429u, 500u, 502u, 503u, 504u })
        {
            std::string url = UrlFor("retryable", status);
            env.SetScript(url, { Status(status), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(2u, call.PerformCount());
            VERIFY_ARE_EQUAL(2u, env.AttemptCount(url));

            // First retry waits retryDelay^1 = 2s; a 500 waits at least 10s.
            uint32_t expectedDelay = status == 500 ? c_minDelayForInternalErrorMs : 2000u;
            VerifyDelays({ expectedDelay }, env.RetryDelays(url));
        }
    }

    DEFINE_TEST_CASE(TestNoRetryOnNonRetryableStatus)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNoRetryOnNonRetryableStatus);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        for (uint32_t status : { 200u, 201u, 204u, 301u, 304u, 400u, 401u, 403u, 404u, 409u, 501u, 505u })
        {
            std::string url = UrlFor("nonretryable", status);
            env.SetScript(url, { Status(status), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(status, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
            VERIFY_ARE_EQUAL(1u, env.AttemptCount(url));
            VERIFY_ARE_EQUAL(size_t{ 0 }, env.RetryDelays(url).size());
        }
    }

    DEFINE_TEST_CASE(TestRetryAllowedFalseDisablesRetry)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRetryAllowedFalseDisablesRetry);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        // Per-call opt out.
        {
            std::string url = UrlFor("retryallowed-percall");
            env.SetScript(url, { Status(503), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(call.Get(), false));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(503u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
            VERIFY_ARE_EQUAL(1u, env.AttemptCount(url));
        }

        // Network errors are not retried either when retries are disabled.
        {
            std::string url = UrlFor("retryallowed-networkerror");
            env.SetScript(url, { NetworkError(E_FAIL), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(call.Get(), false));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(E_FAIL, call.NetworkErrorCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }

        // Global default (copied into calls at creation) disables retry; a per-call override wins.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(nullptr, false));
        {
            std::string url = UrlFor("retryallowed-global");
            env.SetScript(url, { Status(503), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(503u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }
        {
            std::string url = UrlFor("retryallowed-global-override");
            env.SetScript(url, { Status(503), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(call.Get(), true));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(2u, call.PerformCount());
        }
        // The global default is per-initialization state; HCCleanup in ~RetryTestEnvironment resets it.
    }

    DEFINE_TEST_CASE(TestBackoffGrowsAndIsCapped)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestBackoffGrowsAndIsCapped);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        // Default retry delay 2s: delay before retry N is 2^N seconds, capped at 60s.
        {
            std::string url = UrlFor("backoff-default");
            env.SetScript(url, { Status(503), Status(503), Status(503), Status(503), Status(503), Status(503), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(call.Get(), 1000));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(7u, call.PerformCount());
            VerifyDelays({ 2000, 4000, 8000, 16000, 32000, c_maxBackoffMs }, env.RetryDelays(url));
        }

        // Custom retry delay 10s: 10s, then 100s capped to 60s.
        {
            std::string url = UrlFor("backoff-custom");
            env.SetScript(url, { Status(503), Status(503), Status(503), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(call.Get(), 1000));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(call.Get(), 10));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(4u, call.PerformCount());
            VerifyDelays({ 10000, c_maxBackoffMs, c_maxBackoffMs }, env.RetryDelays(url));
        }

        // The global retry delay default is copied into new calls.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(nullptr, 3));
        {
            std::string url = UrlFor("backoff-global");
            env.SetScript(url, { Status(503), Status(503), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(call.Get(), 1000));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(3u, call.PerformCount());
            VerifyDelays({ 3000, 9000 }, env.RetryDelays(url));
        }
    }

    DEFINE_TEST_CASE(TestRetryAfterHeaderCombinedWithBackoff)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRetryAfterHeaderCombinedWithBackoff);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        struct Case
        {
            uint32_t status;
            char const* retryAfter;
            uint32_t expectedDelayMs;
        };

        // The delay is max(backoff, Retry-After), and at least 10s for a 500. The 60s cap applies
        // to the exponential backoff only; a longer Retry-After is honored as-is (the timeout window
        // still bounds it). Only the delta-seconds form is parsed; an HTTP-date is ignored.
        Case const cases[]
        {
            { 429, "5", 5000 },                                // Retry-After longer than backoff (2s)
            { 503, "1", 2000 },                                // Backoff longer than Retry-After
            { 503, "90", 90000 },                              // Retry-After beyond the 60s backoff cap
            { 500, "3", c_minDelayForInternalErrorMs },        // 500 floor beats both
            { 500, "15", 15000 },                              // Retry-After beats the 500 floor
            { 503, "Wed, 21 Oct 2015 07:28:00 GMT", 2000 },    // HTTP-date form not supported
        };

        uint32_t index{ 0 };
        for (auto const& c : cases)
        {
            std::string url = UrlFor("retryafter", index++);
            env.SetScript(url, { Status(c.status, { { "Retry-After", c.retryAfter } }), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(call.Get(), 1000));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(2u, call.PerformCount());
            VerifyDelays({ c.expectedDelayMs }, env.RetryDelays(url));
        }
    }

    DEFINE_TEST_CASE(TestTimeoutWindowStopsRetries)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestTimeoutWindowStopsRetries);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        // Default 20s window: retries after 2s, 4s and 8s (14s elapsed). The next backoff (16s) does
        // not fit in the remaining 6s, so the call completes with the last failure.
        {
            std::string url = UrlFor("window-default");
            env.SetScript(url, { Status(503) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(503u, call.StatusCode());
            VERIFY_ARE_EQUAL(4u, call.PerformCount());
            VERIFY_ARE_EQUAL(4u, env.AttemptCount(url));
            VerifyDelays({ 2000, 4000, 8000 }, env.RetryDelays(url));
        }

        // Time spent waiting for the response counts against the window: after a 19s attempt only
        // 1s remains, which is less than the 2s backoff.
        {
            std::string url = UrlFor("window-slow-response");
            ScriptedResponse slow = Status(503);
            slow.elapsedMs = 19000;
            env.SetScript(url, { slow, Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(503u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }

        // A window of 0 disables retries.
        {
            std::string url = UrlFor("window-zero");
            env.SetScript(url, { Status(503), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(call.Get(), 0));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(503u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }

        // A Retry-After longer than the remaining window stops retries.
        {
            std::string url = UrlFor("window-retryafter");
            env.SetScript(url, { Status(429, { { "Retry-After", "30" } }), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(429u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }

        // The global window default is copied into new calls.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(nullptr, 5));
        {
            std::string url = UrlFor("window-global");
            env.SetScript(url, { Status(503) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            // 5s window: retry after 2s; then 3s remain, less than the 4s backoff.
            VERIFY_ARE_EQUAL(2u, call.PerformCount());
            VerifyDelays({ 2000 }, env.RetryDelays(url));
        }
    }

    DEFINE_TEST_CASE(TestNetworkErrorRetry)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNetworkErrorRetry);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        // A network error (no HTTP status) is retried with the normal backoff.
        {
            std::string url = UrlFor("network-transient");
            env.SetScript(url, { NetworkError(E_FAIL), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(S_OK, call.NetworkErrorCode());
            VERIFY_ARE_EQUAL(2u, call.PerformCount());
            VerifyDelays({ 2000 }, env.RetryDelays(url));
        }

        // A persistent network error is retried until the timeout window and then reported through
        // the network error code; the perform itself still succeeds.
        {
            std::string url = UrlFor("network-persistent");
            env.SetScript(url, { NetworkError(E_FAIL) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(E_FAIL, call.NetworkErrorCode());
            VERIFY_ARE_EQUAL(4u, call.PerformCount());
        }

        // E_HC_NO_NETWORK is never retried.
        {
            std::string url = UrlFor("network-none");
            env.SetScript(url, { NetworkError(E_HC_NO_NETWORK), Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(E_HC_NO_NETWORK, call.NetworkErrorCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
            VERIFY_ARE_EQUAL(1u, env.AttemptCount(url));
        }

        // A provider that fails the perform operation itself is not retried; the failure is the
        // perform result.
        {
            std::string url = UrlFor("network-asyncfailure");
            ScriptedResponse failed = Status(503);
            failed.asyncResult = E_OUTOFMEMORY;
            env.SetScript(url, { failed, Status(200) });

            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, PerformAndWait(call));

            VERIFY_ARE_EQUAL(1u, call.PerformCount());
            VERIFY_ARE_EQUAL(1u, env.AttemptCount(url));
        }
    }

    DEFINE_TEST_CASE(TestResponsePropertiesResetBetweenAttempts)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponsePropertiesResetBetweenAttempts);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        std::string url = UrlFor("reset");

        ScriptedResponse first = Status(503, { { "X-Attempt", "1" }, { "X-First-Only", "yes" } });
        first.body = "first attempt body";

        ScriptedResponse second = NetworkError(E_FAIL);
        second.platformNetworkErrorCode = 1234;
        second.platformNetworkErrorMessage = "connection reset";
        second.headers = { { "X-Attempt", "2" } };

        ScriptedResponse third = Status(200, { { "X-Attempt", "3" } });
        third.body = "final";

        env.SetScript(url, { first, second, third });

        Call call;
        VERIFY_SUCCEEDED(call.Create(url));
        VERIFY_SUCCEEDED(PerformAndWait(call));
        VERIFY_ARE_EQUAL(3u, call.PerformCount());

        // Every attempt starts from a clean response.
        auto observations = env.Observations(url);
        VERIFY_ARE_EQUAL(size_t{ 3 }, observations.size());
        for (auto const& observation : observations)
        {
            VERIFY_ARE_EQUAL(0u, observation.statusCode);
            VERIFY_ARE_EQUAL(0u, observation.numHeaders);
            VERIFY_ARE_EQUAL(size_t{ 0 }, observation.bodySize);
            VERIFY_ARE_EQUAL(S_OK, observation.networkErrorCode);
            VERIFY_ARE_EQUAL(0u, observation.platformNetworkErrorCode);
            VERIFY_ARE_EQUAL_STR("", observation.platformNetworkErrorMessage.c_str());
        }

        // The completed call reports only the final attempt.
        VERIFY_ARE_EQUAL(200u, call.StatusCode());
        VERIFY_ARE_EQUAL(S_OK, call.NetworkErrorCode());
        VERIFY_ARE_EQUAL_STR("3", call.Header("X-Attempt").c_str());
        VERIFY_ARE_EQUAL_STR("<missing>", call.Header("X-First-Only").c_str());

        uint32_t numHeaders{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetNumHeaders(call.Get(), &numHeaders));
        VERIFY_ARE_EQUAL(1u, numHeaders);

        char const* body{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(call.Get(), &body));
        VERIFY_ARE_EQUAL_STR("final", body);

        HRESULT networkErrorCode{ E_FAIL };
        uint32_t platformNetworkErrorCode{ 1 };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetNetworkErrorCode(call.Get(), &networkErrorCode, &platformNetworkErrorCode));
        VERIFY_ARE_EQUAL(0u, platformNetworkErrorCode);

        char const* message{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallResponseGetPlatformNetworkErrorMessage(call.Get(), &message));
        VERIFY_ARE_EQUAL_STR("", message);
    }

    DEFINE_TEST_CASE(TestRetryAfterCacheFailsFastWhileRetryPending)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRetryAfterCacheFailsFastWhileRetryPending);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        constexpr uint32_t cacheId{ 5001 };
        std::string const firstUrl = UrlFor("cache-pending-first");
        std::string const sameIdUrl = UrlFor("cache-pending-sameid");
        std::string const otherIdUrl = UrlFor("cache-pending-otherid");
        std::string const noIdUrl = UrlFor("cache-pending-noid");
        std::string const afterWindowUrl = UrlFor("cache-pending-afterwindow");

        env.SetScript(firstUrl, { Status(429, { { "Retry-After", "5" } }), Status(200) });
        env.SetScript(sameIdUrl, { Status(200) });
        env.SetScript(otherIdUrl, { Status(200) });
        env.SetScript(noIdUrl, { Status(200) });
        env.SetScript(afterWindowUrl, { Status(200) });

        // The first call gets 429 + Retry-After: 5. That caches {T+5s, 429, call pending} for the id
        // and schedules a retry, which is held in the delay hook so the window stays open.
        env.ParkFirstRetryUntilReleased(firstUrl);
        Call first;
        VERIFY_SUCCEEDED(first.Create(firstUrl));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(first.Get(), cacheId));
        PerformOperation firstOperation;
        VERIFY_SUCCEEDED(firstOperation.Start(first.Get()));
        VERIFY_IS_TRUE(env.WaitForParkedRetry());

        // Another call to the same endpoint inside the window fails fast with the cached status,
        // without reaching the provider, because a call to that endpoint is already pending.
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(sameIdUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(429u, call.StatusCode());
            VERIFY_ARE_EQUAL(0u, call.PerformCount());
            VERIFY_ARE_EQUAL(0u, env.AttemptCount(sameIdUrl));
        }

        // A different cache id, or no cache id, is unaffected.
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(otherIdUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId + 1));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(noIdUrl));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }

        // Let the first call's retry run (the hook advances the fake clock by the 5s delay).
        env.ReleaseParkedRetry();
        VERIFY_SUCCEEDED(firstOperation.Wait());
        VERIFY_ARE_EQUAL(200u, first.StatusCode());
        VERIFY_ARE_EQUAL(2u, first.PerformCount());
        VerifyDelays({ 5000 }, env.RetryDelays(firstUrl));

        // Once the Retry-After window has elapsed, calls with the id go straight through again.
        VERIFY_ARE_EQUAL(uint64_t{ 5000 }, env.ClockOffsetMs());
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(afterWindowUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
            VerifyDelays({ 0 }, env.Delays(afterWindowUrl, HC_CALL::PerformDelayReason::FirstAttempt));
        }
    }

    DEFINE_TEST_CASE(TestRetryAfterCacheDelaysOrFailsFastWithoutPendingCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRetryAfterCacheDelaysOrFailsFastWithoutPendingCall);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        constexpr uint32_t cacheId{ 5002 };
        std::string const firstUrl = UrlFor("cache-idle-first");
        std::string const shortWindowUrl = UrlFor("cache-idle-shortwindow");
        std::string const delayedUrl = UrlFor("cache-idle-delayed");
        std::string const secondDelayedUrl = UrlFor("cache-idle-seconddelayed");
        std::string const otherIdUrl = UrlFor("cache-idle-otherid");
        std::string const afterWindowUrl = UrlFor("cache-idle-afterwindow");

        env.SetScript(firstUrl, { Status(429, { { "Retry-After", "30" } }), Status(200) });
        for (auto const& url : { shortWindowUrl, delayedUrl, secondDelayedUrl, otherIdUrl, afterWindowUrl })
        {
            env.SetScript(url, { Status(200) });
        }

        // Retry-After: 30 exceeds the default 20s window, so the first call gives up after one
        // attempt. The cache records {T+30s, 429, no call pending}.
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(firstUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(429u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }

        // A call whose timeout window ends before the Retry-After window fails fast with the cached
        // status.
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(shortWindowUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(429u, call.StatusCode());
            VERIFY_ARE_EQUAL(0u, call.PerformCount());
            VERIFY_ARE_EQUAL(0u, env.AttemptCount(shortWindowUrl));
        }

        // A call whose timeout window outlasts the Retry-After window is not failed: its first attempt
        // is delayed until the window ends, and it becomes the endpoint's pending call.
        env.SetDelayMode(RetryTestEnvironment::DelayMode::SkipNoClock);
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(delayedUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(call.Get(), 60));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
            VerifyDelays({ 30000 }, env.Delays(delayedUrl, HC_CALL::PerformDelayReason::FirstAttempt));
        }

        // With that call pending (the clock was not advanced), the next call fails fast even though
        // its own window would also outlast the Retry-After.
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(secondDelayedUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(call.Get(), 60));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(429u, call.StatusCode());
            VERIFY_ARE_EQUAL(0u, call.PerformCount());
        }

        // Other cache ids are unaffected.
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(otherIdUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId + 1));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }

        // After the Retry-After window the cached state is dropped and calls proceed without delay.
        env.AdvanceClock(30000);
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(afterWindowUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
            VerifyDelays({ 0 }, env.Delays(afterWindowUrl, HC_CALL::PerformDelayReason::FirstAttempt));
        }
    }

    DEFINE_TEST_CASE(TestRetryAfterNotCachedWhenRetryDisabled)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRetryAfterNotCachedWhenRetryDisabled);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        constexpr uint32_t cacheId{ 5003 };
        std::string const firstUrl = UrlFor("cache-noretry-first");
        std::string const secondUrl = UrlFor("cache-noretry-second");
        env.SetScript(firstUrl, { Status(429, { { "Retry-After", "30" } }) });
        env.SetScript(secondUrl, { Status(200) });

        // The Retry-After cache is maintained by the retry logic, so a call with retries disabled
        // does not populate it and later calls with the same id are not failed fast.
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(firstUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(call.Get(), false));
            VERIFY_SUCCEEDED(PerformAndWait(call));
            VERIFY_ARE_EQUAL(429u, call.StatusCode());
        }
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(secondUrl));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryCacheId(call.Get(), cacheId));
            VERIFY_SUCCEEDED(PerformAndWait(call));

            VERIFY_ARE_EQUAL(200u, call.StatusCode());
            VERIFY_ARE_EQUAL(1u, call.PerformCount());
        }
    }

    DEFINE_TEST_CASE(TestCancelWhileRetryPending)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCancelWhileRetryPending);

        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        std::string const url = UrlFor("cancel-pending");
        env.SetScript(url, { Status(503), Status(200) });
        env.ParkFirstRetryWithLongRealDelay(url);

        Call call;
        VERIFY_SUCCEEDED(call.Create(url));
        PerformOperation operation;
        VERIFY_SUCCEEDED(operation.Start(call.Get()));
        VERIFY_IS_TRUE(env.WaitForParkedRetry());

        // Canceling while the retry is waiting completes the perform promptly with E_ABORT; the
        // retry never reaches the provider.
        operation.Cancel();
        VERIFY_ARE_EQUAL(E_ABORT, operation.Wait());
        VERIFY_ARE_EQUAL(1u, env.AttemptCount(url));
        VERIFY_ARE_EQUAL(1u, call.PerformCount());
    }

    DEFINE_TEST_CASE(TestCleanupWhileRetryPending)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCleanupWhileRetryPending);

        // Deterministic variant of GlobalTests::TestAsyncCleanupWithHttpCallPendingRetry: the retry
        // is known to be scheduled (not just probably, after a sleep) when cleanup starts.
        RetryTestEnvironment env;
        VERIFY_SUCCEEDED(env.Initialize());

        std::string const url = UrlFor("cleanup-pending");
        env.SetScript(url, { Status(503), Status(200) });
        env.ParkFirstRetryWithLongRealDelay(url);

        PerformOperation operation;
        {
            Call call;
            VERIFY_SUCCEEDED(call.Create(url));
            VERIFY_SUCCEEDED(operation.Start(call.Get()));
        } // The client handle may be closed while the perform is outstanding.
        VERIFY_IS_TRUE(env.WaitForParkedRetry());

        XTaskQueueHandle cleanupQueue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &cleanupQueue));
        XAsyncBlock cleanupAsync{};
        cleanupAsync.queue = cleanupQueue;
        HRESULT cleanupHr = HCCleanupAsync(&cleanupAsync);
        if (SUCCEEDED(cleanupHr))
        {
            cleanupHr = XAsyncGetStatus(&cleanupAsync, true);
        }
        XTaskQueueCloseHandle(cleanupQueue);

        VERIFY_SUCCEEDED(cleanupHr);
        VERIFY_ARE_EQUAL(E_ABORT, operation.Wait());
        VERIFY_ARE_EQUAL(1u, env.AttemptCount(url));
        VERIFY_IS_FALSE(HCIsInitialized());
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
