// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include <httpClient/httpProvider.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace log
{
    class logger;
}

class http_task_completed_queue
{
public:
    http_internal_queue<HC_TASK*>& get_completed_queue();
#if HC_USE_HANDLES
    HANDLE get_complete_ready_handle();
    void set_task_completed_event();
#endif

#if HC_USE_HANDLES
    win32_handle m_completeReadyHandle;
#endif
    http_internal_queue<HC_TASK*> m_completedQueue;
};

struct HC_TASK_EVENT_FUNC_NODE
{
    HC_TASK_EVENT_FUNC taskEventFunc;
    void* taskEventFuncContext;
    HC_SUBSYSTEM_ID taskSubsystemId;
};

struct http_retry_after_api_state
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
};

struct http_singleton
{
    http_singleton();
    ~http_singleton();

    std::mutex m_singletonLock;

    // Task state
    std::mutex m_taskEventListLock;
    std::map<HC_TASK_EVENT_HANDLE, HC_TASK_EVENT_FUNC_NODE> m_taskEventFuncList;
    std::atomic<std::uint64_t> m_lastId;
    std::mutex m_taskHandleIdMapLock;
    http_internal_map<uint64_t, HC_TASK_PTR> m_taskHandleIdMap;

    std::mutex m_taskLock;
    http_internal_map<uint64_t, http_internal_queue<HC_TASK*>> m_taskPendingQueue;
    http_internal_vector<HC_TASK*> m_taskExecutingQueue;
    http_internal_queue<HC_TASK*>& get_task_pending_queue(_In_ uint64_t taskSubsystemId);

    std::mutex m_taskCompletedQueueLock;
    http_internal_map<HC_SUBSYSTEM_ID, http_internal_map<uint64_t, std::shared_ptr<http_task_completed_queue>>> m_taskCompletedQueue;
    std::shared_ptr<http_task_completed_queue> get_task_completed_queue_for_taskgroup(
        _In_ HC_SUBSYSTEM_ID taskSubsystemId, 
        _In_ uint64_t taskGroupId);

    std::mutex m_retryAfterCacheLock;
    std::unordered_map<uint32_t, http_retry_after_api_state> m_retryAfterCache;
    void set_retry_state(_In_ uint32_t retryAfterCacheId, _In_ const http_retry_after_api_state& state);
    http_retry_after_api_state get_retry_state(_In_ uint32_t retryAfterCacheId);
    void clear_retry_state(_In_ uint32_t retryAfterCacheId);

    // HTTP state
    HC_HTTP_CALL_PERFORM_FUNC m_performFunc;
    bool m_retryAllowed;
    uint32_t m_timeoutInSeconds;
    uint32_t m_timeoutWindowInSeconds;
    uint32_t m_retryDelayInSeconds;

    // WebSocket state
    HC_WEBSOCKET_MESSAGE_FUNC m_websocketMessageFunc;
    HC_WEBSOCKET_CLOSE_EVENT_FUNC m_websocketCloseEventFunc;

    HC_WEBSOCKET_CONNECT_FUNC m_websocketConnectFunc;
    HC_WEBSOCKET_SEND_MESSAGE_FUNC m_websocketSendMessageFunc;
    HC_WEBSOCKET_DISCONNECT_FUNC m_websocketDisconnectFunc;

    // Mock state
    std::mutex m_mocksLock;
    http_internal_vector<HC_CALL*> m_mocks;
    HC_CALL* m_lastMatchingMock;
    bool m_mocksEnabled;

#if HC_USE_HANDLES
    HANDLE get_pending_ready_handle();
    win32_handle m_pendingReadyHandle;
#endif
    void set_task_pending_ready();

    std::mutex m_sharedPtrsLock;
    http_internal_unordered_map<void*, std::shared_ptr<void>> m_sharedPtrs;
};


std::shared_ptr<http_singleton> get_http_singleton(bool assertIfNull);
HC_RESULT init_http_singleton();
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
        std::lock_guard<std::mutex> lock(httpSingleton->m_sharedPtrsLock);

        void *rawVoidPtr = contextSharedPtr.get();
        std::shared_ptr<void> voidSharedPtr(contextSharedPtr, rawVoidPtr);
        httpSingleton->m_sharedPtrs.insert(std::make_pair(rawVoidPtr, voidSharedPtr));
        return rawVoidPtr;
    }

    template<typename T>
    static std::shared_ptr<T> fetch(void *rawContextPtr, bool deleteShared = true)
    {
        auto httpSingleton = get_http_singleton(false);
        if (nullptr == httpSingleton)
            return nullptr;

        std::lock_guard<std::mutex> lock(httpSingleton->m_sharedPtrsLock);

        auto iter = httpSingleton->m_sharedPtrs.find(rawContextPtr);
        if (iter != httpSingleton->m_sharedPtrs.end())
        {
            auto returnPtr = std::shared_ptr<T>(iter->second, reinterpret_cast<T*>(iter->second.get()));
            if (deleteShared)
            {
                httpSingleton->m_sharedPtrs.erase(iter);
            }
            return returnPtr;
        }
        else
        {
            HC_ASSERT(false && "Context not found!");
            return std::shared_ptr<T>();
        }
    }

    static void cleanup(_In_ std::shared_ptr<http_singleton> httpSingleton)
    {
        std::lock_guard<std::mutex> lock(httpSingleton->m_sharedPtrsLock);
        HC_ASSERT(httpSingleton->m_sharedPtrs.size() == 0);
        httpSingleton->m_sharedPtrs.clear();
    }

private:
    shared_ptr_cache();
    shared_ptr_cache(const shared_ptr_cache&);
    shared_ptr_cache& operator=(const shared_ptr_cache&);
};

NAMESPACE_XBOX_HTTP_CLIENT_END



