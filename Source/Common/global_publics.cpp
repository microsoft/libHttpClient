// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../http/httpcall.h"
#include "buildver.h"
#include "singleton.h"
#include "log.h"
#include "debug_output.h"

using namespace xbox::httpclient;

HC_API void HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR_T* version)
{
    *version = LIBHTTPCLIENT_VERSION;
}

HC_API void HC_CALLING_CONV
HCGlobalInitialize()
{
    xbox::httpclient::get_http_singleton(true);
}

HC_API void HC_CALLING_CONV
HCGlobalCleanup()
{
    xbox::httpclient::cleanup_http_singleton();
}

HC_API void HC_CALLING_CONV
HCGlobalSetHttpCallPerformFunction(
    _In_opt_ HC_HTTP_CALL_PERFORM_FUNC performFunc
    )
{
    xbox::httpclient::verify_http_singleton();
    get_http_singleton()->m_performFunc = (performFunc == nullptr) ? Internal_HCHttpCallPerform : performFunc;
}

HC_API void HC_CALLING_CONV
HCGlobalGetHttpCallPerformFunction(
    _Out_ HC_HTTP_CALL_PERFORM_FUNC* performFunc
    )
{
    xbox::httpclient::verify_http_singleton();
    *performFunc = get_http_singleton()->m_performFunc;
}

