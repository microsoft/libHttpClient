// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include <httpClient/httpProvider.h>
#include "perform_env.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace log
{
    class logger;
}

typedef struct http_retry_after_api_state
{
    http_retry_after_api_state() = default;

    http_retry_after_api_state(
        _In_ const chrono_clock_t::time_point& _retryAfterTime,
        _In_ uint32_t _statusCode,
        _In_ bool _callPending
    ) :
        retryAfterTime(_retryAfterTime),
        statusCode(_statusCode),
        callPending(_callPending)
    {
    }

    chrono_clock_t::time_point retryAfterTime{};
    uint32_t statusCode{ 0 };
    bool callPending{ false };
} http_retry_after_api_state;

static const uint32_t DEFAULT_TIMEOUT_WINDOW_IN_SECONDS = 20;
static const uint32_t DEFAULT_HTTP_TIMEOUT_IN_SECONDS = 30;
static const uint32_t DEFAULT_RETRY_DELAY_IN_SECONDS = 2;

typedef struct http_singleton
{
public:
    static std::shared_ptr<http_singleton> get() noexcept;
    static HRESULT create(_In_ HCInitArgs* args) noexcept;
    static HRESULT cleanup_async(_In_ XAsyncBlock* async) noexcept;

    http_singleton(const http_singleton&) = delete;
    http_singleton& operator=(http_singleton) = delete;
    ~http_singleton();

    std::recursive_mutex m_singletonLock;

    std::recursive_mutex m_retryAfterCacheLock;
    http_internal_unordered_map<uint32_t, http_retry_after_api_state> m_retryAfterCache;
    void set_retry_state(_In_ uint32_t retryAfterCacheId, _In_ const http_retry_after_api_state& state);
    http_retry_after_api_state get_retry_state(_In_ uint32_t retryAfterCacheId);
    void clear_retry_state(_In_ uint32_t retryAfterCacheId);

    std::recursive_mutex m_callRoutedHandlersLock;
    std::atomic<int32_t> m_callRoutedHandlersContext{ 0 };
    http_internal_unordered_map<int32_t, std::pair<HCCallRoutedHandler, void*>> m_callRoutedHandlers;
#if !HC_NOWEBSOCKETS
    http_internal_unordered_map<int32_t, std::pair<HCWebSocketRoutedHandler, void*>> m_webSocketRoutedHandlers;
#endif

    // HTTP state
    HttpPerformInfo const m_httpPerform;
    PerformEnv m_performEnv;

    HRESULT set_global_proxy(_In_ const char* proxyUri);

    std::atomic<std::uint64_t> m_lastId{ 0 };
    bool m_retryAllowed = true;
    uint32_t m_timeoutInSeconds = DEFAULT_HTTP_TIMEOUT_IN_SECONDS;
    uint32_t m_timeoutWindowInSeconds = DEFAULT_TIMEOUT_WINDOW_IN_SECONDS;
    uint32_t m_retryDelayInSeconds = DEFAULT_RETRY_DELAY_IN_SECONDS;

#if HC_PLATFORM == HC_PLATFORM_GDK
    bool m_disableAssertsForSSLValidationInDevSandboxes{ false };
#endif

#if !HC_NOWEBSOCKETS
    WebSocketPerformInfo const m_websocketPerform;
#endif

    // Mock state
    std::recursive_mutex m_mocksLock;
    http_internal_vector<HC_MOCK_CALL*> m_mocks;

    std::recursive_mutex m_sharedPtrsLock;
    http_internal_unordered_map<void*, std::shared_ptr<void>> m_sharedPtrs;

    http_singleton(
        HttpPerformInfo const& httpPerformInfo,
#if !HC_NOWEBSOCKETS
        WebSocketPerformInfo const& websocketPerformInfo,
#endif
        PerformEnv&& performEnv
    );

private:
    enum class singleton_access_mode
    {
        create,
        get,
        cleanup
    };

    static HRESULT singleton_access(
        _In_ singleton_access_mode mode,
        _In_opt_ HCInitArgs* createArgs,
        _Out_ std::shared_ptr<http_singleton>& singleton
    ) noexcept;

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);

    // Self reference to prevent deletion on static shutdown.
    std::shared_ptr<http_singleton> m_self{ nullptr };
} http_singleton;


std::shared_ptr<http_singleton> get_http_singleton();

class shared_ptr_cache
{
public:
    template<typename T>
    static void* store(std::shared_ptr<T> contextSharedPtr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return nullptr;
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_sharedPtrsLock);

        void *rawVoidPtr = contextSharedPtr.get();
        std::shared_ptr<void> voidSharedPtr(contextSharedPtr, rawVoidPtr);
        httpSingleton->m_sharedPtrs.insert(std::make_pair(rawVoidPtr, voidSharedPtr));
        return rawVoidPtr;
    }

    template<typename T>
    static std::shared_ptr<T> fetch(void *rawContextPtr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return nullptr;

        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_sharedPtrsLock);

        auto iter = httpSingleton->m_sharedPtrs.find(rawContextPtr);
        if (iter != httpSingleton->m_sharedPtrs.end())
        {
            auto returnPtr = std::shared_ptr<T>(iter->second, reinterpret_cast<T*>(iter->second.get()));
            return returnPtr;
        }

        return nullptr;
    }

    static void remove(void *rawContextPtr)
    {
        auto httpSingleton = get_http_singleton();
        if (nullptr == httpSingleton)
            return;

        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_sharedPtrsLock);

        auto iter = httpSingleton->m_sharedPtrs.find(rawContextPtr);
        if (iter != httpSingleton->m_sharedPtrs.end())
        {
            httpSingleton->m_sharedPtrs.erase(iter);
        }
    }

    static void cleanup(_In_ std::shared_ptr<http_singleton> httpSingleton)
    {
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_sharedPtrsLock);
        httpSingleton->m_sharedPtrs.clear();
    }

private:
    shared_ptr_cache();
    shared_ptr_cache(const shared_ptr_cache&);
    shared_ptr_cache& operator=(const shared_ptr_cache&);
};

HttpPerformInfo& GetUserHttpPerformHandler() noexcept;
#if !HC_NOWEBSOCKETS
WebSocketPerformInfo& GetUserWebSocketPerformHandlers() noexcept;
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
