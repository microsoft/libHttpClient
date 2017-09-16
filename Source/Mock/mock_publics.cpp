// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;
using namespace xbox::httpclient::log;


HC_API HC_RESULT HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_CALL_HANDLE call
    )
{
    if (call == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();

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

