// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

struct http_header_compare
{
    bool operator()(http_internal_string const& l, http_internal_string const& r) const;
};

using http_header_map = http_internal_map<http_internal_string, http_internal_string, http_header_compare>;

HRESULT CALLBACK DefaultRequestBodyReadFunction(
    _In_ HCCallHandle call,
    _In_ size_t offset,
    _In_ size_t bytesAvailable,
    _In_opt_ void* context,
    _Out_writes_bytes_to_(bytesAvailable, *bytesWritten) uint8_t* destination,
    _Out_ size_t* bytesWritten
    ) noexcept;

HRESULT CALLBACK DefaultResponseBodyWriteFunction(
    _In_ HCCallHandle call,
    _In_reads_bytes_(bytesAvailable) const uint8_t* source,
    _In_ size_t bytesAvailable,
    _In_opt_ void* context
    ) noexcept;

struct HC_CALL
{
    HC_CALL()
    {
        refCount = 1;
    }
    virtual ~HC_CALL();

    http_internal_string method;
    http_internal_string url;
    http_internal_vector<uint8_t> requestBodyBytes;
    http_internal_string requestBodyString;
    size_t requestBodySize = 0;
    HCHttpCallRequestBodyReadFunction requestBodyReadFunction = DefaultRequestBodyReadFunction;
    void* requestBodyReadFunctionContext = nullptr;
    http_header_map requestHeaders;

    http_internal_string responseString;
    http_internal_vector<uint8_t> responseBodyBytes;
    HCHttpCallResponseBodyWriteFunction responseBodyWriteFunction = DefaultResponseBodyWriteFunction;
    void* responseBodyWriteFunctionContext = nullptr;
    http_header_map responseHeaders;
    uint32_t statusCode = 0;
    HRESULT networkErrorCode = S_OK;
    uint32_t platformNetworkErrorCode = 0;
    http_internal_string platformNetworkErrorMessage;
    std::shared_ptr<xbox::httpclient::hc_task> task;

    uint64_t id = 0;
    bool traceCall = true;
#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
    bool sslValidation = true;
#endif
    void* context = nullptr;
    std::atomic<int> refCount;

    chrono_clock_t::time_point firstRequestStartTime;
    std::chrono::milliseconds delayBeforeRetry = std::chrono::milliseconds(0);
    uint32_t retryIterationNumber = 0;
    bool retryAllowed = false;
    uint32_t retryAfterCacheId = 0;
    uint32_t timeoutInSeconds = 0;
    uint32_t timeoutWindowInSeconds = 0;
    uint32_t retryDelayInSeconds = 0;
    bool performCalled = false;
};

struct HttpPerformInfo
{
    HttpPerformInfo(_In_ HCCallPerformFunction h, _In_opt_ void* ctx)
        : handler(h), context(ctx)
    { }
    HCCallPerformFunction handler = nullptr;
    void* context = nullptr; // non owning
};
