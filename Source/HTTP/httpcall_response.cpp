// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;


STDAPI 
HCHttpCallResponseGetResponseString(
    _In_ hc_call_handle_t call,
    _Out_ UTF8CSTR* responseString
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

STDAPI 
HCHttpCallResponseSetResponseString(
    _In_ hc_call_handle_t call,
    _In_z_ UTF8CSTR responseString
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr || responseString == nullptr)
    {
        return E_INVALIDARG;
    }

    call->responseString = responseString;
    if (call->traceCall) HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseString [ID %llu]: responseString=%.2048s", call->id, responseString);
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetStatusCode(
    _In_ hc_call_handle_t call,
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

STDAPI 
HCHttpCallResponseSetStatusCode(
    _In_ hc_call_handle_t call,
    _In_ uint32_t statusCode
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return E_INVALIDARG;
    }

    call->statusCode = statusCode;
    if (call->traceCall) HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetStatusCode [ID %llu]: statusCode=%u",
        call->id, statusCode);
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetNetworkErrorCode(
    _In_ hc_call_handle_t call,
    _Out_ HRESULT* networkErrorCode,
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

STDAPI 
HCHttpCallResponseSetNetworkErrorCode(
    _In_ hc_call_handle_t call,
    _In_ HRESULT networkErrorCode,
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
    if (call->traceCall) HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetErrorCode [ID %llu]: errorCode=%08X (%08X)",
        call->id, networkErrorCode, platformNetworkErrorCode);
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCHttpCallResponseGetHeader(
    _In_ hc_call_handle_t call,
    _In_z_ UTF8CSTR headerName,
    _Out_ UTF8CSTR* headerValue
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

STDAPI 
HCHttpCallResponseGetNumHeaders(
    _In_ hc_call_handle_t call,
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

STDAPI 
HCHttpCallResponseGetHeaderAtIndex(
    _In_ hc_call_handle_t call,
    _In_ uint32_t headerIndex,
    _Out_ UTF8CSTR* headerName,
    _Out_ UTF8CSTR* headerValue
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

STDAPI 
HCHttpCallResponseSetHeader(
    _In_ hc_call_handle_t call,
    _In_z_ UTF8CSTR headerName,
    _In_z_ UTF8CSTR headerValue
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
        // Duplicated response header found. We must concatenate it with the existing headers
        http_internal_string& newHeaderValue = it->second;
        newHeaderValue.append(", ");
        newHeaderValue.append(headerValue);

        if (call->traceCall) HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: Duplicated header %s=%s",
            call->id, headerName, newHeaderValue.c_str());
    }
    else
    {
        call->responseHeaders[headerName] = headerValue;
        if (call->traceCall) HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: %s=%s",
            call->id, headerName, headerValue);
    }

    return S_OK;
}
CATCH_RETURN()


