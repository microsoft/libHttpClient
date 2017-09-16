// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"

using namespace xbox::httpclient;


HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetResponseString(
    _In_ HC_CALL_HANDLE call,
    _Out_ PCSTR* responseString
    )
{
    if (call == nullptr || responseString == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        *responseString = call->responseString.c_str();
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetResponseString(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR responseString
    )
{
    if (call == nullptr || responseString == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        call->responseString = responseString;
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseString [ID %llu]: responseString=%.2048s", call->id, responseString);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* statusCode
    )
{
    if (call == nullptr || statusCode == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        *statusCode = call->statusCode;
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetStatusCode(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t statusCode
    )
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        call->statusCode = statusCode;
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetStatusCode [ID %llu]: statusCode=%u",
            call->id, statusCode);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetNetworkErrorCode(
    _In_ HC_CALL_HANDLE call,
    _Out_ HC_RESULT* networkErrorCode,
    _Out_ uint32_t* platformNetworkErrorCode
    )
{
    if (call == nullptr || networkErrorCode == nullptr || platformNetworkErrorCode == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        *networkErrorCode = call->networkErrorCode;
        *platformNetworkErrorCode = call->platformNetworkErrorCode;
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetNetworkErrorCode(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_RESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    )
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        call->networkErrorCode = networkErrorCode;
        call->platformNetworkErrorCode = platformNetworkErrorCode;
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetErrorCode [ID %llu]: errorCode=%08X (%08X)",
            call->id, networkErrorCode, platformNetworkErrorCode);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _Out_ PCSTR* headerValue
    )
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        auto it = call->responseHeaders.find(headerName);
        if (it != call->responseHeaders.end())
        {
            *headerValue = it->second.c_str();
        }
        else
        {
            *headerValue = nullptr;
        }
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetNumHeaders(
    _In_ HC_CALL_HANDLE call,
    _Out_ uint32_t* numHeaders
    )
{
    if (call == nullptr || numHeaders == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        *numHeaders = static_cast<uint32_t>(call->responseHeaders.size());
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseGetHeaderAtIndex(
    _In_ HC_CALL_HANDLE call,
    _In_ uint32_t headerIndex,
    _Out_ PCSTR* headerName,
    _Out_ PCSTR* headerValue
    )
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
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
    );
}

// TODO: verify header can be empty string
HC_API HC_RESULT HC_CALLING_CONV
HCHttpCallResponseSetHeader(
    _In_ HC_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _In_ PCSTR headerValue
    )
{
    if (call == nullptr || headerName == nullptr || headerValue == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        call->responseHeaders[headerName] = headerValue;
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallResponseSetResponseHeader [ID %llu]: %s=%s",
            call->id, headerName, headerValue);
    );
}


