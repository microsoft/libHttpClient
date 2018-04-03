// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"
#include "../mock/mock.h"

using namespace xbox::httpclient;

const int MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS = 10000;
#if HC_UNITTEST_API
    const int MIN_HTTP_TIMEOUT_IN_MS = 0; // speed up unit tests
#else
    const int MIN_HTTP_TIMEOUT_IN_MS = 5000;
#endif
const double MAX_DELAY_TIME_IN_SEC = 60.0;
const int RETRY_AFTER_CAP_IN_SEC = 15;
#define RETRY_AFTER_HEADER ("Retry-After")

STDAPI 
HCHttpCallCreate(
    _Out_ hc_call_handle_t* callHandle
    ) HC_NOEXCEPT
try 
{
    if (callHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    HC_CALL* call = new HC_CALL();

    call->retryAllowed = httpSingleton->m_retryAllowed;
    call->timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
    call->timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
    call->retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;
    call->retryIterationNumber = 0;
    call->id = ++httpSingleton->m_lastId;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCreate [ID %llu]", call->id);

    *callHandle = call;
    return S_OK;
}
CATCH_RETURN()

hc_call_handle_t HCHttpCallDuplicateHandle(
    _In_ hc_call_handle_t call
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        return nullptr;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallDuplicateHandle [ID %llu]", static_cast<HC_CALL*>(call)->id);
    ++call->refCount;

    return call;
}
CATCH_RETURN_WITH(nullptr)

STDAPI 
HCHttpCallCloseHandle(
    _In_ hc_call_handle_t call
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallCloseHandle [ID %llu]", call->id);
    int refCount = --call->refCount;
    if (refCount <= 0)
    {
        assert(refCount == 0); // should only fire at 0
        delete call;
    }

    return S_OK;
}
CATCH_RETURN()

HRESULT perform_http_call(
    _In_ std::shared_ptr<http_singleton> httpSingleton,
    _In_ hc_call_handle_t call,
    _In_ AsyncBlock* asyncBlock
    )
{
    HRESULT hr = BeginAsync(asyncBlock, call, perform_http_call, __FUNCTION__,
        [](AsyncOp opCode, AsyncProviderData* data)
    {
        switch (opCode)
        {
            case AsyncOp_DoWork:
            {
                hc_call_handle_t call = static_cast<hc_call_handle_t>(data->context);
                auto httpSingleton = get_http_singleton(false);
                if (nullptr == httpSingleton)
                    return E_INVALIDARG;

                bool matchedMocks = false;
                if (httpSingleton->m_mocksEnabled)
                {
                    matchedMocks = Mock_Internal_HCHttpCallPerform(call);
                    if (matchedMocks)
                    {
                        CompleteAsync(data->async, S_OK, 0);
                    }
                }

                if (!matchedMocks) // if there wasn't a matched mock, then real call
                {
                    HCCallPerformFunction performFunc = httpSingleton->m_performFunc;
                    if (performFunc != nullptr)
                    {
                        try
                        {
                            performFunc(call, data->async);
                        }
                        catch (...)
                        {
                            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu]: failed", static_cast<HC_CALL*>(call)->id);
                        }
                    }
                }

                return E_PENDING;
            }

            default: return S_OK;
        }
    });

    if (SUCCEEDED(hr))
    {
        uint32_t delayInMilliseconds = static_cast<uint32_t>(call->delayBeforeRetry.count());
        hr = ScheduleAsync(asyncBlock, delayInMilliseconds);
    }

    return hr;
}

void clear_http_call_response(_In_ hc_call_handle_t call)
{
    call->responseString.clear();
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
            if (value > RETRY_AFTER_CAP_IN_SEC)
            {
                // Cap the Retry-After header so users won't be locked out of an endpoint 
                // for a long time the limit is hit near the end of a period
                value = RETRY_AFTER_CAP_IN_SEC;
            }

            return std::chrono::seconds(value);
        }
    }

    return std::chrono::seconds(0);
}

bool http_call_should_retry(
    _In_ hc_call_handle_t call,
    _In_ const chrono_clock_t::time_point& responseReceivedTime)
{
    if (!call->retryAllowed)
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
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] remainingTimeBeforeTimeout %lld ms", call->id, remainingTimeBeforeTimeout.count());
        if (remainingTimeBeforeTimeout.count() <= MIN_HTTP_TIMEOUT_IN_MS) // Need at least 5 seconds to bother making a call
        {
            return false;
        }

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
        double secondsToWait = MIN(secondsToWaitUncapped, MAX_DELAY_TIME_IN_SEC); // cap max wait to 1 min
        std::chrono::milliseconds waitTime = std::chrono::milliseconds(static_cast<int64_t>(secondsToWait * 1000.0));
        if (retryAfter.count() > 0)
        {
            // Use either the waitTime or Retry-After header, whichever is bigger
            call->delayBeforeRetry = std::chrono::milliseconds(__max(waitTime.count(), retryAfter.count()));
        }
        else
        {
            call->delayBeforeRetry = waitTime;
        }
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] delayBeforeRetry %lld ms", call->id, call->delayBeforeRetry.count());

        if (remainingTimeBeforeTimeout < call->delayBeforeRetry + std::chrono::milliseconds(MIN_HTTP_TIMEOUT_IN_MS))
        {
            // Don't bother retrying when out of time
            return false;
        }

        if (httpStatus == 500) // Internal Error
        {
            // For 500 - Internal Error, wait at least 10 seconds before retrying.
            if (call->delayBeforeRetry.count() < MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS)
            {
                call->delayBeforeRetry = std::chrono::milliseconds(MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS);
            }
        }

        return true;
    }

    return false;
}

bool should_fast_fail(
    _In_ http_retry_after_api_state apiState,
    _In_ HC_CALL* call,
    _In_ const chrono_clock_t::time_point& currentTime
    )
{
    if (apiState.statusCode < 400)
    {
        return false;
    }

    std::chrono::milliseconds remainingTimeBeforeRetryAfterInMS = std::chrono::duration_cast<std::chrono::milliseconds>(apiState.retryAfterTime - currentTime);
    if (remainingTimeBeforeRetryAfterInMS.count() <= 0)
    {
        return false;
    }

    std::chrono::seconds timeoutWindowInSeconds = std::chrono::seconds(call->timeoutWindowInSeconds);
    chrono_clock_t::time_point timeoutTime = call->firstRequestStartTime + timeoutWindowInSeconds;

    // If the Retry-After will happen first, just wait till Retry-After is done, and don't fast fail
    if (apiState.retryAfterTime < timeoutTime)
    {
        auto retryAfterCountInMS = static_cast<uint32_t>(remainingTimeBeforeRetryAfterInMS.count());
        return false;
    }
    else
    {
        return true;
    }
}

typedef struct retry_context
{
    HC_CALL* call;
    AsyncBlock* outerAsyncBlock;
    async_queue_t outerQueue;
} retry_context;

void retry_http_call_until_done(
    _In_ retry_context* retryContext
    )
{
    auto httpSingleton = get_http_singleton(false);
    if (nullptr == httpSingleton)
    {
        CompleteAsync(retryContext->outerAsyncBlock, S_OK, 0);
    }

    auto requestStartTime = chrono_clock_t::now();
    if (retryContext->call->retryIterationNumber == 0)
    {
        retryContext->call->firstRequestStartTime = requestStartTime;
    }
    retryContext->call->retryIterationNumber++;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] Iteration %d", 
        retryContext->call->id, 
        retryContext->call->retryIterationNumber);

    http_retry_after_api_state apiState = httpSingleton->get_retry_state(retryContext->call->retryAfterCacheId);
    if (apiState.statusCode >= 400)
    {
        if (should_fast_fail(apiState, retryContext->call, requestStartTime))
        {
            HCHttpCallResponseSetStatusCode(retryContext->call, apiState.statusCode);
            HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] Fast fail %d", retryContext->call->id, apiState.statusCode);
            CompleteAsync(retryContext->outerAsyncBlock, S_OK, 0);
        }
        else
        {
            httpSingleton->clear_retry_state(retryContext->call->retryAfterCacheId);
        }
    }

    async_queue_t nestedQueue;
    CreateNestedAsyncQueue(retryContext->outerQueue, &nestedQueue);
    AsyncBlock* nestedBlock = new AsyncBlock;
    ZeroMemory(nestedBlock, sizeof(AsyncBlock));
    nestedBlock->queue = nestedQueue;
    nestedBlock->context = retryContext;

    nestedBlock->callback = [](AsyncBlock* nestedAsyncBlock)
    {
        retry_context* retryContext = static_cast<retry_context*>(nestedAsyncBlock->context);
        auto responseReceivedTime = chrono_clock_t::now();

        uint32_t timeoutWindowInSeconds = 0;
        HCHttpCallRequestGetTimeoutWindow(retryContext->call, &timeoutWindowInSeconds);

        CloseAsyncQueue(nestedAsyncBlock->queue);
        delete nestedAsyncBlock;

        if (http_call_should_retry(retryContext->call, responseReceivedTime))
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerformExecute [ID %llu] Retry after %lld ms",
                retryContext->call->id,
                retryContext->call->delayBeforeRetry.count());

            clear_http_call_response(retryContext->call);
            retry_http_call_until_done(retryContext);
        }
        else
        {
            CompleteAsync(retryContext->outerAsyncBlock, S_OK, 0);
        }
    };

    HRESULT hr = perform_http_call(httpSingleton, retryContext->call, nestedBlock);
    if (FAILED(hr))
    {
        CompleteAsync(retryContext->outerAsyncBlock, hr, 0);
        return;
    }
}

STDAPI 
HCHttpCallPerform(
    _In_ hc_call_handle_t call,
    _In_ AsyncBlock* asyncBlock
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu]", call->id);
    call->performCalled = true;

    std::shared_ptr<retry_context> retryContext = std::make_shared<retry_context>();
    retryContext->call = static_cast<HC_CALL*>(call);
    retryContext->outerAsyncBlock = asyncBlock;
    retryContext->outerQueue = asyncBlock->queue;
    retry_context* rawRetryContext = static_cast<retry_context*>(shared_ptr_cache::store<retry_context>(retryContext));

    HRESULT hr = BeginAsync(asyncBlock, rawRetryContext, HCHttpCallPerform, __FUNCTION__,
        [](_In_ AsyncOp op, _Inout_ AsyncProviderData* data)
    {
        switch (op)
        {
            case AsyncOp_DoWork:
                retry_http_call_until_done(static_cast<retry_context*>(data->context));
                return E_PENDING;

            case AsyncOp_Cleanup:
                shared_ptr_cache::fetch<retry_context>(data->context, true);
                break;
        }

        return S_OK;
    });

    if (hr == S_OK)
    {
        hr = ScheduleAsync(asyncBlock, 0);
    }

    return hr;
}
CATCH_RETURN()

STDAPI_(uint64_t)
HCHttpCallGetId(
    _In_ hc_call_handle_t call
    ) HC_NOEXCEPT
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
HCHttpCallSetContext(
    _In_ hc_call_handle_t call,
    _In_ void* context
    ) HC_NOEXCEPT
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
    _In_ hc_call_handle_t call,
    _In_ void** context
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        return 0;
    }
    
    *context = call->context;

    return S_OK;
}
CATCH_RETURN()
