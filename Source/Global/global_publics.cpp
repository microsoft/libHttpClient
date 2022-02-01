// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../HTTP/httpcall.h"
#include "buildver.h"
#include "global.h"

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
    return http_singleton::create(args);
}
CATCH_RETURN()

STDAPI_(bool) 
HCIsInitialized() noexcept
{
    auto httpSingleton = get_http_singleton();
    return (nullptr != httpSingleton);
}

STDAPI_(void) HCCleanup() noexcept
try
{
    XTaskQueueHandle queue = nullptr;
    HRESULT hr = XTaskQueueCreate(
        XTaskQueueDispatchMode::ThreadPool,
        XTaskQueueDispatchMode::ThreadPool,
        &queue);
    if (SUCCEEDED(hr))
    {
        XAsyncBlock async{};
        async.queue = queue; // queue is required for this call
        hr = HCCleanupAsync(&async); 
        if (SUCCEEDED(hr))
        {
            XAsyncGetStatus(&async, true);
        }
        XTaskQueueCloseHandle(queue);
    }
}
CATCH_RETURN_WITH(;)

STDAPI HCCleanupAsync(XAsyncBlock* async) noexcept
try
{
    return http_singleton::cleanup_async(async);
}
CATCH_RETURN()

STDAPI
HCSetGlobalProxy(_In_ const char* proxyUri) noexcept
try
{
    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
    {
        return E_HC_NOT_INITIALISED;
    }

    return httpSingleton->set_global_proxy(proxyUri);
}
CATCH_RETURN()

STDAPI
HCSetHttpCallPerformFunction(
    _In_ HCCallPerformFunction performFunc,
    _In_opt_ void* performContext
    ) noexcept
{
    if (performFunc == nullptr)
    {
        return E_INVALIDARG;
    }

    auto httpSingleton = get_http_singleton();
    if (httpSingleton)
    {
        return E_HC_ALREADY_INITIALISED;
    }

    auto& info = GetUserHttpPerformHandler();
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

    auto& info = GetUserHttpPerformHandler();
    *performFunc = info.handler;
    *performContext = info.context;
    return S_OK;
}
CATCH_RETURN()

STDAPI_(int32_t) HCAddCallRoutedHandler(
    _In_ HCCallRoutedHandler handler,
    _In_opt_ void* context
    ) noexcept
{
    if (handler == nullptr)
    {
        return -1;
    }

    auto httpSingleton = get_http_singleton();
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
    auto httpSingleton = get_http_singleton();
    if (nullptr != httpSingleton)
    {
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
        httpSingleton->m_callRoutedHandlers.erase(handlerContext);
    }
}

#if !HC_NOWEBSOCKETS
STDAPI_(int32_t) HCAddWebSocketRoutedHandler(
    _In_ HCWebSocketRoutedHandler handler,
    _In_opt_ void* context
) noexcept
{
    if (handler == nullptr)
    {
        return -1;
    }

    auto httpSingleton = get_http_singleton();
    if (nullptr == httpSingleton)
        return E_HC_NOT_INITIALISED;

    std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
    auto functionContext = httpSingleton->m_callRoutedHandlersContext++;
    httpSingleton->m_webSocketRoutedHandlers[functionContext] = std::make_pair(handler, context);
    return functionContext;
}

STDAPI_(void) HCRemoveWebSocketRoutedHandler(
    _In_ int32_t handlerContext
) noexcept
{
    auto httpSingleton = get_http_singleton();
    if (nullptr != httpSingleton)
    {
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_callRoutedHandlersLock);
        httpSingleton->m_webSocketRoutedHandlers.erase(handlerContext);
    }
}
#endif
