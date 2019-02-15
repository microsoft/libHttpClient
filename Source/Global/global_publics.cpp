// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../HTTP/httpcall.h"
#include "buildver.h"
#include "global.h"
#include "../Logger/trace_internal.h"

using namespace xbox::httpclient;

STDAPI 
HCGetLibVersion(_Outptr_ const char** version) noexcept
try
{
    if (version == nullptr)
    {
        return E_INVALIDARG;
    }

    *version = LIBHTTPCLIENT_VERSION;
    return S_OK;
}
CATCH_RETURN()

STDAPI 
HCInitialize(_In_opt_ HCInitArgs* args) noexcept
try
{
    HCTraceImplInit();
    return xbox::httpclient::init_http_singleton(args);
}
CATCH_RETURN()

STDAPI_(void) HCCleanup() noexcept
try
{
    xbox::httpclient::cleanup_http_singleton();
    HCTraceImplCleanup();
}
CATCH_RETURN_WITH(;)

STDAPI
HCSetHttpCallPerformFunction(
    _In_ HCCallPerformFunction performFunc,
    _In_opt_ void* performContext
    ) noexcept
{
    auto httpSingleton = get_http_singleton(false);
    if (httpSingleton)
    {
        return E_HC_ALREADY_INITIALISED;
    }

    auto& info = GetUserPerformHandler();
    info.handler = performFunc;
    info.context = performContext;
    return S_OK;
}

STDAPI 
HCGetHttpCallPerformFunction(
    _Out_ HCCallPerformFunction* performFunc,
    _Out_ void** performContext
    ) noexcept
try
{
    if (performFunc == nullptr || performContext == nullptr)
    {
        return E_INVALIDARG;
    }

    auto& info = GetUserPerformHandler();
    *performFunc = info.handler;
    *performContext = info.context;
    return S_OK;
}
CATCH_RETURN()

STDAPI_(int32_t) HCAddCallRoutedHandler(
    _In_ HCCallRoutedHandler handler,
    _In_ void* context
    ) noexcept
{
    if (handler == nullptr)
    {
        return -1;
    }

    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
    auto functionContext = httpSingleton->m_callRoutedHandlersContext++;
    httpSingleton->m_callRoutedHandlers[functionContext] = std::make_pair(handler, context);
    return functionContext;
}

STDAPI_(void) HCRemoveCallRoutedHandler(
    _In_ int32_t handlerContext
    ) noexcept
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr != httpSingleton)
    {
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
        httpSingleton->m_callRoutedHandlers.erase(handlerContext);
    }
}

