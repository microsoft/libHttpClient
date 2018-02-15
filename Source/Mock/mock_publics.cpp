// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;
using namespace xbox::httpclient::log;


HC_API HC_RESULT HC_CALLING_CONV
HCMockCallCreate(
    _Out_ HC_MOCK_CALL_HANDLE* call
    ) HC_NOEXCEPT
{
    return HCHttpCallCreate(call);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_opt_z_ PCSTR method,
    _In_opt_z_ PCSTR url,
    _In_reads_bytes_opt_(requestBodySize) const PBYTE requestBodyBytes,
    _In_ uint32_t requestBodySize
    ) HC_NOEXCEPT
try 
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    if (method != nullptr && url != nullptr)
    {
        HC_RESULT hr = HCHttpCallRequestSetUrl(call, method, url);
        if (hr != HC_OK)
        {
            return hr;
        }
    }

    if (requestBodyBytes)
    {
        HC_RESULT hr = HCHttpCallRequestSetRequestBodyBytes(call, requestBodyBytes, requestBodySize);
        if (hr != HC_OK)
        {
            return hr;
        }
    }

    std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);
    httpSingleton->m_mocks.push_back(call);
    httpSingleton->m_mocksEnabled = true;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCMockClearMocks() HC_NOEXCEPT
try 
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);

    for (auto& mockCall : httpSingleton->m_mocks)
    {
        HCHttpCallCloseHandle(mockCall);
    }

    httpSingleton->m_mocks.clear();
    httpSingleton->m_mocksEnabled = false;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetResponseString(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_z_ PCSTR responseString
    ) HC_NOEXCEPT
{
    return HCHttpCallResponseSetResponseString(call, responseString);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetStatusCode(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ uint32_t statusCode
    ) HC_NOEXCEPT
{
    return HCHttpCallResponseSetStatusCode(call, statusCode);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetNetworkErrorCode(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ HC_RESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    ) HC_NOEXCEPT
{
    return HCHttpCallResponseSetNetworkErrorCode(call, networkErrorCode, platformNetworkErrorCode);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetHeader(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_z_ PCSTR headerName,
    _In_z_ PCSTR headerValue
    ) HC_NOEXCEPT
{
    return HCHttpCallResponseSetHeader(call, headerName, headerValue);
}

