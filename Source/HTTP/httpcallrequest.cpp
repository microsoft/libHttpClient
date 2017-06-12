// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "mem.h"
#include "singleton.h"
#include "log.h"
#include "httpcall.h"


HC_API void HC_CALLING_CONV
HCHttpCallRequestSetUrl(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T method,
    _In_ PCSTR_T url,
    _In_opt_ PCSTR_T requestBody
    )
{
    VerifyGlobalInit();
    call->method = method;
    call->url = url;
    call->requestBody = requestBody;
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetUrl(
    _In_ HC_CALL_HANDLE call,
    _Outptr_ PCSTR_T* method,
    _Outptr_ PCSTR_T* url,
    _Outptr_opt_ PCSTR_T* requestBody
    )
{
    VerifyGlobalInit();
    *method = call->method.c_str();
    *url = call->url.c_str();
    *requestBody = call->requestBody.c_str();
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _In_ PCSTR_T headerValue
    )
{
    VerifyGlobalInit();
    call->requestHeaders[headerName] = headerValue;
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _Out_ PCSTR_T* headerValue
    )
{
    VerifyGlobalInit();
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
    VerifyGlobalInit();
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
    VerifyGlobalInit();

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
    VerifyGlobalInit();
    call->retryAllowed = retryAllowed;
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetRetryAllowed(
    _In_ HC_CALL_HANDLE call,
    _Out_ bool* retryAllowed
    )
{
    VerifyGlobalInit();
    *retryAllowed = call->retryAllowed;
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestSetTimeout(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t timeoutInSeconds
    )
{
    VerifyGlobalInit();
    call->timeoutInSeconds = timeoutInSeconds;
}

HC_API void HC_CALLING_CONV
HCHttpCallRequestGetTimeout(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* timeoutInSeconds
    )
{
    VerifyGlobalInit();
    *timeoutInSeconds = call->timeoutInSeconds;
}


