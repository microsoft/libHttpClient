// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include <httpClient/httpProvider.h>
#include "httpcall.h"
#include "../Mock/lhc_mock.h"
#include "compression.h"
// remove this
#include <iostream>

using namespace xbox::httpclient;

#define MAX_DELAY_TIME_IN_SEC 60
#define MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS 10000
#if HC_UNITTEST_API
// Speed up unit tests
#define MIN_HTTP_TIMEOUT_IN_MS 0
#else
#define MIN_HTTP_TIMEOUT_IN_MS 5000
#endif
#define RETRY_AFTER_HEADER ("Retry-After")

HC_CALL::HC_CALL(uint64_t _id, IHttpProvider& provider) :
    id{ _id },
    m_provider{ provider }
{
}

HC_CALL::~HC_CALL()
{
    HC_TRACE_INFORMATION(HTTPCLIENT, __FUNCTION__);
}

// Context for PerformAsyncProvider. Ensures HC_CALL lifetime until perform completes
struct PerformContext
{
    PerformContext(HC_CALL* _call, XAsyncBlock* _asyncBlock) : asyncBlock{ _asyncBlock }
    {
        call = HCHttpCallDuplicateHandle(_call);
    }

    ~PerformContext()
    {
        if (call)
        {
            HCHttpCallCloseHandle(call);
        }
        if (workQueue)
        {
            XTaskQueueCloseHandle(workQueue);
        }
        if (providerQueue)
        {
            XTaskQueueCloseHandle(providerQueue);
        }
    }

    HC_CALL* call{ nullptr };
    XAsyncBlock* const asyncBlock; // client owned
    XTaskQueueHandle workQueue{ nullptr };
    XTaskQueueHandle providerQueue{ nullptr };
};

HRESULT HC_CALL::PerformAsync(XAsyncBlock* async) noexcept
{
    HC_UNIQUE_PTR<PerformContext> performContext = http_allocate_unique<PerformContext>(this, async);
    RETURN_IF_FAILED(XAsyncBegin(async, performContext.get(), nullptr, __FUNCTION__, PerfomAsyncProvider));
    performContext.release();
    return S_OK;
}

HRESULT CALLBACK HC_CALL::PerfomAsyncProvider(XAsyncOp op, XAsyncProviderData const* data)
{
    PerformContext* context{ static_cast<PerformContext*>(data->context) };
    HC_CALL* call{ context->call };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        call->performCalled = true;
        call->m_performStartTime = chrono_clock_t::now();

        // Initialize work queues
        XTaskQueuePortHandle workPort{ nullptr };
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &context->workQueue));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &context->providerQueue));

        // Fail Fast check
        uint32_t performDelay{ 0 };
        Result<bool> shouldFailFast = call->ShouldFailFast(performDelay);
        RETURN_IF_FAILED(shouldFailFast.hr);
        if (shouldFailFast.Payload())
        {
            if (call->traceCall)
            {
                HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::PerfomAsyncProvider [ID %llu] Fast fail %d", TO_ULL(call->id), call->statusCode);
            }
            XAsyncComplete(data->async, S_OK, 0);
            return S_OK;
        }
        else
        {
            // If Custom ReponseWriteFunction is specified and compressedResponse is specified reset to default response body write callback
            if (((void*) call->responseBodyWriteFunction != (void *) HC_CALL::ResponseBodyWrite) && call->compressedResponse) {
                call->responseBodyWriteFunction = HC_CALL::ResponseBodyWrite;
                call->responseBodyWriteFunctionContext = nullptr;
            }

            // Compress body before call if applicable
            if (Compression::Available() && call->compressionLevel != HCCompressionLevel::None)
            {
                // Schedule compression task
                RETURN_IF_FAILED(XTaskQueueSubmitDelayedCallback(context->workQueue, XTaskQueuePort::Work, performDelay, context, HC_CALL::CompressRequestBody));
            }
            else
            {
                RETURN_IF_FAILED(XTaskQueueSubmitDelayedCallback(context->workQueue, XTaskQueuePort::Work, performDelay, context, HC_CALL::PerformSingleRequest));
            }

        }
        return S_OK;
    }
    case XAsyncOp::Cancel:
    {
        if (call->traceCall)
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::PerfomAsyncProvider Cancel [ID %llu]", TO_ULL(call->id));
        }

        // Terminate the Perform workQueue and allow XAsync to handle synchronization. If PerformSingleRequest has been scheduled but is not yet
        // running, it will be canceled and the Perform operation will be completed then.  If a request is currently running, the Perform
        // operation will be completed when that request completes (either with success or with E_ABORT, depending on whether the request succeeded).
        RETURN_IF_FAILED(XTaskQueueTerminate(context->workQueue, false, nullptr, nullptr));

        return S_OK;
    }
    case XAsyncOp::Cleanup:
    {
        // Here is potential location to uncompress body bytes

        if (call->traceCall)
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::PerfomAsyncProvider Cleanup [ID %llu]", TO_ULL(call->id));
        }

        // Cleanup PerformContext
        HC_UNIQUE_PTR<PerformContext> reclaim{ context };
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK HC_CALL::CompressRequestBody(void* c, bool canceled)
{
    PerformContext* context{ static_cast<PerformContext*>(c) };
    HC_CALL* call{ context->call };

    assert(Compression::Available());

    if (canceled)
    {
        XAsyncComplete(context->asyncBlock, E_ABORT, 0);
        return;
    }

    if (call->traceCall)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::CompressRequestBody [ID %llu] Iteration %d", TO_ULL(call->id), call->m_iterationNumber);
    }

    HCHttpCallRequestBodyReadFunction clientRequestBodyReadCallback{ nullptr };
    size_t requestBodySize{};
    void* clientRequestBodyReadCallbackContext{ nullptr };
    HRESULT hr = HCHttpCallRequestGetRequestBodyReadFunction(call, &clientRequestBodyReadCallback, &requestBodySize, &clientRequestBodyReadCallbackContext);

    if (FAILED(hr) || !clientRequestBodyReadCallback)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "HC_CALL::CompressRequestBody: Unable to get client's RequestBodyRead callback");
        return;
    }

    http_internal_vector<uint8_t> uncompressedRequestyBodyBuffer(requestBodySize);
    uint8_t* bufferPtr = &uncompressedRequestyBodyBuffer.front();
    size_t bytesWritten = 0;

    try
    {
        hr = clientRequestBodyReadCallback(call, 0, requestBodySize, clientRequestBodyReadCallbackContext, bufferPtr, &bytesWritten);

        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "HC_CALL::CompressRequestBody: client RequestBodyRead callback failed");
            return;
        }

        // Return error if client provides less bytes than expected.
        if (bytesWritten < requestBodySize)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "HC_CALL::CompressRequestBody: Expected more data written by the client based on initial request body size provided.");
            return;
        }
    }
    catch (...)
    {
        return;
    }

    http_internal_vector<uint8_t> compressedRequestBodyBuffer;

    Compression::CompressToGzip(uncompressedRequestyBodyBuffer.data(),
        requestBodySize,
        call->compressionLevel,
        compressedRequestBodyBuffer);

    // Setting back to default read request body callback to be invoked by Platform-specific code
    call->requestBodyReadFunction = HC_CALL::ReadRequestBody;
    call->requestBodyReadFunctionContext = nullptr;

    // Directly setting compressed body bytes to HCCall
    call->requestBodySize = (uint32_t)compressedRequestBodyBuffer.size();
    call->requestBodyBytes = std::move(compressedRequestBodyBuffer);
    call->requestBodyString.clear();

    // Setting GZIP as the Content Encoding
    call->requestHeaders["Content-Encoding"] = "gzip";

    uint32_t performDelay{ 0 };
    hr = XTaskQueueSubmitDelayedCallback(context->workQueue, XTaskQueuePort::Work, performDelay, context, HC_CALL::PerformSingleRequest);

    if (FAILED(hr))
    {
        XAsyncComplete(context->asyncBlock, hr, 0);
        return;
    }
}

void CALLBACK HC_CALL::PerformSingleRequest(void* c, bool canceled)
{
    PerformContext* context{ static_cast<PerformContext*>(c) };
    HC_CALL* call{ context->call };

    if (canceled)
    {
        XAsyncComplete(context->asyncBlock, E_ABORT, 0);
        return;
    }

    if (call->traceCall)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::PerformSingleRequest [ID %llu] Iteration %d", TO_ULL(call->id), call->m_iterationNumber);
    }

    call->m_iterationNumber++;

    // Allocate XAsyncBlock for iteration. Will be cleaned up in PerformSingleRequestComplete
    HC_UNIQUE_PTR<XAsyncBlock> iterationAsyncBlock{ new (http_stl_allocator<XAsyncBlock>{}.allocate(1)) XAsyncBlock
        {
            context->providerQueue,
            context,
            HC_CALL::PerformSingleRequestComplete
        }
    };
    // Here?
    HRESULT hr = XAsyncBegin(iterationAsyncBlock.get(), call, nullptr, nullptr, HC_CALL::PerformSingleRequestAsyncProvider);
    if (FAILED(hr))
    {
        XAsyncComplete(context->asyncBlock, hr, 0);
        return;
    }

    iterationAsyncBlock.release();
}

HRESULT HC_CALL::PerformSingleRequestAsyncProvider(XAsyncOp op, XAsyncProviderData const* data) noexcept
{
    HC_CALL* call{ static_cast<HC_CALL*>(data->context) };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        if (call->traceCall)
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::PerformSingleRequestAsyncProvider Begin [ID %llu]", TO_ULL(call->id));
        }

        auto httpSingleton = get_http_singleton();
        RETURN_HR_IF(E_HC_NOT_INITIALISED, !httpSingleton);

        // Check for matched mocks
        bool haveMockResponse = Mock_Internal_HCHttpCallPerformAsync(call);
        if (haveMockResponse)
        {
            XAsyncComplete(data->async, S_OK, 0);
            return S_OK;
        }

        try
        {
            RETURN_IF_FAILED(call->m_provider.PerformAsync(call, data->async));
        }
        catch (...)
        {
            if (call->traceCall)
            {
                HC_TRACE_ERROR(HTTPCLIENT, "Caught unhandled exception in HCCallPerformFunction [ID %llu]", TO_ULL(static_cast<HC_CALL*>(call)->id));
            }
            return E_FAIL;
        }
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

// This may be it...after response had been written completely
void HC_CALL::PerformSingleRequestComplete(XAsyncBlock* async)
{   
    HC_UNIQUE_PTR<XAsyncBlock> reclaimAsyncBlock{ async };
    PerformContext* context{ static_cast<PerformContext*>(async->context) };
    HC_CALL* call{ context->call };

    auto httpSingleton = get_http_singleton();
    if (httpSingleton)
    {
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
        for (const auto& pair : httpSingleton->m_callRoutedHandlers)
        {
            pair.second.first(call, pair.second.second);
        }
    }

    HRESULT hr = XAsyncGetStatus(async, false);
    if (call->traceCall)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::PerformSingleRequestComplete [ID %llu] (hr=0x%08x)", TO_ULL(static_cast<HC_CALL*>(call)->id), hr);
    }

    if (SUCCEEDED(hr))
    {
        uint32_t performDelay{ 0 };
        bool shouldRetry = call->ShouldRetry(performDelay);
        if (shouldRetry)
        {
            if (call->traceCall)
            {
                HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::PerformSingleRequestComplete [ID %llu] Retry after %lld ms", TO_ULL(call->id), TO_ULL(performDelay));
            }

            // Schedule retry
            hr = XTaskQueueSubmitDelayedCallback(context->workQueue, XTaskQueuePort::Work, performDelay, context, HC_CALL::PerformSingleRequest);
            if (SUCCEEDED(hr))
            {
                call->ResetResponseProperties();
                return;
            }
        }
    }

    // Decompress Response Bytes 
    if (Compression::Available() && call->compressedResponse == true)
    {
        http_internal_vector<uint8_t> uncompressedResponseBodyBuffer;

        Compression::DecompressFromGzip(
            call->responseBodyBytes.data(),
            call->responseBodyBytes.size(),
            uncompressedResponseBodyBuffer);

        call->responseBodyBytes.resize(uncompressedResponseBodyBuffer.size());
        call->responseBodyBytes = std::move(uncompressedResponseBodyBuffer);
    }

    // Complete perform if we aren't retrying or if there were any XAsync failures
    XAsyncComplete(context->asyncBlock, hr, 0);
}

HRESULT CALLBACK HC_CALL::ReadRequestBody(
    _In_ HCCallHandle call,
    _In_ size_t offset,
    _In_ size_t bytesAvailable,
    _In_opt_ void* /*context*/,
    _Out_writes_bytes_to_(bytesAvailable, *bytesWritten) uint8_t* destination,
    _Out_ size_t* bytesWritten
) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !call || !bytesAvailable || !destination || !bytesWritten);

    uint8_t const* requestBody = nullptr;
    uint32_t requestBodySize = 0;
    HRESULT hr = HCHttpCallRequestGetRequestBodyBytes(call, &requestBody, &requestBodySize);
    if (FAILED(hr) || (requestBody == nullptr && requestBodySize != 0) || offset > requestBodySize)
    {
        return E_FAIL;
    }

    const size_t bytesToWrite = std::min(bytesAvailable, static_cast<size_t>(requestBodySize) - offset);
    if (bytesToWrite > 0)
    {
        std::memcpy(destination, requestBody + offset, bytesToWrite);
    }

    *bytesWritten = bytesToWrite;

    return S_OK;
}

// Where is bytesAvailable coming from?
HRESULT CALLBACK HC_CALL::ResponseBodyWrite(
    _In_ HCCallHandle call,
    _In_reads_bytes_(bytesAvailable) const uint8_t* source,
    _In_ size_t bytesAvailable,
    _In_opt_ void* /*context*/
) noexcept
{
    return HCHttpCallResponseAppendResponseBodyBytes(call, source, bytesAvailable);    
}

Result<bool> HC_CALL::ShouldFailFast(uint32_t& performDelay)
{
    std::shared_ptr<http_singleton> state = get_http_singleton();
    RETURN_HR_IF(E_HC_NOT_INITIALISED, !state);

    std::lock_guard<std::recursive_mutex> lock(state->m_retryAfterCacheLock);
    http_retry_after_api_state apiState = state->get_retry_state(retryAfterCacheId);

    if (apiState.statusCode < 400)
    {
        // Most recent call to the endpoint was successful, don't fail fast and don't delay call
        performDelay = 0;
        return false;
    }

    std::chrono::milliseconds remainingTimeBeforeRetryAfterInMS = std::chrono::duration_cast<std::chrono::milliseconds>(apiState.retryAfterTime - chrono_clock_t::now());
    if (remainingTimeBeforeRetryAfterInMS.count() <= 0)
    {
        // We are outside the Retry-After window for this endpoint. Clear retry state and make HTTP call without delay
        state->clear_retry_state(retryAfterCacheId);
        performDelay = 0;
        return false;
    }

    std::chrono::seconds timeoutWindow = std::chrono::seconds(timeoutWindowInSeconds);
    chrono_clock_t::time_point timeoutTime = m_performStartTime + timeoutWindow;
    if (!apiState.callPending && apiState.retryAfterTime < timeoutTime)
    {
        // Don't have multiple calls waiting for the Retry-After window for a single endpoint.
        // This causes a flood of calls to the endpoint as soon as the Retry-After windows elapses. If there is already a call
        // pending to this endpoint, fail fast with the cached error code.
        //
        // Otherwise, if the Retry-After will elapse before this call's timeout, delay the call until Retry-After window but don't fail fast
        performDelay = static_cast<uint32_t>(remainingTimeBeforeRetryAfterInMS.count());

        // Update retry cache to indicate this request is pending to the endpoint
        apiState.callPending = true;
        state->set_retry_state(retryAfterCacheId, apiState);

        return false;
    }

    // Fail fast. Set this call's status as the last cached result from the endpoint
    statusCode = apiState.statusCode;
    return true;
}

bool HC_CALL::ShouldRetry(uint32_t& performDelay)
{
    if (!retryAllowed)
    {
        return false;
    }

    if (networkErrorCode == E_HC_NO_NETWORK)
    {
        return false;
    }

    auto responseReceivedTime{ chrono_clock_t::now() };

    if (statusCode == 408 || // Request Timeout
        statusCode == 429 || // Too Many Requests
        statusCode == 500 || // Internal Error
        statusCode == 502 || // Bad Gateway
        statusCode == 503 || // Service Unavailable
        statusCode == 504 || // Gateway Timeout
        networkErrorCode != S_OK)
    {
        // Compute how much time left before hitting the TimeoutWindow setting
        std::chrono::milliseconds timeElapsedSinceFirstCall = std::chrono::duration_cast<std::chrono::milliseconds>(responseReceivedTime - m_performStartTime);
        std::chrono::seconds timeoutWindow = std::chrono::seconds{ timeoutWindowInSeconds };
        std::chrono::milliseconds remainingTimeBeforeTimeout = timeoutWindow - timeElapsedSinceFirstCall;
        if (traceCall)
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::ShouldRetry [ID %llu] remainingTimeBeforeTimeout %lld ms", TO_ULL(id), remainingTimeBeforeTimeout.count());
        }

        // Based on the retry iteration, delay 2,4,8,16,etc seconds by default between retries
        // Jitter the response between the current and next delay based on system clock
        // Max wait time is 1 minute

        double secondsToWaitMin = std::pow(retryDelayInSeconds, m_iterationNumber);
        double secondsToWaitMax = std::pow(retryDelayInSeconds, m_iterationNumber + 1);
        double secondsToWaitDelta = secondsToWaitMax - secondsToWaitMin;
        double lerpScaler = (responseReceivedTime.time_since_epoch().count() % 10000) / 10000.0; // from 0 to 1 based on clock
#if HC_UNITTEST_API
        lerpScaler = 0; // make unit tests deterministic
#endif
        double secondsToWaitUncapped = secondsToWaitMin + secondsToWaitDelta * lerpScaler; // lerp between min & max wait
        double secondsToWait = std::min(secondsToWaitUncapped, (double)MAX_DELAY_TIME_IN_SEC); // cap max wait to 1 min
        std::chrono::milliseconds waitTime = std::chrono::milliseconds(static_cast<int64_t>(secondsToWait * 1000.0));

        auto getRetryAfterHeaderResult = GetRetryAfterHeaderTime();
        if (SUCCEEDED(getRetryAfterHeaderResult.hr))
        {
            // Jitter to spread the load of Retry-After out between the devices trying to retry
            std::chrono::milliseconds retryAfterMin = getRetryAfterHeaderResult.Payload();
            std::chrono::milliseconds retryAfterMax = std::chrono::milliseconds(static_cast<int64_t>(getRetryAfterHeaderResult.Payload().count() * 1.2));
            auto retryAfterDelta = retryAfterMax.count() - retryAfterMin.count();
            std::chrono::milliseconds retryAfterJittered = std::chrono::milliseconds(static_cast<int64_t>(retryAfterMin.count() + retryAfterDelta * lerpScaler)); // lerp between min & max wait

            // If we have a Retry-After header use the max of the jittered Retry-After time and the iteration-based retry backoff
            performDelay = static_cast<uint32_t>(std::max(waitTime.count(), retryAfterJittered.count()));
        }
        else
        {
            performDelay = static_cast<uint32_t>(waitTime.count());
        }

        if (statusCode == 500) // Internal Error
        {
            // For 500 - Internal Error, wait at least 10 seconds before retrying.
            if (performDelay < MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS)
            {
                performDelay = MIN_DELAY_FOR_HTTP_INTERNAL_ERROR_IN_MS;
            }
        }

        if (traceCall)
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "HC_CALL::ShouldRetry [ID %llu] performDelay %lld ms", TO_ULL(id), TO_ULL(performDelay));
        }

        bool shouldRetry{ true };
        if (remainingTimeBeforeTimeout.count() <= MIN_HTTP_TIMEOUT_IN_MS)
        {
            // Need at least 5 seconds to bother making a call
            shouldRetry = false;
        }
        else if (static_cast<uint32_t>(remainingTimeBeforeTimeout.count()) < performDelay + MIN_HTTP_TIMEOUT_IN_MS)
        {
            // Don't bother retrying when out of time
            shouldRetry = false;
        }

        // Remember result if there was an error and there was a Retry-After header
        if (retryAfterCacheId != 0 &&
            SUCCEEDED(getRetryAfterHeaderResult.hr) &&
            statusCode > 400)
        {
            auto retryAfterTime = getRetryAfterHeaderResult.Payload() + responseReceivedTime;
            http_retry_after_api_state state{ retryAfterTime, statusCode, shouldRetry };
            auto httpSingleton = get_http_singleton();
            if (httpSingleton)
            {
                httpSingleton->set_retry_state(retryAfterCacheId, state);
            }
        }

        return shouldRetry;
    }

    return false;
}

Result<std::chrono::seconds> HC_CALL::GetRetryAfterHeaderTime()
{
    auto it = responseHeaders.find(RETRY_AFTER_HEADER);
    if (it != responseHeaders.end())
    {
        int value = 0;
        http_internal_stringstream ss(it->second);
        ss >> value;

        if (!ss.fail())
        {
            return std::chrono::seconds(value);
        }
    }
    return E_FAIL;
}

// Clears response fields between iterations
void HC_CALL::ResetResponseProperties()
{
    responseString.clear();
    responseBodyBytes.clear();
    responseHeaders.clear();
    statusCode = 0;
    networkErrorCode = S_OK;
    platformNetworkErrorCode = 0;
}

bool HeaderCompare::operator()(http_internal_string const& l, http_internal_string const& r) const
{
    return str_icmp(l, r) < 0;
}
