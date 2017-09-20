// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../http/httpcall.h"
#include "buildver.h"
#include "global.h"

using namespace xbox::httpclient;

HC_API HC_RESULT HC_CALLING_CONV
HCGlobalGetLibVersion(_Outptr_ PCSTR* version) HC_NOEXCEPT
try
{
    if (version == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    *version = LIBHTTPCLIENT_VERSION;
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCGlobalInitialize() HC_NOEXCEPT
try
{
    HCTraceImplGlobalInit();
    return xbox::httpclient::init_http_singleton();
}
CATCH_RETURN()

HC_API void HC_CALLING_CONV
HCGlobalCleanup() HC_NOEXCEPT
try
{
    xbox::httpclient::cleanup_http_singleton();
    HCTraceImplGlobalCleanup();
}
CATCH_RETURN_WITH(;)

HC_API void HC_CALLING_CONV
HCGlobalSetHttpCallPerformFunction(
    _In_opt_ HC_HTTP_CALL_PERFORM_FUNC performFunc
    ) HC_NOEXCEPT
{
    auto httpSingleton = get_http_singleton();
    httpSingleton->m_performFunc = (performFunc == nullptr) ? Internal_HCHttpCallPerform : performFunc;
}

HC_API HC_RESULT HC_CALLING_CONV
HCGlobalGetHttpCallPerformFunction(
    _Out_ HC_HTTP_CALL_PERFORM_FUNC* performFunc
    ) HC_NOEXCEPT
try
{
    if (performFunc == nullptr)
    {
        return HC_E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    *performFunc = httpSingleton->m_performFunc;
    return HC_OK;
}
CATCH_RETURN()

