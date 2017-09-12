// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;

HC_API void HC_CALLING_CONV
HCHttpCallRequestSetUrl(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T method,
    _In_ PCSTR_T url
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);

    call->method = method;
    call->url = url;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetUrl [ID %llu]: method=%s url=%s",
        call->id, method, url);
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetUrl(
    _In_ HC_CALL_HANDLE call,
    _Outptr_ PCSTR_T* method,
    _Outptr_ PCSTR_T* url
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);

    *method = call->method.c_str();
    *url = call->url.c_str();
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestSetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T requestBodyString
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);

    call->requestBodyString = requestBodyString;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetBodyString [ID %llu]: requestBodyString=%s",
        call->id, requestBodyString);
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRequestBodyString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR_T* requestBodyString
    )
{
    *requestBodyString = call->requestBodyString.c_str();
}


HC_API void HC_CALLING_CONV
HCHttpCallRequestSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _In_ PCSTR_T headerValue
    )
{
    call->requestHeaders[headerName] = headerValue;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetHeader [ID %llu]: %s=%s",
        call->id, headerName, headerValue);
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _Out_ PCSTR_T* headerValue
    )
{
    auto it = call->requestHeaders.find(headerName);
    if (it != call->requestHeaders.end())
    {
        *headerValue = it->second.c_str();
    }
    else
    {
        *headerValue = nullptr;
    }
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    )
{
    *numHeaders = static_cast<uint32_t>(call->requestHeaders.size());
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR_T* headerName,
    _Out_ PCSTR_T* headerValue
    )
{
    uint32_t index = 0;
    for (auto it = call->requestHeaders.cbegin(); it != call->requestHeaders.cend(); ++it)
    {
        if (index == headerIndex)
        {
            *headerName = it->first.c_str();
            *headerValue = it->second.c_str();
            return;
        }

        index++;
    }

    *headerName = nullptr;
    *headerValue = nullptr;
    return;
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestSetRetryAllowed(
    _In_ HC_CALL_HANDLE call,
    _In_ bool retryAllowed
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);
    call->retryAllowed = retryAllowed;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetRetryAllowed [ID %llu]: retryAllowed=%s",
        call->id, retryAllowed ? _T("true") : _T("false"));
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRetryAllowed(
    _In_ HC_CALL_HANDLE call,
    _Out_ bool* retryAllowed
    )
{
    *retryAllowed = call->retryAllowed;
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestSetTimeout(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutInSeconds
    )
{
    call->timeoutInSeconds = timeoutInSeconds;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallRequestSetTimeout [ID %llu]: timeoutInSeconds=%u",
        call->id, timeoutInSeconds);
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetTimeout(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutInSeconds
    )
{
    *timeoutInSeconds = call->timeoutInSeconds;
}


