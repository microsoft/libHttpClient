// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;
using namespace xbox::httpclient::log;

HC_API void HC_CALLING_CONV
HCSettingsSetLogLevel(
    _In_ HC_LOG_LEVEL traceLevel
    )
{
    HCTraceLevel internalTraceLevel = HC_TRACELEVEL_OFF;
    switch (traceLevel)
    {
        case LOG_OFF: internalTraceLevel = HC_TRACELEVEL_OFF; break;
        case LOG_ERROR: internalTraceLevel = HC_TRACELEVEL_WARNING; break;
        case LOG_VERBOSE: internalTraceLevel = HC_TRACELEVEL_INFORMATION; break;
    };

    HC_TRACE_SET_VERBOSITY(HTTPCLIENT, internalTraceLevel);
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCSettingsSetLogLevel: %d", traceLevel);
}

HC_API void HC_CALLING_CONV
HCSettingsGetLogLevel(
    _Out_ HC_LOG_LEVEL* traceLevel
    )
{
    *traceLevel = static_cast<HC_LOG_LEVEL>(HC_TRACE_GET_VERBOSITY(HTTPCLIENT)); // TODO fix
}

HC_API void HC_CALLING_CONV
HCSettingsSetTimeoutWindow(
    _In_ uint32_t timeoutWindowInSeconds
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);
    httpSingleton->m_timeoutWindowInSeconds = timeoutWindowInSeconds;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCSettingsTimeoutWindow: %u", timeoutWindowInSeconds);
}

HC_API void HC_CALLING_CONV
HCSettingsGetRetryDelay(
    _In_ uint32_t* retryDelayInSeconds
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);
    *retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsSetRetryDelay(
    _In_ uint32_t retryDelayInSeconds
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);
    httpSingleton->m_retryDelayInSeconds = retryDelayInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsGetTimeoutWindow(
    _Out_ uint32_t* timeoutWindowInSeconds
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);
    *timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
}

HC_API void HC_CALLING_CONV
HCSettingsSetAssertsForThrottling(
    _In_ bool enableAssertsForThrottling
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);
    httpSingleton->m_enableAssertsForThrottling = enableAssertsForThrottling;
}

HC_API void HC_CALLING_CONV
HCSettingsGetAssertsForThrottling(
    _Out_ bool* enableAssertsForThrottling
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);
    *enableAssertsForThrottling = httpSingleton->m_enableAssertsForThrottling;
}

HC_API void HC_CALLING_CONV
HCMockAddMock(
    _In_ HC_CALL_HANDLE call
    )
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);

    std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);
    httpSingleton->m_mocks.push_back(call);
    httpSingleton->m_mocksEnabled = true;
}

HC_API void HC_CALLING_CONV
HCMockClearMocks()
{
    auto httpSingleton = get_http_singleton();
    xbox::httpclient::verify_http_singleton(httpSingleton);

    std::lock_guard<std::mutex> guard(httpSingleton->m_mocksLock);

    for (auto& mockCall : httpSingleton->m_mocks)
    {
        HCHttpCallCleanup(mockCall);
    }

    httpSingleton->m_mocks.clear();
    httpSingleton->m_mocksEnabled = false;
}

