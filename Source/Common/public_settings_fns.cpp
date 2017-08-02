// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;

HC_API void HC_CALLING_CONV
HCSettingsSetLogLevel(
    _In_ HC_LOG_LEVEL traceLevel
    )
{
    verify_http_singleton();
    get_http_singleton()->m_logger->set_log_level(traceLevel);

    LOGS_INFO << L"HCSettingsSetLogLevel: " << traceLevel;
}

HC_API void HC_CALLING_CONV
HCSettingsGetLogLevel(
    _Out_ HC_LOG_LEVEL* traceLevel
    )
{
    verify_http_singleton();
    *traceLevel = get_http_singleton()->m_logger->get_log_level();
}

HC_API void HC_CALLING_CONV
HCSettingsSetTimeoutWindow(
    _In_ uint32_t timeoutWindowInSeconds
    )
{
    verify_http_singleton();
    get_http_singleton()->m_timeoutWindowInSeconds = timeoutWindowInSeconds;

    LOGS_INFO << L"HCSettingsSetTimeoutWindow: " << timeoutWindowInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsGetRetryDelay(
    _In_ uint32_t* retryDelayInSeconds
    )
{
    verify_http_singleton();
    *retryDelayInSeconds = get_http_singleton()->m_retryDelayInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsSetRetryDelay(
    _In_ uint32_t retryDelayInSeconds
    )
{
    verify_http_singleton();
    get_http_singleton()->m_retryDelayInSeconds = retryDelayInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsGetTimeoutWindow(
    _Out_ uint32_t* timeoutWindowInSeconds
    )
{
    verify_http_singleton();
    *timeoutWindowInSeconds = get_http_singleton()->m_timeoutWindowInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsSetAssertsForThrottling(
    _In_ bool enableAssertsForThrottling
    )
{
    verify_http_singleton();
    get_http_singleton()->m_enableAssertsForThrottling = enableAssertsForThrottling;
}

HC_API void HC_CALLING_CONV
HCSettingsGetAssertsForThrottling(
    _Out_ bool* enableAssertsForThrottling
    )
{
    verify_http_singleton();
    *enableAssertsForThrottling = get_http_singleton()->m_enableAssertsForThrottling;
}

HC_API void HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_CALL_HANDLE call
    )
{
    verify_http_singleton();

    std::lock_guard<std::mutex> guard(get_http_singleton()->m_mocksLock);
    get_http_singleton()->m_mocks.push_back(call);
    get_http_singleton()->m_mocksEnabled = true;
}

HC_API void HC_CALLING_CONV
HCMockClearMocks()
{
    verify_http_singleton();

    std::lock_guard<std::mutex> guard(get_http_singleton()->m_mocksLock);

    for (auto& mockCall : get_http_singleton()->m_mocks)
    {
        HCHttpCallCleanup(mockCall);
    }

    get_http_singleton()->m_mocks.clear();
    get_http_singleton()->m_mocksEnabled = false;
}

