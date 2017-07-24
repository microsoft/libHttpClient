// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"

class http_task_completed_queue
{
private:
#if UWP_API || UNITTEST_API
    win32_handle m_completeReadyHandle;
#endif
    http_internal_queue<std::shared_ptr<HC_ASYNC_INFO>> m_asyncCompleteQueue;

public:
    http_internal_queue<std::shared_ptr<HC_ASYNC_INFO>> get_queue();
#if UWP_API || UNITTEST_API
    HANDLE get_complete_ready_handle();
    void set_async_op_complete_ready();
#endif
};

struct http_singleton
{
    http_singleton();
    ~http_singleton();

    std::mutex m_singletonLock;

    std::mutex m_asyncLock;
    http_internal_queue<std::shared_ptr<HC_ASYNC_INFO>> m_asyncPendingQueue;
    http_internal_vector<std::shared_ptr<HC_ASYNC_INFO>> m_asyncProcessingQueue;

    std::mutex m_taskCompletedQueueLock;
    std::shared_ptr<http_task_completed_queue> get_task_queue_for_id(_In_ uint64_t taskGroupId);
    std::map<uint64_t, std::shared_ptr<http_task_completed_queue>> m_taskCompletedQueue;

    std::function<_Ret_maybenull_ _Post_writable_byte_size_(dwSize) void*(_In_ size_t dwSize)> m_pMemAllocHook;
    std::function<void(_In_ void* pAddress)> m_pMemFreeHook;

    function_context add_logging_handler(_In_ std::function<void(HC_LOG_LEVEL, const std::string&, const std::string&)> handler);
    void remove_logging_handler(_In_ function_context context);
    void _Raise_logging_event(_In_ HC_LOG_LEVEL level, _In_ const std::string& category, _In_ const std::string& message);

    std::mutex m_loggingWriteLock;
    http_internal_unordered_map<function_context, std::function<void(HC_LOG_LEVEL, const std::string&, const std::string&)>> m_loggingHandlers;
    function_context m_loggingHandlersCounter;

    HC_HTTP_CALL_PERFORM_FUNC m_performFunc;
    HC_LOG_LEVEL m_traceLevel;
    uint32_t m_timeoutWindowInSeconds;
    uint32_t m_retryDelayInSeconds;
    bool m_enableAssertsForThrottling;

    std::mutex m_mocksLock;
    std::vector<HC_CALL*> m_mocks;
    HC_CALL* m_lastMatchingMock;
    bool m_mocksEnabled;

#if UWP_API || UNITTEST_API
    HANDLE get_pending_ready_handle();
    win32_handle m_pendingReadyHandle;
#endif
    void set_async_op_pending_ready();
};

http_singleton* get_http_singleton(_In_ bool createIfRequired = false);

void VerifyGlobalInit();
http_internal_string SetOptionalParam(_In_opt_ PCSTR_T param);



