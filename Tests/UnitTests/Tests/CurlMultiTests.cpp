// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"libHttpClient"
#include "DefineTestMacros.h"
#include "HTTP/Curl/CurlMulti.h"

using namespace xbox::httpclient;

namespace
{
struct RequestState
{
    bool stalled{ false };
    unsigned completions{ 0 };
    HRESULT result{ E_PENDING };
    XAsyncBlock async{};
};

struct FakeEasy
{
    RequestState* state;
    bool done{ false };
    bool reported{ false };
};

struct FakeMulti
{
    http_internal_vector<FakeEasy*> requests;
    unsigned performs{ 0 };
    bool failPerform{ false };
    std::function<void()> beforePerform;
    CURLMsg message{};
};

FakeMulti* g_lastMulti{};
}

// The Generic-platform tests link this fake curl backend, not libcurl. Only the
// scheduler under test (CurlMulti.cpp) is real; transfers complete deterministically.
extern "C"
{
CURLM* curl_multi_init()
{
    g_lastMulti = Make<FakeMulti>();
    return g_lastMulti;
}

CURLMcode curl_multi_cleanup(CURLM* multi)
{
    Delete(static_cast<FakeMulti*>(multi));
    return CURLM_OK;
}

CURLMcode curl_multi_add_handle(CURLM* multi, CURL* easy)
{
    static_cast<FakeMulti*>(multi)->requests.push_back(static_cast<FakeEasy*>(easy));
    return CURLM_OK;
}

CURLMcode curl_multi_remove_handle(CURLM* multi, CURL* easy)
{
    auto& requests = static_cast<FakeMulti*>(multi)->requests;
    requests.erase(std::find(requests.begin(), requests.end(), static_cast<FakeEasy*>(easy)));
    return CURLM_OK;
}

CURLMcode curl_multi_perform(CURLM* multi, int* running)
{
    auto fake = static_cast<FakeMulti*>(multi);
    ++fake->performs;
    if (fake->beforePerform)
    {
        fake->beforePerform();
    }
    if (fake->failPerform)
    {
        fake->failPerform = false;
        return CURLM_INTERNAL_ERROR;
    }

    *running = 0;
    for (auto easy : fake->requests)
    {
        easy->done = !easy->state->stalled;
        if (!easy->done)
        {
            ++*running;
        }
    }
    return CURLM_OK;
}

CURLMsg* curl_multi_info_read(CURLM* multi, int* remaining)
{
    auto fake = static_cast<FakeMulti*>(multi);
    *remaining = 0;
    for (auto easy : fake->requests)
    {
        if (easy->done && !easy->reported)
        {
            easy->reported = true;
            fake->message.msg = CURLMSG_DONE;
            fake->message.easy_handle = easy;
            fake->message.data.result = CURLE_OK;
            *remaining = 1;
            return &fake->message;
        }
    }
    return nullptr;
}

CURLMcode curl_multi_wait(CURLM*, struct curl_waitfd*, unsigned int, int, int* ready)
{
    *ready = 0;
    return CURLM_OK;
}

#if defined(CURL_AT_LEAST_VERSION) && CURL_AT_LEAST_VERSION(7,69,0)
CURLMcode curl_multi_poll(CURLM* multi, struct curl_waitfd* fds, unsigned int count, int timeout, int* ready)
{
    return curl_multi_wait(multi, fds, count, timeout, ready);
}
#endif
}

namespace xbox
{
namespace httpclient
{
HRESULT HrFromCurlm(CURLMcode code) noexcept
{
    return code == CURLM_OK ? S_OK : E_FAIL;
}

CurlEasyRequest::CurlEasyRequest(CURL* easy, HCCallHandle call, XAsyncBlock* async)
    : m_curlEasyHandle{ easy }, m_hcCallHandle{ call }, m_asyncBlock{ async }
{
}

CurlEasyRequest::~CurlEasyRequest()
{
    Delete(static_cast<FakeEasy*>(m_curlEasyHandle));
}

Result<HC_UNIQUE_PTR<CurlEasyRequest>> CurlEasyRequest::Initialize(HCCallHandle call, XAsyncBlock* async)
{
    auto easy = Make<FakeEasy>();
    easy->state = static_cast<RequestState*>(async->context);
    http_stl_allocator<CurlEasyRequest> allocator;
    return HC_UNIQUE_PTR<CurlEasyRequest>{
        new (allocator.allocate(1)) CurlEasyRequest{ easy, call, async } };
}

CURL* CurlEasyRequest::Handle() const noexcept
{
    return m_curlEasyHandle;
}

void CurlEasyRequest::Complete(CURLcode code)
{
    Fail(code == CURLE_OK ? S_OK : E_FAIL);
}

void CurlEasyRequest::Fail(HRESULT hr)
{
    auto state = static_cast<RequestState*>(m_asyncBlock->context);
    ++state->completions;
    state->result = hr;
}
}
}

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{
class MultiHarness
{
public:
    explicit MultiHarness(XTaskQueueDispatchMode mode = XTaskQueueDispatchMode::Manual)
    {
        VERIFY_SUCCEEDED(XTaskQueueCreate(mode, mode, &queue));
        XTaskQueuePortHandle port{};
        VERIFY_SUCCEEDED(XTaskQueueGetPort(queue, XTaskQueuePort::Work, &port));
        auto result = CurlMulti::Initialize(port);
        VERIFY_SUCCEEDED(result.hr);
        multi = result.ExtractPayload();
        backend = g_lastMulti;
    }

    ~MultiHarness()
    {
        Cleanup();
        XTaskQueueCloseHandle(queue);
    }

    RequestState& Add(bool stalled = false)
    {
        requests.emplace_back();
        auto& state = requests.back();
        state.stalled = stalled;
        state.async.context = &state;
        auto result = CurlEasyRequest::Initialize(nullptr, &state.async);
        VERIFY_SUCCEEDED(result.hr);
        auto easy = result.ExtractPayload();
        VERIFY_SUCCEEDED(multi->AddRequest(std::move(easy)));
        return state;
    }

    void Poll()
    {
        VERIFY_IS_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 1000));
    }

    void Cleanup()
    {
        if (!multi)
        {
            return;
        }
        XAsyncBlock async{};
        async.queue = queue;
        VERIFY_SUCCEEDED(CurlMulti::CleanupAsync(std::move(multi), &async));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (XAsyncGetStatus(&async, false) == E_PENDING && std::chrono::steady_clock::now() < deadline)
        {
            XTaskQueueDispatch(queue, XTaskQueuePort::Work, 10);
            XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);
        }
        VERIFY_SUCCEEDED(XAsyncGetStatus(&async, false));
    }

    XTaskQueueHandle queue{};
    HC_UNIQUE_PTR<CurlMulti> multi;
    http_internal_list<RequestState> requests;
    FakeMulti* backend{};
};
}

DEFINE_TEST_CLASS(CurlMultiTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(CurlMultiTests);

    DEFINE_TEST_CASE(StalledTransferDoesNotAccumulatePollingChains)
    {
        MultiHarness harness;
        auto& stalled = harness.Add(true);
        harness.Poll();

        for (unsigned round = 0; round < 16; ++round)
        {
            for (unsigned request = 0; request < 8; ++request)
            {
                harness.Add();
            }
            harness.Poll();
        }
        for (auto& state : harness.requests)
        {
            if (!state.stalled)
            {
                VERIFY_ARE_EQUAL(1u, state.completions);
                VERIFY_SUCCEEDED(state.result);
            }
        }

        const unsigned performsBefore = harness.backend->performs;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
        while (std::chrono::steady_clock::now() < deadline)
        {
            XTaskQueueDispatch(harness.queue, XTaskQueuePort::Work, 10);
        }
        // One 50ms driver, with slack for timer granularity. Previously all 129
        // requests left a polling chain alive for as long as the stalled request.
        const unsigned performs = harness.backend->performs - performsBefore;
        VERIFY_IS_TRUE(performs > 0);
        VERIFY_IS_TRUE(performs <= 8);
        VERIFY_ARE_EQUAL(0u, stalled.completions);
        harness.Cleanup();
        VERIFY_ARE_EQUAL(1u, stalled.completions);
        VERIFY_ARE_EQUAL(E_ABORT, stalled.result);
    }

    DEFINE_TEST_CASE(IdleDriverRestartsForNewRequests)
    {
        MultiHarness harness;
        for (unsigned i = 0; i < 32; ++i)
        {
            auto& state = harness.Add();
            harness.Poll();
            VERIFY_ARE_EQUAL(1u, state.completions);
            VERIFY_SUCCEEDED(state.result);
            VERIFY_IS_FALSE(XTaskQueueDispatch(harness.queue, XTaskQueuePort::Work, 0));
        }
    }

    DEFINE_TEST_CASE(CleanupCancelsInitialAndDelayedDriver)
    {
        for (bool dispatchFirst : { false, true })
        {
            MultiHarness harness;
            auto& first = harness.Add(true);
            auto& second = harness.Add();
            if (dispatchFirst)
            {
                harness.Poll();
                VERIFY_ARE_EQUAL(1u, second.completions);
                VERIFY_SUCCEEDED(second.result);
            }
            harness.Cleanup();
            VERIFY_ARE_EQUAL(1u, first.completions);
            VERIFY_ARE_EQUAL(E_ABORT, first.result);
            VERIFY_ARE_EQUAL(1u, second.completions);
            VERIFY_ARE_EQUAL((dispatchFirst ? S_OK : E_ABORT), second.result);
        }
    }

    DEFINE_TEST_CASE(PerformFailureRetiresDriverAndAllowsRestart)
    {
        MultiHarness harness;
        auto& stalled = harness.Add(true);
        auto& shortRequest = harness.Add();
        harness.backend->failPerform = true;
        harness.Poll();
        VERIFY_ARE_EQUAL(1u, stalled.completions);
        VERIFY_ARE_EQUAL(E_FAIL, stalled.result);
        VERIFY_ARE_EQUAL(1u, shortRequest.completions);
        VERIFY_ARE_EQUAL(E_FAIL, shortRequest.result);
        VERIFY_IS_FALSE(XTaskQueueDispatch(harness.queue, XTaskQueuePort::Work, 0));

        auto& restarted = harness.Add();
        harness.Poll();
        VERIFY_ARE_EQUAL(1u, restarted.completions);
        VERIFY_SUCCEEDED(restarted.result);
    }

    DEFINE_TEST_CASE(InlineDriverCanCompleteAndRestartBeforeSubmitReturns)
    {
        MultiHarness harness{ XTaskQueueDispatchMode::Immediate };
        for (unsigned i = 0; i < 32; ++i)
        {
            auto& state = harness.Add();
            VERIFY_ARE_EQUAL(1u, state.completions);
            VERIFY_SUCCEEDED(state.result);
        }
    }

    DEFINE_TEST_CASE(AddRequestRacingIdleDriverDoesNotLoseWakeup)
    {
        for (unsigned i = 0; i < 64; ++i)
        {
            RequestState incoming;
            incoming.async.context = &incoming;
            MultiHarness harness;
            auto& first = harness.Add();
            auto result = CurlEasyRequest::Initialize(nullptr, &incoming.async);
            VERIFY_SUCCEEDED(result.hr);

            std::mutex mutex;
            std::condition_variable changed;
            bool performing = false;
            bool adding = false;
            harness.backend->beforePerform = [&]
            {
                std::unique_lock<std::mutex> lock{ mutex };
                performing = true;
                changed.notify_one();
                changed.wait_for(lock, std::chrono::seconds(1), [&] { return adding; });
            };

            bool dispatched = false;
            std::thread driver([&]
            {
                dispatched = XTaskQueueDispatch(harness.queue, XTaskQueuePort::Work, 1000);
            });
            bool observedPerform;
            {
                std::unique_lock<std::mutex> lock{ mutex };
                observedPerform = changed.wait_for(lock, std::chrono::seconds(1), [&] { return performing; });
                adding = true;
                changed.notify_one();
            }
            HRESULT hr = harness.multi->AddRequest(result.ExtractPayload());
            driver.join();
            harness.backend->beforePerform = nullptr;

            VERIFY_IS_TRUE(observedPerform);
            VERIFY_IS_TRUE(dispatched);
            VERIFY_SUCCEEDED(hr);
            VERIFY_ARE_EQUAL(1u, first.completions);
            harness.Poll();
            VERIFY_ARE_EQUAL(1u, incoming.completions);
            VERIFY_SUCCEEDED(incoming.result);
        }
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
