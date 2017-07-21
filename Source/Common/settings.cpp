// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

HC_API void HC_CALLING_CONV
HCSettingsSetLogLevel(
    _In_ HC_LOG_LEVEL traceLevel
    )
{
    VerifyGlobalInit();
    get_http_singleton()->m_traceLevel = traceLevel;
}

HC_API void HC_CALLING_CONV
HCSettingsGetLogLevel(
    _Out_ HC_LOG_LEVEL* traceLevel
    )
{
    VerifyGlobalInit();
    *traceLevel = get_http_singleton()->m_traceLevel;
}

HC_API void HC_CALLING_CONV
HCSettingsSetTimeoutWindow(
    _In_ uint32_t timeoutWindowInSeconds
    )
{
    VerifyGlobalInit();
    get_http_singleton()->m_timeoutWindowInSeconds = timeoutWindowInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsGetTimeoutWindow(
    _Out_ uint32_t* timeoutWindowInSeconds
    )
{
    VerifyGlobalInit();
    *timeoutWindowInSeconds = get_http_singleton()->m_timeoutWindowInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsSetAssertsForThrottling(
    _In_ bool enableAssertsForThrottling
    )
{
    VerifyGlobalInit();
    get_http_singleton()->m_enableAssertsForThrottling = enableAssertsForThrottling;
}

HC_API void HC_CALLING_CONV
HCSettingsGetAssertsForThrottling(
    _Out_ bool* enableAssertsForThrottling
    )
{
    VerifyGlobalInit();
    *enableAssertsForThrottling = get_http_singleton()->m_enableAssertsForThrottling;
}

HC_API void HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_CALL_HANDLE call
    )
{
    VerifyGlobalInit();

    std::lock_guard<std::mutex> guard(get_http_singleton()->m_mocksLock);
    get_http_singleton()->m_mocks.push_back(call);
    get_http_singleton()->m_mocksEnabled = true;
}

HC_API void HC_CALLING_CONV
HCMockClearMocks()
{
    VerifyGlobalInit();

    std::lock_guard<std::mutex> guard(get_http_singleton()->m_mocksLock);

    for (auto& mockCall : get_http_singleton()->m_mocks)
    {
        HCHttpCallCleanup(mockCall);
    }

    get_http_singleton()->m_mocks.clear();
    get_http_singleton()->m_mocksEnabled = false;
}

