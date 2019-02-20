// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include <httpClient/httpProvider.h>
#include "../HTTP/httpcall.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace log
{
    class logger;
}

typedef struct http_retry_after_api_state
{
    http_retry_after_api_state() : statusCode(0)
    {
    }

    http_retry_after_api_state(
        _In_ const chrono_clock_t::time_point& _retryAfterTime,
        _In_ uint32_t _statusCode
        ) :
        retryAfterTime(_retryAfterTime),
        statusCode(_statusCode)
    {
    }

    chrono_clock_t::time_point retryAfterTime;
    uint32_t statusCode;
} http_retry_after_api_state;

static const uint32_t DEFAULT_TIMEOUT_WINDOW_IN_SECONDS = 20;
static const uint32_t DEFAULT_HTTP_TIMEOUT_IN_SECONDS = 30;
static const uint32_t DEFAULT_RETRY_DELAY_IN_SECONDS = 2;

typedef struct http_singleton
{
    http_singleton(PerformInfo const& performInfo, PerformEnv&& performEnv);
    ~http_singleton();

    std::recursive_mutex m_singletonLock;

    std::recursive_mutex m_retryAfterCacheLock;
    std::unordered_map<uint32_t, http_retry_after_api_state> m_retryAfterCache;
    void set_retry_state(_In_ uint32_t retryAfterCacheId, _In_ const http_retry_after_api_state& state);
    http_retry_after_api_state get_retry_state(_In_ uint32_t retryAfterCacheId);
    void clear_retry_state(_In_ uint32_t retryAfterCacheId);

    std::recursive_mutex m_callRoutedHandlersLock;
    std::atomic<int32_t> m_callRoutedHandlersContext;
    http_internal_unordered_map<int32_t, std::pair<HCCallRoutedHandler, void*>> m_callRoutedHandlers;

    // HTTP state
    PerformInfo const m_perform;
    PerformEnv const m_performEnv;

    std::atomic<std::uint64_t> m_lastId;
    bool m_retryAllowed = true;
    uint32_t m_timeoutInSeconds = DEFAULT_HTTP_TIMEOUT_IN_SECONDS;
    uint32_t m_timeoutWindowInSeconds = DEFAULT_TIMEOUT_WINDOW_IN_SECONDS;
    uint32_t m_retryDelayInSeconds = DEFAULT_RETRY_DELAY_IN_SECONDS;

#if !HC_NOWEBSOCKETS
    // Platform implementation handlers
    HCWebSocketMessageFunction m_websocketMessageFunc = nullptr;
    HCWebSocketCloseEventFunction m_websocketCloseEventFunc = nullptr;
    HCWebSocketConnectFunction m_websocketConnectFunc = nullptr;
    HCWebSocketSendMessageFunction m_websocketSendMessageFunc = nullptr;
    HCWebSocketSendBinaryMessageFunction m_websocketSendBinaryMessageFunc = nullptr;
    HCWebSocketDisconnectFunction m_websocketDisconnectFunc = nullptr;
#endif

    // Mock state
    std::recursive_mutex m_mocksLock;
    http_internal_vector<HC_CALL*> m_mocks;
    HC_CALL* m_lastMatchingMock = nullptr;
    bool m_mocksEnabled = false;

    std::recursive_mutex m_sharedPtrsLock;
    http_internal_unordered_map<void*, std::shared_ptr<void>> m_sharedPtrs;
} http_singleton;


std::shared_ptr<http_singleton> get_http_singleton(bool assertIfNull);
HRESULT init_http_singleton(HCInitArgs* args);
void cleanup_http_singleton();


class shared_ptr_cache
{
public:
    template<typename T>
    static void* store(std::shared_ptr<T> contextSharedPtr)
    {
        auto httpSingleton = get_http_singleton(false);
        if (nullptr == httpSingleton)
            return nullptr;
        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_sharedPtrsLock);

        void *rawVoidPtr = contextSharedPtr.get();
        std::shared_ptr<void> voidSharedPtr(contextSharedPtr, rawVoidPtr);
        httpSingleton->m_sharedPtrs.insert(std::make_pair(rawVoidPtr, voidSharedPtr));
        return rawVoidPtr;
    }

    template<typename T>
    static std::shared_ptr<T> fetch(void *rawContextPtr, bool assertIfNotFound)
    {
        auto httpSingleton = get_http_singleton(false);
        if (nullptr == httpSingleton)
            return nullptr;

        std::lock_guard<std::recursive_mutex> lock(httpSingleton->m_sharedPtrsLock);

        auto iter = httpSingleton->m_sharedPtrs.find(rawContextPtr);
        if (iter != httpSingleton->m_sharedPtrs.end())
        {
            auto returnPtr = std::shared_ptr<T>(iter->second, reinterpret_cast<T*>(iter->second.get()));
            return returnPtr;
        }
        else
        {
            if (assertIfNotFound)
            {
                ASSERT(false && "Context not found!");
            }
            return std::shared_ptr<T>();
        }
    }

    template<typename T>
    static void remove(void *rawContextPtr)
    {
        auto httpSingleton = get_http_singleton(false);
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
        ASSERT(httpSingleton->m_sharedPtrs.size() == 0);
        httpSingleton->m_sharedPtrs.clear();
    }

private:
    shared_ptr_cache();
    shared_ptr_cache(const shared_ptr_cache&);
    shared_ptr_cache& operator=(const shared_ptr_cache&);
};

PerformInfo& GetUserPerformHandler() noexcept;

NAMESPACE_XBOX_HTTP_CLIENT_END
