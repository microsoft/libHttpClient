// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;
using namespace xbox::httpclient::log;

STDAPI 
HCSettingsSetTraceLevel(
    _In_ HCTraceLevel traceLevel
    ) noexcept
try
{
    HC_TRACE_SET_VERBOSITY(HTTPCLIENT, traceLevel);
    HC_TRACE_SET_VERBOSITY(WEBSOCKET, traceLevel);
    HC_TRACE_INFORMATION(HTTPCLIENT, "HCSettingsSetTraceLevel: %d", traceLevel);
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCSettingsGetTraceLevel(
    _Out_ HCTraceLevel* traceLevel
    ) noexcept
try
{
    if (traceLevel == nullptr)
    {
        return E_INVALIDARG;
    }

    *traceLevel = static_cast<HCTraceLevel>(HC_TRACE_GET_VERBOSITY(HTTPCLIENT));
    return S_OK;
}
CATCH_RETURN()
