// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;
using namespace xbox::httpclient::log;


HC_API HC_RESULT HC_CALLING_CONV
HCMockCallCreate(
    _Out_ HC_MOCK_CALL_HANDLE* call
    )
{
    return HCHttpCallCreate(call);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_opt_ PCSTR method,
    _In_opt_ PCSTR url,
    _In_opt_ PCSTR requestBodyString
    )
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();

        if (method != nullptr && url != nullptr)
        {
            HC_RESULT hr = HCHttpCallRequestSetUrl(call, method, url);
            if (hr != HC_OK)
            {
                return hr;
            }
        }

        if (requestBodyString)
        {
            HC_RESULT hr = HCHttpCallRequestSetRequestBodyString(call, requestBodyString);
            if (hr != HC_OK)
            {
                return hr;
            }
        }

        std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);
        httpSingleton->m_mocks.push_back(call);
        httpSingleton->m_mocksEnabled = true;
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockClearMocks()
{
    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();

        std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);

        for (auto& mockCall : httpSingleton->m_mocks)
        {
            HCHttpCallCleanup(mockCall);
        }

        httpSingleton->m_mocks.clear();
        httpSingleton->m_mocksEnabled = false;
    );
}



HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetResponseString(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ PCSTR responseString
    )
{
    return HCHttpCallResponseSetResponseString(call, responseString);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetStatusCode(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ uint32_t statusCode
    )
{
    return HCHttpCallResponseSetStatusCode(call, statusCode);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetNetworkErrorCode(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ HC_RESULT networkErrorCode,
    _In_ uint32_t platformNetworkErrorCode
    )
{
    return HCHttpCallResponseSetNetworkErrorCode(call, networkErrorCode, platformNetworkErrorCode);
}

HC_API HC_RESULT HC_CALLING_CONV
HCMockResponseSetHeader(
    _In_ HC_MOCK_CALL_HANDLE call,
    _In_ PCSTR headerName,
    _In_ PCSTR headerValue
    )
{
    return HCHttpCallResponseSetHeader(call, headerName, headerValue);
}

