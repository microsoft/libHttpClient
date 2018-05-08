// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../HTTP/httpcall.h"
#include "buildver.h"
#include "global.h"
#include "../WebSocket/hcwebsocket.h"

using namespace xbox::httpclient;

static const uint32_t DEFAULT_TIMEOUT_WINDOW_IN_SECONDS = 20;
static const uint32_t DEFAULT_HTTP_TIMEOUT_IN_SECONDS = 30;
static const uint32_t DEFAULT_RETRY_DELAY_IN_SECONDS = 2;

static std::shared_ptr<http_singleton> g_httpSingleton_atomicReadsOnly;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

http_singleton::http_singleton()
{
    m_lastId = 0;
    m_performFunc = Internal_HCHttpCallPerform;

    m_websocketMessageFunc = nullptr;
    m_websocketCloseEventFunc = nullptr;

    m_websocketConnectFunc = Internal_HCWebSocketConnect;
    m_websocketSendMessageFunc = Internal_HCWebSocketSendMessage;
    m_websocketDisconnectFunc = Internal_HCWebSocketDisconnect;

    m_timeoutWindowInSeconds = DEFAULT_TIMEOUT_WINDOW_IN_SECONDS;
    m_retryDelayInSeconds = DEFAULT_RETRY_DELAY_IN_SECONDS;
    m_mocksEnabled = false;
    m_lastMatchingMock = nullptr;
    m_retryAllowed = true;
    m_timeoutInSeconds = DEFAULT_HTTP_TIMEOUT_IN_SECONDS;

    m_callRoutedHandlersContext = 0;
}

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
        HC_TRACE_ERROR(HTTPCLIENT, "Call HCGlobalInitialize() fist");
        ASSERT(httpSingleton != nullptr);
    }

    return httpSingleton;
}

HRESULT init_http_singleton()
{
    HRESULT hr = S_OK;
    auto httpSingleton = std::atomic_load(&g_httpSingleton_atomicReadsOnly);
    if (!httpSingleton)
    {
        auto newSingleton = http_allocate_shared<http_singleton>();
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
    std::lock_guard<std::mutex> lock(m_retryAfterCacheLock); // STL is not safe for multithreaded writes
    m_retryAfterCache[retryAfterCacheId] = state;
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
    std::lock_guard<std::mutex> lock(m_retryAfterCacheLock); // STL is not safe for multithreaded writes
    m_retryAfterCache.erase(retryAfterCacheId);
}

NAMESPACE_XBOX_HTTP_CLIENT_END
