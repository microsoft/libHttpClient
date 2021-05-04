// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;

HRESULT CALLBACK DefaultRequestBodyReadFunction(
    _In_ HCCallHandle call,
    _In_ size_t offset,
    _In_ size_t bytesAvailable,
    _Out_writes_bytes_to_(bytesAvailable, *bytesWritten) uint8_t* destination,
    _Out_ size_t* bytesWritten
    ) noexcept
{
    if (call == nullptr || bytesAvailable == 0 || destination == nullptr || bytesWritten == nullptr)
    {
        return E_INVALIDARG;
    }

    uint8_t const* requestBody = nullptr;
    uint32_t requestBodySize = 0;
    HRESULT hr = HCHttpCallRequestGetRequestBodyBytes(call, &requestBody, &requestBodySize);
    if (FAILED(hr) || (requestBody == nullptr && requestBodySize != 0) || offset > requestBodySize)
    {
        return E_FAIL;
    }

    const size_t bytesToWrite = std::min(bytesAvailable, static_cast<size_t>(requestBodySize) - offset);
    std::memcpy(destination, requestBody + offset, bytesToWrite);

    *bytesWritten = bytesToWrite;

    return S_OK;
}

STDAPI 
HCHttpCallRequestSetUrl(
    _In_ HCCallHandle call,
    _In_z_ const char* method,
    _In_z_ const char* url
    ) noexcept
try 
{
    if (call == nullptr || method == nullptr || url == nullptr)
    {
        return E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    call->method = method;
    call->url = url;

    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetUrl [ID %llu]: method=%s url=%s", TO_ULL(call->id), method, url); }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetUrl(
    _In_ HCCallHandle call,
    _Outptr_ const char** method,
    _Outptr_ const char** url
    ) noexcept
try
{
    if (call == nullptr || method == nullptr || url == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    *method = call->method.c_str();
    *url = call->url.c_str();
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestSetRequestBodyBytes(
    _In_ HCCallHandle call,
    _In_reads_bytes_(requestBodySize) const uint8_t* requestBodyBytes,
    _In_ uint32_t requestBodySize
    ) noexcept
try
{
    if (call == nullptr || requestBodyBytes == nullptr || requestBodySize == 0)
    {
        return E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    HRESULT hr = HCHttpCallRequestSetRequestBodyReadFunction(call, DefaultRequestBodyReadFunction, requestBodySize);
    if (FAILED(hr))
    {
        return hr;
    }

    call->requestBodySize = requestBodySize;
    call->requestBodyBytes.assign(requestBodyBytes, requestBodyBytes + requestBodySize);
    call->requestBodyString.clear();

    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRequestBodyBytes [ID %llu]: requestBodySize=%lu", TO_ULL(call->id), static_cast<unsigned long>(requestBodySize)); }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestSetRequestBodyString(
    _In_ HCCallHandle call,
    _In_z_ const char* requestBodyString
) noexcept
{
    if (requestBodyString == nullptr)
    {
        return E_INVALIDARG;
    }

    return HCHttpCallRequestSetRequestBodyBytes(
        call,
        reinterpret_cast<uint8_t const*>(requestBodyString),
        static_cast<uint32_t>(strlen(requestBodyString))
    );
}

STDAPI
HCHttpCallRequestSetRequestBodyReadFunction(
    _In_ HCCallHandle call,
    _In_ HCHttpCallRequestBodyReadFunction readFunction,
    _In_ size_t bodySize
) noexcept
try
{
    if (call == nullptr || readFunction == nullptr)
    {
        return E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    call->requestBodyReadFunction = readFunction;
    call->requestBodySize = bodySize;

    call->requestBodyString.clear();
    call->requestBodyBytes.clear();
    call->requestBodyBytes.shrink_to_fit();

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetRequestBodyBytes(
    _In_ HCCallHandle call,
    _Outptr_result_bytebuffer_maybenull_(*requestBodySize) const uint8_t** requestBodyBytes,
    _Out_ uint32_t* requestBodySize
    ) noexcept
try 
{
    if (call == nullptr || requestBodyBytes == nullptr || requestBodySize == nullptr)
    {
        return E_INVALIDARG;
    }

    *requestBodySize = static_cast<uint32_t>(call->requestBodySize);
    if (*requestBodySize == 0)
    {
        *requestBodyBytes = nullptr;
    }
    else
    {
        *requestBodyBytes = call->requestBodyBytes.data();
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetRequestBodyString(
    _In_ HCCallHandle call,
    _Outptr_ const char** requestBody
) noexcept
try
{
    if (call == nullptr || requestBody == nullptr)
    {
        return E_INVALIDARG;
    }

    if (call->requestBodyString.empty())
    {
        call->requestBodyString = http_internal_string(reinterpret_cast<char const*>(call->requestBodyBytes.data()), call->requestBodyBytes.size());
    }
    *requestBody = call->requestBodyString.c_str();
    return S_OK;
}
CATCH_RETURN()

STDAPI
HCHttpCallRequestGetRequestBodyReadFunction(
    _In_ HCCallHandle call,
    _Out_ HCHttpCallRequestBodyReadFunction* readFunction,
    _Out_ size_t* requestBodySize
    ) noexcept
try
{
    if (call == nullptr || readFunction == nullptr || requestBodySize == nullptr)
    {
        return E_INVALIDARG;
    }

    *readFunction = call->requestBodyReadFunction;
    *requestBodySize = call->requestBodySize;

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestSetHeader(
    _In_ HCCallHandle call,
    _In_z_ const char* headerName,
    _In_z_ const char* headerValue,
    _In_ bool allowTracing
    ) noexcept
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    call->requestHeaders[headerName] = headerValue;

    if (allowTracing && call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetHeader [ID %llu]: %s=%s", TO_ULL(call->id), headerName, headerValue); }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetHeader(
    _In_ HCCallHandle call,
    _In_z_ const char* headerName,
    _Out_ const char** headerValue
    ) noexcept
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    auto it = call->requestHeaders.find(headerName);
    if (it != call->requestHeaders.end())
    {
        *headerValue = it->second.c_str();
    }
    else
    {
        *headerValue = nullptr;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetNumHeaders(
    _In_ HCCallHandle call,
    _Out_ uint32_t* numHeaders
    ) noexcept
try
{
    if (call == nullptr || numHeaders == nullptr)
    {
        return E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(call->requestHeaders.size());
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetHeaderAtIndex(
    _In_ HCCallHandle call,
    _In_ uint32_t headerIndex,
    _Out_ const char** headerName,
    _Out_ const char** headerValue
    ) noexcept
try
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    uint32_t index = 0;
    for (auto it = call->requestHeaders.cbegin(); it != call->requestHeaders.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return S_OK;
        }

        index++;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestSetRetryCacheId(
    _In_opt_ HCCallHandle call,
    _In_ uint32_t retryAfterCacheId
    ) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->retryAfterCacheId = retryAfterCacheId;

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryCacheId [ID %llu]: retryAfterCacheId=%d", TO_ULL(call->id), retryAfterCacheId); }
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestSetRetryAllowed(
    _In_opt_ HCCallHandle call,
    _In_ bool retryAllowed
    ) noexcept
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        httpSingleton->m_retryAllowed = retryAllowed;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->retryAllowed = retryAllowed;

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryAllowed [ID %llu]: retryAllowed=%s", TO_ULL(call->id), retryAllowed ? "true" : "false"); }
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetRetryAllowed(
    _In_opt_ HCCallHandle call,
    _Out_ bool* retryAllowed
    ) noexcept
try
{
    if (retryAllowed == nullptr)
    {
        return E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        *retryAllowed = httpSingleton->m_retryAllowed;
    }
    else
    {
        *retryAllowed = call->retryAllowed;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetRetryCacheId(
    _In_ HCCallHandle call,
    _Out_ uint32_t* retryAfterCacheId
    ) noexcept
try
{
    if (call == nullptr || retryAfterCacheId == nullptr)
    {
        return E_INVALIDARG;
    }
    else
    {
        *retryAfterCacheId = call->retryAfterCacheId;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestSetTimeout(
    _In_opt_ HCCallHandle call,
    _In_ uint32_t timeoutInSeconds
    ) noexcept
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        httpSingleton->m_timeoutInSeconds = timeoutInSeconds;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->timeoutInSeconds = timeoutInSeconds;

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetTimeout [ID %llu]: timeoutInSeconds=%u", TO_ULL(call->id), timeoutInSeconds); }
    }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetTimeout(
    _In_opt_ HCCallHandle call,
    _Out_ uint32_t* timeoutInSeconds
    ) noexcept
try
{
    if (timeoutInSeconds == nullptr)
    {
        return E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        *timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
    }
    else
    {
        *timeoutInSeconds = call->timeoutInSeconds;
    }
    return S_OK;
}
CATCH_RETURN()


STDAPI 
HCHttpCallRequestSetTimeoutWindow(
    _In_opt_ HCCallHandle call,
    _In_ uint32_t timeoutWindowInSeconds
    ) noexcept
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        httpSingleton->m_timeoutWindowInSeconds = timeoutWindowInSeconds;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->timeoutWindowInSeconds = timeoutWindowInSeconds;
    }

    if (call == nullptr || call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestTimeoutWindow: %u", timeoutWindowInSeconds); }

    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetTimeoutWindow(
    _In_opt_ HCCallHandle call,
    _Out_ uint32_t* timeoutWindowInSeconds
    ) noexcept
try
{
    if (timeoutWindowInSeconds == nullptr)
    {
        return E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        *timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
    }
    else
    {
        *timeoutWindowInSeconds = call->timeoutWindowInSeconds;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestGetRetryDelay(
    _In_opt_ HCCallHandle call,
    _In_ uint32_t* retryDelayInSeconds
    ) noexcept
try
{
     if (retryDelayInSeconds == nullptr)
    {
        return E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        *retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;
    }
    else
    {
        *retryDelayInSeconds = call->retryDelayInSeconds;
    }
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallRequestSetRetryDelay(
    _In_opt_ HCCallHandle call,
    _In_ uint32_t retryDelayInSeconds
    ) noexcept
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        httpSingleton->m_retryDelayInSeconds = retryDelayInSeconds;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->retryDelayInSeconds = retryDelayInSeconds;
    }
    return S_OK;
}
CATCH_RETURN()

#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
STDAPI
HCHttpCallRequestSetSSLValidation(
    _In_ HCCallHandle call,
    _In_ bool sslValidation
) noexcept
try
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->sslValidation = sslValidation;

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetSSLValidation [ID %llu]: sslValidation=%s", TO_ULL(call->id), sslValidation ? "true" : "false"); }
    }
    return S_OK;
}
CATCH_RETURN()
#endif
