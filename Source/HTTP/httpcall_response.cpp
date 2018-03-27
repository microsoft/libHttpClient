// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;


HCAPI 
HCHttpCallResponseGetResponseString(
    _In_ hc_call_handle call,
    _Out_ const_utf8_string* responseString
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || responseString == nullptr)
    {
        return E_INVALIDARG;
    }

    *responseString = call->responseString.c_str();
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseSetResponseString(
    _In_ hc_call_handle call,
    _In_z_ const_utf8_string responseString
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || responseString == nullptr)
    {
        return E_INVALIDARG;
    }

    call->responseString = responseString;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseString [ID %llu]: responseString=%.2048s", call->id, responseString);
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseGetStatusCode(
    _In_ hc_call_handle call,
    _Out_ uint32_t* statusCode
    )
try 
{
    if (call == nullptr || statusCode == nullptr)
    {
        return E_INVALIDARG;
    }

    *statusCode = call->statusCode;
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseSetStatusCode(
    _In_ hc_call_handle call,
    _In_ uint32_t statusCode
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    call->statusCode = statusCode;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetStatusCode [ID %llu]: statusCode=%u",
        call->id, statusCode);
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseGetNetworkErrorCode(
    _In_ hc_call_handle call,
    _Out_ hresult_t* networkErrorCode,
    _Out_ uint32_t* platformNetworkErrorCode
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || networkErrorCode == nullptr || platformNetworkErrorCode == nullptr)
    {
        return E_INVALIDARG;
    }

    *networkErrorCode = call->networkErrorCode;
    *platformNetworkErrorCode = call->platformNetworkErrorCode;
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseSetNetworkErrorCode(
    _In_ hc_call_handle call,
    _In_ hresult_t networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    call->networkErrorCode = networkErrorCode;
    call->platformNetworkErrorCode = platformNetworkErrorCode;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetErrorCode [ID %llu]: errorCode=%08X (%08X)",
        call->id, networkErrorCode, platformNetworkErrorCode);
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseGetHeader(
    _In_ hc_call_handle call,
    _In_z_ const_utf8_string headerName,
    _Out_ const_utf8_string* headerValue
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
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
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseGetNumHeaders(
    _In_ hc_call_handle call,
    _Out_ uint32_t* numHeaders
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || numHeaders == nullptr)
    {
        return E_INVALIDARG;
    }

    *numHeaders = static_cast<uint32_t>(call->responseHeaders.size());
    return S_OK;
}
CATCH_RETURN()

HCAPI 
HCHttpCallResponseGetHeaderAtIndex(
    _In_ hc_call_handle call,
    _In_ uint32_t headerIndex,
    _Out_ const_utf8_string* headerName,
    _Out_ const_utf8_string* headerValue
    ) HC_NOEXCEPT
try
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    uint32_t index = 0;
    for (auto it = call->responseHeaders.cbegin(); it != call->responseHeaders.cend(); ++it)
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

HCAPI 
HCHttpCallResponseSetHeader(
    _In_ hc_call_handle call,
    _In_z_ const_utf8_string headerName,
    _In_z_ const_utf8_string headerValue
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return E_INVALIDARG;
    }

    call->responseHeaders[headerName] = headerValue;
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: %s=%s",
        call->id, headerName, headerValue);
    return S_OK;
}
CATCH_RETURN()


