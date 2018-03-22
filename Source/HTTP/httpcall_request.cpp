// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetUrl(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR method,
    _In_z_ PCSTR url
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || method == nullptr || url == nullptr)
    {
        return HC_E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    call->method = method;
    call->url = url;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetUrl [ID %llu]: method=%s url=%s",
        call->id, method, url);

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetUrl(
    _In_ HC_CALL_HANDLE call,
    _Outptr_ PCSTR* method,
    _Outptr_ PCSTR* url
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || method == nullptr || url == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    *method = call->method.c_str();
    *url = call->url.c_str();
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyBytes(
    _In_ HC_CALL_HANDLE call,
    _In_reads_bytes_(requestBodySize) const BYTE* requestBodyBytes,
    _In_ uint32_t requestBodySize
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || requestBodyBytes == nullptr || requestBodySize == 0)
    {
        return HC_E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    call->requestBodyBytes.assign(requestBodyBytes, requestBodyBytes + requestBodySize);
    call->requestBodyString.clear();

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRequestBodyBytes [ID %llu]: requestBodySize=%lu",
        call->id, requestBodySize);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR requestBodyString
) HC_NOEXCEPT
{
    if (requestBodyString == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    return HCHttpCallRequestSetRequestBodyBytes(
        call,
        reinterpret_cast<uint8_t const*>(requestBodyString),
        static_cast<uint32_t>(strlen(requestBodyString))
    );
}


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRequestBodyBytes(
    _In_ HC_CALL_HANDLE call,
    _Outptr_result_bytebuffer_maybenull_(*requestBodySize) const BYTE** requestBodyBytes,
    _Out_ uint32_t* requestBodySize
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || requestBodyBytes == nullptr || requestBodySize == nullptr)
    {
        return HC_E_INVALIDARG;
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

    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _Outptr_ PCSTR* requestBody
) HC_NOEXCEPT
try
{
    if (call == nullptr || requestBody == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    if (call->requestBodyString.empty())
    {
        call->requestBodyString = http_internal_string(reinterpret_cast<char const*>(call->requestBodyBytes.data()), call->requestBodyBytes.size());
    }
    *requestBody = call->requestBodyString.c_str();
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR headerName,
    _In_z_ PCSTR headerValue
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }
    RETURN_IF_PERFORM_CALLED(call);

    call->requestHeaders[headerName] = headerValue;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetHeader [ID %llu]: %s=%s",
        call->id, headerName, headerValue);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR headerName,
    _Out_ PCSTR* headerValue
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
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
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || numHeaders == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(call->requestHeaders.size());
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR* headerName,
    _Out_ PCSTR* headerValue
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    uint32_t index = 0;
    for (auto it = call->requestHeaders.cbegin(); it != call->requestHeaders.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return HC_OK;
        }

        index++;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRetryCacheId(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t retryAfterCacheId
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->retryAfterCacheId = retryAfterCacheId;

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryCacheId [ID %llu]: retryAfterCacheId=%d",
            call->id, retryAfterCacheId);
    }
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRetryAllowed(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ bool retryAllowed
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        httpSingleton->m_retryAllowed = retryAllowed;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->retryAllowed = retryAllowed;

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryAllowed [ID %llu]: retryAllowed=%s",
            call->id, retryAllowed ? "true" : "false");
    }
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRetryAllowed(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ bool* retryAllowed
    ) HC_NOEXCEPT
try
{
    if (retryAllowed == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        *retryAllowed = httpSingleton->m_retryAllowed;
    }
    else
    {
        *retryAllowed = call->retryAllowed;
    }
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRetryCacheId(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* retryAfterCacheId
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || retryAfterCacheId == nullptr)
    {
        return HC_E_INVALIDARG;
    }
    else
    {
        *retryAfterCacheId = call->retryAfterCacheId;
    }
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetTimeout(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutInSeconds
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        httpSingleton->m_timeoutInSeconds = timeoutInSeconds;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->timeoutInSeconds = timeoutInSeconds;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetTimeout [ID %llu]: timeoutInSeconds=%u",
        call->id, timeoutInSeconds);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetTimeout(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutInSeconds
    ) HC_NOEXCEPT
try
{
    if (timeoutInSeconds == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        *timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
    }
    else
    {
        *timeoutInSeconds = call->timeoutInSeconds;
    }
    return HC_OK;
}
CATCH_RETURN()


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetTimeoutWindow(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutWindowInSeconds
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        httpSingleton->m_timeoutWindowInSeconds = timeoutWindowInSeconds;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->timeoutWindowInSeconds = timeoutWindowInSeconds;
    }

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestTimeoutWindow: %u", timeoutWindowInSeconds);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetTimeoutWindow(
    _In_opt_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutWindowInSeconds
    ) HC_NOEXCEPT
try
{
    if (timeoutWindowInSeconds == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        *timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
    }
    else
    {
        *timeoutWindowInSeconds = call->timeoutWindowInSeconds;
    }
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestGetRetryDelay(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t* retryDelayInSeconds
    ) HC_NOEXCEPT
try
{
    if (retryDelayInSeconds == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        *retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;
    }
    else
    {
        *retryDelayInSeconds = call->retryDelayInSeconds;
    }
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallRequestSetRetryDelay(
    _In_opt_ HC_CALL_HANDLE call,
    _In_ uint32_t retryDelayInSeconds
    ) HC_NOEXCEPT
try
{
    if (call == nullptr)
    {
        auto httpSingleton = get_http_singleton(true);
        if (nullptr == httpSingleton)
            return HC_E_NOTINITIALISED;

        httpSingleton->m_retryDelayInSeconds = retryDelayInSeconds;
    }
    else
    {
        RETURN_IF_PERFORM_CALLED(call);
        call->retryDelayInSeconds = retryDelayInSeconds;
    }
    return HC_OK;
}
CATCH_RETURN()
