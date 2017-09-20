// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetResponseString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR* responseString
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || responseString == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *responseString = call->responseString.c_str();
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetResponseString(
    _In_ HC_CALL_HANDLE call,
    _In_z_ PCSTR responseString
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || responseString == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    call->responseString = responseString;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseString [ID %llu]: responseString=%.2048s", call->id, responseString);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* statusCode
    )
try 
{
    if (call == nullptr || statusCode == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *statusCode = call->statusCode;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t statusCode
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    call->statusCode = statusCode;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetStatusCode [ID %llu]: statusCode=%u",
        call->id, statusCode);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetNetworkErrorCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ HC_RESULT* networkErrorCode,
    _Out_ uint32_t* platformNetworkErrorCode
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || networkErrorCode == nullptr || platformNetworkErrorCode == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *networkErrorCode = call->networkErrorCode;
    *platformNetworkErrorCode = call->platformNetworkErrorCode;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetNetworkErrorCode(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_RESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    call->networkErrorCode = networkErrorCode;
    call->platformNetworkErrorCode = platformNetworkErrorCode;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetErrorCode [ID %llu]: errorCode=%08X (%08X)",
        call->id, networkErrorCode, platformNetworkErrorCode);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetHeader(
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

    auto it = call->responseHeaders.find(headerName);
    if (it != call->responseHeaders.end())
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
HCHttpCallResponseGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || numHeaders == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(call->responseHeaders.size());
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetHeaderAtIndex(
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
    for (auto it = call->responseHeaders.cbegin(); it != call->responseHeaders.cend(); ++it)
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

// TODO: verify header can be empty string
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetHeader(
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

    call->responseHeaders[headerName] = headerValue;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: %s=%s",
        call->id, headerName, headerValue);
    return HC_OK;
}
CATCH_RETURN()


