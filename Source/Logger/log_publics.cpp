// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;
using namespace xbox::httpclient::log;

STDAPI 
HCSettingsSetLogLevel(
    _In_ HCLogLevel traceLevel
    ) HC_NOEXCEPT
try
{
    HCTraceLevel internalTraceLevel = HCTraceLevel_Off;
    switch (traceLevel)
    {
        case HCLogLevel_Off: internalTraceLevel = HCTraceLevel_Off; break;
        case HCLogLevel_Error: internalTraceLevel = HCTraceLevel_Error; break;
        case HCLogLevel_Important: internalTraceLevel = HCTraceLevel_Important; break;
        case HCLogLevel_Warning: internalTraceLevel = HCTraceLevel_Warning; break;
        case HCLogLevel_Information: internalTraceLevel = HCTraceLevel_Information; break;
        case HCLogLevel_Verbose: internalTraceLevel = HCTraceLevel_Verbose; break;
        default: return E_INVALIDARG;
    };

    HC_TRACE_SET_VERBOSITY(HTTPCLIENT, internalTraceLevel);
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCSettingsSetLogLevel: %d", traceLevel);
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCSettingsGetLogLevel(
    _Out_ HCLogLevel* traceLevel
    ) HC_NOEXCEPT
try
{
    if (traceLevel == nullptr)
    {
        return E_INVALIDARG;
    }

    *traceLevel = static_cast<HCLogLevel>(HC_TRACE_GET_VERBOSITY(HTTPCLIENT)); 
    return S_OK;
}
CATCH_RETURN()
