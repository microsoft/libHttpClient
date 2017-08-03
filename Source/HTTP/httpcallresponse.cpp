// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;


HC_API void HC_CALLING_CONV
HCHttpCallResponseGetResponseString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR_T* responseString
    )
{
    verify_http_singleton();
    *responseString = call->responseString.c_str();
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseSetResponseString(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T responseString
    )
{
    verify_http_singleton();
    call->responseString = responseString;
#if ENABLE_LOGS
    LOGS_INFO << "HCHttpCallResponseSetResponseString [ID " << call->id << "]: responseString:" << responseString;
#endif
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseGetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* statusCode
    )
{
    verify_http_singleton();
    *statusCode = call->statusCode;
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseSetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t statusCode
    )
{
    verify_http_singleton();
    call->statusCode = statusCode;
#if ENABLE_LOGS
    LOGS_INFO << "HCHttpCallResponseSetStatusCode [ID " << call->id << "]: statusCode=" << statusCode;
#endif
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseGetErrorCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* errorCode
    )
{
    verify_http_singleton();
    *errorCode = call->errorCode;
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseSetErrorCode(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t errorCode
    )
{
    verify_http_singleton();
    call->errorCode = errorCode;
#if ENABLE_LOGS
    LOGS_INFO << "HCHttpCallResponseSetErrorCode [ID " << call->id << "]: errorCode=" << errorCode;
#endif
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseGetErrorMessage(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR_T* errorMessage
    )
{
    verify_http_singleton();
    *errorMessage = call->errorMessage.c_str();
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseSetErrorMessage(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T errorMessage
    )
{
    verify_http_singleton();
    call->errorMessage = errorMessage;
#if ENABLE_LOGS
    LOGS_INFO << "HCHttpCallResponseSetErrorMessage [ID " << call->id << "]: errorMessage=" << errorMessage;
#endif
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _Out_ PCSTR_T* headerValue
    )
{
    verify_http_singleton();
    auto it = call->responseHeaders.find(headerName);
    if (it != call->responseHeaders.end())
    {
        *headerValue = it->second.c_str();
    }
    else
    {
        *headerValue = nullptr;
    }
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    )
{
    verify_http_singleton();
    *numHeaders = static_cast<uint32_t>(call->responseHeaders.size());
}

HC_API void HC_CALLING_CONV
HCHttpCallResponseGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR_T* headerName,
    _Out_ PCSTR_T* headerValue
    )
{
    verify_http_singleton();

    uint32_t index = 0;
    for (auto it = call->responseHeaders.cbegin(); it != call->responseHeaders.cend(); ++it)
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
HCHttpCallResponseSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR_T headerName,
    _Out_ PCSTR_T headerValue
    )
{
    call->responseHeaders[headerName] = headerValue;
#if ENABLE_LOGS
    LOGS_INFO << "HCHttpCallResponseSetHeader [ID " << call->id << "]: " << headerName << "=" << headerValue;
#endif
}


