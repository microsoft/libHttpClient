// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../http/httpcall.h"
#include "buildver.h"
#include "singleton.h"

using namespace xbox::httpclient;

HC_API void HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR_T* version)
{
    *version = LIBHTTPCLIENT_VERSION;
}

HC_API void HC_CALLING_CONV
HCGlobalInitialize()
{
    HCTraceImplGlobalInit();
    xbox::httpclient::init_http_singleton();
}

HC_API void HC_CALLING_CONV
HCGlobalCleanup()
{
    xbox::httpclient::cleanup_http_singleton();
    HCTraceImplGlobalCleanup();
}

HC_API void HC_CALLING_CONV
HCGlobalSetHttpCallPerformFunction(
    _In_opt_ HC_HTTP_CALL_PERFORM_FUNC performFunc
    )
{
    auto httpSingleton = get_http_singleton();
    httpSingleton->m_performFunc = (performFunc == nullptr) ? Internal_HCHttpCallPerform : performFunc;
}

HC_API void HC_CALLING_CONV
HCGlobalGetHttpCallPerformFunction(
    _Out_ HC_HTTP_CALL_PERFORM_FUNC* performFunc
    )
{
    auto httpSingleton = get_http_singleton();
    *performFunc = httpSingleton->m_performFunc;
}

