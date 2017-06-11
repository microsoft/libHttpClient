// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "mem.h"
#include "singleton.h"
#include "log.h"

HC_API void HC_CALLING_CONV
HCSettingsSetDiagnosticsTraceLevel(
    _In_ HC_DIAGNOSTICS_TRACE_LEVEL traceLevel
    )
{
    VerifyGlobalInit();
    get_http_singleton()->m_traceLevel = traceLevel;
}

HC_API void HC_CALLING_CONV
HCSettingsGetDiagnosticsTraceLevel(
    _Out_ HC_DIAGNOSTICS_TRACE_LEVEL* traceLevel
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

