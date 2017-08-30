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
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseString [ID %llu]: responseString=%ws",
        call->id, responseString);
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
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetStatusCode [ID %llu]: statusCode=%u",
        call->id, statusCode);
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
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetErrorCode [ID %llu]: errorCode=%08X",
        call->id, errorCode);
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
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetErrorMessage [ID %llu]: errorMessage=%ws",
        call->id, errorMessage);
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
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: %ws=%ws",
        call->id, headerName, headerValue);
}


