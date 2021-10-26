// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"
#include "../Mock/lhc_mock.h"

using namespace xbox::httpclient;

const int MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS = 10000;
#if HC_UNITTEST_API
    const int MIN_HTTP_TIMEOUT_IN_MS = 0; // speed up unit tests
#else
    const int MIN_HTTP_TIMEOUT_IN_MS = 5000;
#endif
const double MAX_DELAY_TIME_IN_SEC = 60.0;
#define RETRY_AFTER_HEADER ("Retry-After")

HC_CALL::~HC_CALL()
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "HCCallHandle dtor");
}

STDAPI 
HCHttpCallCreate(
    _Out_ HCCallHandle* callHandle
    ) noexcept
try 
{
    if (callHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    HC_CALL* call = Make<HC_CALL>();
    if (call == nullptr)
    {
        return E_OUTOFMEMORY;
    }
    call->retryAllowed = httpSingleton->m_retryAllowed;
    call->timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
    call->timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
    call->retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;
    call->retryIterationNumber = 0;
    call->id = ++httpSingleton->m_lastId;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCreate [ID %llu]", TO_ULL(call->id));

    *callHandle = call;
    return S_OK;
}
CATCH_RETURN()

STDAPI_(HCCallHandle) HCHttpCallDuplicateHandle(
    _In_ HCCallHandle call
    ) noexcept
try
{
    if (call == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallDuplicateHandle [ID %llu]", TO_ULL(static_cast<HC_CALL*>(call)->id));
    ++call->refCount;

    return call;
}
CATCH_RETURN_WITH(nullptr)

STDAPI 
HCHttpCallCloseHandle(
    _In_ HCCallHandle call
    ) noexcept
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCloseHandle [ID %llu]", TO_ULL(call->id));
    int refCount = --call->refCount;
    if (refCount <= 0)
    {
        ASSERT(refCount == 0); // should only fire at 0
        Delete(call);
    }

    return S_OK;
}
CATCH_RETURN()

HRESULT perform_http_call(
    _In_ std::shared_ptr<http_singleton> httpSingleton,
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock
    )
{
    return XAsyncBegin(asyncBlock, call, reinterpret_cast<void*>(perform_http_call), __FUNCTION__,
        [](XAsyncOp opCode, const XAsyncProviderData* data)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
        {
            return E_HC_NOT_INITIALISED;
        }

        HCCallHandle call = static_cast<HCCallHandle>(data->context);

        switch (opCode)
        {
            case XAsyncOp::Begin:
            {
                uint32_t delayInMilliseconds = static_cast<uint32_t>(call->delayBeforeRetry.count());
                HC_TRACE_VERBOSE(HTTPCLIENT, "HttpCall [ID %llu] scheduling with delay %u", call->id, delayInMilliseconds);
                return XAsyncSchedule(data->async, delayInMilliseconds);
            }

            case XAsyncOp::DoWork:
            {
                bool matchedMocks = false;

                matchedMocks = Mock_Internal_HCHttpCallPerformAsync(call);
                if (matchedMocks)
                {
                    XAsyncComplete(data->async, S_OK, 0);
                }
                else // if there wasn't a matched mock, then real call
                {
                    HttpPerformInfo const& info = httpSingleton->m_httpPerform;
                    if (info.handler != nullptr)
                    {
                        try
                        {
                            info.handler(call, data->async, info.context, httpSingleton->m_performEnv.get());
                        }
                        catch (...)
                        {
                            if (call->traceCall) { HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu]: failed", TO_ULL(static_cast<HC_CALL*>(call)->id)); }
                        }
                    }
                }

                return E_PENDING;
            }

            default: return S_OK;
        }
    });
}

void clear_http_call_response(_In_ HCCallHandle call)
{
    call->responseString.clear();
    call->responseBodyBytes.clear();
    call->responseHeaders.clear();
    call->statusCode = 0;
    call->networkErrorCode = S_OK;
    call->platformNetworkErrorCode = 0;
    call->task.reset();
}

std::chrono::seconds GetRetryAfterHeaderTime(_In_ HC_CALL* call)
{
    auto it = call->responseHeaders.find(RETRY_AFTER_HEADER);
    if (it != call->responseHeaders.end())
    {
        int value = 0;
        http_internal_stringstream ss(it->second);
        ss >> value;

        if (!ss.fail())
        {
            return std::chrono::seconds(value);
        }
    }

    return std::chrono::seconds(0);
}

bool http_call_should_retry(
    _In_ HCCallHandle call,
    _In_ const chrono_clock_t::time_point& responseReceivedTime)
{
    if (!call->retryAllowed)
    {
        return false;
    }

    if (call->networkErrorCode == E_HC_NO_NETWORK)
    {
        return false;
    }

    auto httpStatus = call->statusCode;

    if (httpStatus == 408 || // Request Timeout
        httpStatus == 429 || // Too Many Requests 
        httpStatus == 500 || // Internal Error
        httpStatus == 502 || // Bad Gateway 
        httpStatus == 503 || // Service Unavailable
        httpStatus == 504 || // Gateway Timeout
        call->networkErrorCode != S_OK)
    {
        std::chrono::milliseconds retryAfter = GetRetryAfterHeaderTime(call);

        // Compute how much time left before hitting the TimeoutWindow setting
        std::chrono::milliseconds timeElapsedSinceFirstCall = std::chrono::duration_cast<std::chrono::milliseconds>(responseReceivedTime - call->firstRequestStartTime);

        uint32_t timeoutWindowInSeconds = 0;
        HCHttpCallRequestGetTimeoutWindow(call, &timeoutWindowInSeconds);
        std::chrono::seconds timeoutWindow = std::chrono::seconds(timeoutWindowInSeconds);
        std::chrono::milliseconds remainingTimeBeforeTimeout = timeoutWindow - timeElapsedSinceFirstCall;
        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] remainingTimeBeforeTimeout %lld ms", TO_ULL(call->id), remainingTimeBeforeTimeout.count()); }

        // Based on the retry iteration, delay 2,4,8,16,etc seconds by default between retries
        // Jitter the response between the current and next delay based on system clock
        // Max wait time is 1 minute
        uint32_t retryDelayInSeconds = 0;
        HCHttpCallRequestGetRetryDelay(call, &retryDelayInSeconds);
        double secondsToWaitMin = std::pow(retryDelayInSeconds, call->retryIterationNumber);
        double secondsToWaitMax = std::pow(retryDelayInSeconds, call->retryIterationNumber + 1);
        double secondsToWaitDelta = secondsToWaitMax - secondsToWaitMin;
        double lerpScaler = (responseReceivedTime.time_since_epoch().count() % 10000) / 10000.0; // from 0 to 1 based on clock
#if HC_UNITTEST_API
        lerpScaler = 0; // make unit tests deterministic
#endif
        double secondsToWaitUncapped = secondsToWaitMin + secondsToWaitDelta * lerpScaler; // lerp between min & max wait
        double secondsToWait = std::min(secondsToWaitUncapped, MAX_DELAY_TIME_IN_SEC); // cap max wait to 1 min
        std::chrono::milliseconds waitTime = std::chrono::milliseconds(static_cast<int64_t>(secondsToWait * 1000.0));
        if (retryAfter.count() > 0)
        {
            // Jitter to spread the load of Retry-After out between the devices trying to retry
            std::chrono::milliseconds retryAfterMin = retryAfter;
            std::chrono::milliseconds retryAfterMax = std::chrono::milliseconds(static_cast<int64_t>(retryAfter.count() * 1.2));
            auto retryAfterDelta = retryAfterMax.count() - retryAfterMin.count();
            std::chrono::milliseconds retryAfterJittered = std::chrono::milliseconds(static_cast<int64_t>(retryAfterMin.count() + retryAfterDelta * lerpScaler)); // lerp between min & max wait

            // Use either the waitTime or the jittered Retry-After header, whichever is bigger
            call->delayBeforeRetry = std::chrono::milliseconds(std::max(waitTime.count(), retryAfterJittered.count()));
        }
        else
        {
            call->delayBeforeRetry = waitTime;
        }
        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] delayBeforeRetry %lld ms", TO_ULL(call->id), call->delayBeforeRetry.count()); }

        if (httpStatus == 500) // Internal Error
        {
            // For 500 - Internal Error, wait at least 10 seconds before retrying.
            if (call->delayBeforeRetry.count() < MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS)
            {
                call->delayBeforeRetry = std::chrono::milliseconds(MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS);
                if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] 500: delayBeforeRetry %lld ms", TO_ULL(call->id), call->delayBeforeRetry.count()); }
            }
        }

        bool shouldRetry{ true };

        if (remainingTimeBeforeTimeout.count() <= MIN_HTTP_TIMEOUT_IN_MS) 
        {
            // Need at least 5 seconds to bother making a call
            shouldRetry = false;
        }
        else if (remainingTimeBeforeTimeout < call->delayBeforeRetry + std::chrono::milliseconds(MIN_HTTP_TIMEOUT_IN_MS))
        {
            // Don't bother retrying when out of time
            shouldRetry = false;
        }

        // Remember result if there was an error and there was a Retry-After header
        if (call->retryAfterCacheId != 0 &&
            retryAfter.count() > 0 &&
            httpStatus > 400)
        {
            auto retryAfterTime = retryAfter + responseReceivedTime;
            http_retry_after_api_state state{ retryAfterTime, httpStatus, shouldRetry };
            auto httpSingleton = get_http_singleton();
            if (httpSingleton)
            {
                httpSingleton->set_retry_state(call->retryAfterCacheId, state);
            }
        }

        return shouldRetry;
    }

    return false;
}

bool should_fast_fail(
    _In_ HC_CALL* call,
    _In_ const chrono_clock_t::time_point& currentTime,
    _In_ std::shared_ptr<http_singleton> state
)
{
    std::lock_guard<std::recursive_mutex> lock(state->m_retryAfterCacheLock);
    http_retry_after_api_state apiState = state->get_retry_state(call->retryAfterCacheId);

    if (apiState.statusCode < 400)
    {
        return false;
    }

    std::chrono::milliseconds remainingTimeBeforeRetryAfterInMS = std::chrono::duration_cast<std::chrono::milliseconds>(apiState.retryAfterTime - currentTime);
    if (remainingTimeBeforeRetryAfterInMS.count() <= 0)
    {
        // We are outside the Retry-After window for this endpoint. Clear retry state and make HTTP call
        state->clear_retry_state(call->retryAfterCacheId);
        return false;
    }

    std::chrono::seconds timeoutWindowInSeconds = std::chrono::seconds(call->timeoutWindowInSeconds);
    chrono_clock_t::time_point timeoutTime = call->firstRequestStartTime + timeoutWindowInSeconds;
    if (!apiState.callPending && apiState.retryAfterTime < timeoutTime)
    {
        // Don't have multiple calls waiting for the Retry-After window for a single endpoint.
        // This causes a flood of calls to the endpoint as soon as the Retry-After windows elapses.
        // If the Retry-After will happen first, just wait till Retry-After is done, and don't fast fail.
        call->delayBeforeRetry = remainingTimeBeforeRetryAfterInMS;
        apiState.callPending = true;

        state->set_retry_state(call->retryAfterCacheId, apiState);
        return false;
    }

    call->statusCode = apiState.statusCode;
    return true;
}


class HcCallWrapper
{
public:
    HcCallWrapper(_In_ HC_CALL* call)
    {
        assert(call != nullptr);
        if (call != nullptr)
        {
            m_call = HCHttpCallDuplicateHandle(call);
        }
    }

    ~HcCallWrapper()
    {
        if (m_call)
        {
            HCHttpCallCloseHandle(m_call);
        }
    }

    HC_CALL* get()
    {
        return m_call;
    }

private:
    HC_CALL* m_call{ nullptr };
};

typedef struct retry_context
{
    std::shared_ptr<HcCallWrapper> call;
    XAsyncBlock* outerAsyncBlock;
    XTaskQueueHandle outerQueue;
} retry_context;

void notify_call_routed_handlers(std::shared_ptr<http_singleton> httpSingleton, HC_CALL* call)
{
    std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
    for (const auto& pair : httpSingleton->m_callRoutedHandlers)
    {
        pair.second.first(call, pair.second.second);
    }
}

void retry_http_call_until_done(
    _In_ HC_UNIQUE_PTR<retry_context> retryContext
    )
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        HC_TRACE_WARNING(HTTPCLIENT, "Http call after HCCleanup was called. Aborting call.");
        XAsyncComplete(retryContext->outerAsyncBlock, E_HC_NOT_INITIALISED, 0);
        return;
    }

    auto requestStartTime = chrono_clock_t::now();
    HC_CALL* call = retryContext->call->get();
    if (call->retryIterationNumber == 0)
    {
        call->firstRequestStartTime = requestStartTime;
    }
    call->retryIterationNumber++;
    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] Iteration %d", TO_ULL(call->id), call->retryIterationNumber); }

    if (should_fast_fail(call, requestStartTime, httpSingleton))
    {
        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] Fast fail %d", TO_ULL(call->id), call->statusCode); }
        XAsyncComplete(retryContext->outerAsyncBlock, S_OK, 0);
        return;
    }

    auto nestedBlock = http_allocate_unique<XAsyncBlock>();
    if (nestedBlock == nullptr)
    {
        XAsyncComplete(retryContext->outerAsyncBlock, E_OUTOFMEMORY, 0);
        return;
    }

    XTaskQueueHandle nestedQueue = nullptr;
    if (retryContext->outerQueue != nullptr)
    {
        XTaskQueuePortHandle workPort;
        XTaskQueueGetPort(retryContext->outerQueue, XTaskQueuePort::Work, &workPort);
        XTaskQueueCreateComposite(workPort, workPort, &nestedQueue);
    }
    nestedBlock->queue = nestedQueue;
    nestedBlock->context = retryContext.get();
    nestedBlock->callback = [](XAsyncBlock* nestedAsyncBlock)
    {
        HC_UNIQUE_PTR<XAsyncBlock> nestedAsyncPtr{ nestedAsyncBlock };
        HC_UNIQUE_PTR<retry_context> retryContext{ static_cast<retry_context*>(nestedAsyncBlock->context) };

        auto httpSingleton = get_http_singleton();
        if (httpSingleton == nullptr)
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Http completed after HCCleanup was called. Aborting call.");
            XAsyncComplete(retryContext->outerAsyncBlock, E_HC_NOT_INITIALISED, 0);
        }
        else
        {
            auto callStatus = XAsyncGetStatus(nestedAsyncBlock, false);
            auto responseReceivedTime = chrono_clock_t::now();
            uint32_t timeoutWindowInSeconds = 0;
            HC_CALL* call = retryContext->call->get();
            HCHttpCallRequestGetTimeoutWindow(call, &timeoutWindowInSeconds);
            notify_call_routed_handlers(httpSingleton, call);

            if (SUCCEEDED(callStatus) && http_call_should_retry(call, responseReceivedTime))
            {
                if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] Retry after %lld ms", TO_ULL(call->id), call->delayBeforeRetry.count()); }
                clear_http_call_response(call);
                retry_http_call_until_done(std::move(retryContext));
            }
            else
            {
                XAsyncComplete(retryContext->outerAsyncBlock, callStatus, 0);
            }
        }

        if (nestedAsyncBlock->queue != nullptr)
        {
            XTaskQueueCloseHandle(nestedAsyncBlock->queue);
        }
        // Cleanup with happen when unique ptr's go out of scope
    };

    HRESULT hr = perform_http_call(httpSingleton, call, nestedBlock.get());
    if (SUCCEEDED(hr))
    {
        nestedBlock.release(); // at this point we know do work will be called eventually
        retryContext.release(); // at this point we know do work will be called eventually
    }
    else
    {
        // Cleanup with happen when unique ptr's go out of scope if they weren't released
        XAsyncComplete(retryContext->outerAsyncBlock, hr, 0);
        return;
    }
}

STDAPI 
HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock
    ) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] uri: %s", TO_ULL(call->id), call->url.c_str()); }
    call->performCalled = true;

    auto retryContext = http_allocate_unique<retry_context>();
    if (retryContext == nullptr)
    {
        HCHttpCallCloseHandle(call);
        return E_HC_NOT_INITIALISED;
    }
    retryContext->call = http_allocate_shared<HcCallWrapper>(static_cast<HC_CALL*>(call)); // RAII will keep the HCCallHandle alive during HTTP call
    retryContext->outerAsyncBlock = asyncBlock;
    retryContext->outerQueue = asyncBlock->queue;

    HRESULT hr = XAsyncBegin(asyncBlock, retryContext.get(), reinterpret_cast<void*>(HCHttpCallPerformAsync), __FUNCTION__,
        [](_In_ XAsyncOp op, _In_ const XAsyncProviderData* data)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
        {
            return E_HC_NOT_INITIALISED;
        }

        switch (op)
        {
            case XAsyncOp::DoWork:
            {
                HC_UNIQUE_PTR<retry_context> retryContext{ static_cast<retry_context*>(data->context) };
                retry_http_call_until_done(std::move(retryContext));
                return E_PENDING;
            }
                
            default:
                break;
        }

        return S_OK;
    });

    if (SUCCEEDED(hr))
    {
        hr = XAsyncSchedule(asyncBlock, 0);
        if (SUCCEEDED(hr))
        {
            retryContext.release(); // at this point we know do work will be called eventually
        }
    }

    return hr;
}
CATCH_RETURN()

STDAPI_(uint64_t)
HCHttpCallGetId(
    _In_ HCCallHandle call
    ) noexcept
try
{
    if (call == nullptr)
    {
        return 0;
    }
    return call->id;
}
CATCH_RETURN()

STDAPI
HCHttpCallSetTracing(
    _In_ HCCallHandle call,
    _In_ bool logCall
    ) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }
    call->traceCall = logCall;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallSetContext(
    _In_ HCCallHandle call,
    _In_opt_ void* context
    ) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    call->context = context;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallGetContext(
    _In_ HCCallHandle call,
    _In_ void** context
    ) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }
    
    *context = call->context;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallGetRequestUrl(
    _In_ HCCallHandle call,
    _Out_ const char** url
    ) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    *url = call->url.data();
    return S_OK;
}
CATCH_RETURN()

bool http_header_compare::operator()(http_internal_string const& l, http_internal_string const& r) const
{
    return str_icmp(l, r) < 0;
}
