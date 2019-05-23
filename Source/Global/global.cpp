// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../HTTP/httpcall.h"
#include "buildver.h"
#include "global.h"

#if !HC_NOWEBSOCKETS
#include "../WebSocket/hcwebsocket.h"
#endif

using namespace xbox::httpclient;

static std::shared_ptr<http_singleton> g_httpSingleton_atomicReadsOnly;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

http_singleton::http_singleton(
    HttpPerformInfo const& httpPerformInfo,
#if !HC_NOWEBSOCKETS
    WebSocketPerformInfo const& websocketPerformInfo,
#endif
    PerformEnv&& performEnv
) :
    m_httpPerform{ httpPerformInfo },
    m_performEnv{ std::move(performEnv) }
#if !HC_NOWEBSOCKETS
    , m_websocketPerform{ websocketPerformInfo }
#endif
{}

http_singleton::~http_singleton()
{
    g_httpSingleton_atomicReadsOnly = nullptr;
    for (auto& mockCall : m_mocks)
    {
        HCHttpCallCloseHandle(mockCall);
    }
    m_mocks.clear();
}

std::shared_ptr<http_singleton> get_http_singleton(bool assertIfNull)
{
    auto httpSingleton = std::atomic_load(&g_httpSingleton_atomicReadsOnly);
    if (assertIfNull && httpSingleton == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Call HCInitialize() fist");
        ASSERT(httpSingleton != nullptr);
    }

    return httpSingleton;
}

HRESULT init_http_singleton(HCInitArgs* args)
{
    HRESULT hr = S_OK;

    // TODO do as a run once? to avoid platform code being inited twice?
    auto httpSingleton = std::atomic_load(&g_httpSingleton_atomicReadsOnly);
    if (!httpSingleton)
    {
        PerformEnv performEnv;
        hr = Internal_InitializeHttpPlatform(args, performEnv);

        if (SUCCEEDED(hr))
        {
            auto newSingleton = http_allocate_shared<http_singleton>(
                GetUserHttpPerformHandler(),
#if !HC_NOWEBSOCKETS
                GetUserWebSocketPerformHandlers(),
#endif
                std::move(performEnv)
            );
            std::atomic_compare_exchange_strong(
                &g_httpSingleton_atomicReadsOnly,
                &httpSingleton,
                newSingleton
            );

            if (newSingleton == nullptr)
            {
                hr = E_OUTOFMEMORY;
            }
            // At this point there is a singleton (ours or someone else's)
        }
    }

    return hr;
}

void cleanup_http_singleton()
{
    std::shared_ptr<http_singleton> httpSingleton;
    httpSingleton = std::atomic_exchange(&g_httpSingleton_atomicReadsOnly, httpSingleton);

    if (httpSingleton != nullptr)
    {
        shared_ptr_cache::cleanup(httpSingleton);

        // Wait for all other references to the singleton to go away
        // Note that the use count check here is only valid because we never create
        // a weak_ptr to the singleton. If we did that could cause the use count
        // to increase even though we are the only strong reference
        while (httpSingleton.use_count() > 1)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
        }
        // httpSingleton will be destroyed on this thread now
    }
}

void http_singleton::set_retry_state(
    _In_ uint32_t retryAfterCacheId,
    _In_ const http_retry_after_api_state& state)
{
    std::lock_guard<std::recursive_mutex> lock(m_retryAfterCacheLock); // STL is not safe for multithreaded writes
    http_retry_after_api_state oldstate = get_retry_state(retryAfterCacheId);
    if (oldstate.statusCode < 400)
    {
        m_retryAfterCache[retryAfterCacheId] = state;
    }
    else if (oldstate.retryAfterTime <= state.retryAfterTime)
    {
        m_retryAfterCache[retryAfterCacheId] = state;
    }
}

http_retry_after_api_state http_singleton::get_retry_state(_In_ uint32_t retryAfterCacheId)
{
    auto it = m_retryAfterCache.find(retryAfterCacheId); // STL is multithread read safe
    if (it != m_retryAfterCache.end())
    {
        return it->second; // returning a copy of state struct
    }

    return http_retry_after_api_state();
}

void http_singleton::clear_retry_state(_In_ uint32_t retryAfterCacheId)
{
    std::lock_guard<std::recursive_mutex> lock(m_retryAfterCacheLock); // STL is not safe for multithreaded writes
    m_retryAfterCache.erase(retryAfterCacheId);
}

HttpPerformInfo& GetUserHttpPerformHandler() noexcept
{
    static HttpPerformInfo handler(&Internal_HCHttpCallPerformAsync, nullptr);
    return handler;
}

#if !HC_NOWEBSOCKETS
WebSocketPerformInfo& GetUserWebSocketPerformHandlers() noexcept
{
    static WebSocketPerformInfo handlers(
        Internal_HCWebSocketConnectAsync,
        Internal_HCWebSocketSendMessageAsync,
        Internal_HCWebSocketSendBinaryMessageAsync,
        Internal_HCWebSocketDisconnect,
        nullptr
    );
    return handlers;
}
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
