// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;

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

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    call->method = method;
    call->url = url;

    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetUrl [ID %llu]: method=%s url=%s", call->id, method, url); }

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

    auto httpSingleton = get_http_singleton(true);
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

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    call->requestBodyBytes.assign(requestBodyBytes, requestBodyBytes + requestBodySize);
    call->requestBodyString.clear();

    if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRequestBodyBytes [ID %llu]: requestBodySize=%lu", call->id, requestBodySize); }
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

    *requestBodySize = static_cast<uint32_t>(call->requestBodyBytes.size());
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

    if (allowTracing && call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetHeader [ID %llu]: %s=%s", call->id, headerName, headerValue); }
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

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryCacheId [ID %llu]: retryAfterCacheId=%d", call->id, retryAfterCacheId); }
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
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        httpSingleton->m_retryAllowed = retryAllowed;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->retryAllowed = retryAllowed;

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryAllowed [ID %llu]: retryAllowed=%s", call->id, retryAllowed ? "true" : "false"); }
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
        auto httpSingleton = get_http_singleton(true);
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
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return E_HC_NOT_INITIALISED;

        httpSingleton->m_timeoutInSeconds = timeoutInSeconds;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->timeoutInSeconds = timeoutInSeconds;

        if (call->traceCall) { HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetTimeout [ID %llu]: timeoutInSeconds=%u", call->id, timeoutInSeconds); }
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
        auto httpSingleton = get_http_singleton(true);
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
        auto httpSingleton = get_http_singleton(true);
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
        auto httpSingleton = get_http_singleton(true);
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
        auto httpSingleton = get_http_singleton(true);
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
        auto httpSingleton = get_http_singleton(true);
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
