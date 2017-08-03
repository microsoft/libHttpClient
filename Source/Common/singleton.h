// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "uwp/utils_uwp.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace log
{
    class logger;
}

class http_task_completed_queue
{
public:
    http_internal_queue<HC_TASK*>& get_completed_queue();
#if UWP_API
    HANDLE get_complete_ready_handle();
    void set_task_completed_event();
#endif

#if UWP_API
    win32_handle m_completeReadyHandle;
#endif
    http_internal_queue<HC_TASK*> m_completedQueue;
};

struct http_singleton
{
    http_singleton();
    ~http_singleton();

    std::mutex m_singletonLock;

    std::mutex m_taskLock;
    http_internal_queue<HC_TASK*> m_taskPendingQueue;
    http_internal_vector<HC_TASK*> m_taskExecutingQueue;

    std::mutex m_taskHandleIdMapLock;
    http_internal_map<uint64_t, std::unique_ptr<HC_TASK>> m_taskHandleIdMap;

    std::mutex m_taskCompletedQueueLock;
    std::shared_ptr<http_task_completed_queue> get_task_completed_queue_for_taskgroup(_In_ uint64_t taskGroupId);
    http_internal_map<uint64_t, std::shared_ptr<http_task_completed_queue>> m_taskCompletedQueue;

    function_context add_logging_handler(_In_ std::function<void(HC_LOG_LEVEL, const std::string&, const std::string&)> handler);
    void remove_logging_handler(_In_ function_context context);
    void raise_logging_event(_In_ HC_LOG_LEVEL level, _In_ const std::string& category, _In_ const std::string& message);

    std::mutex m_loggingWriteLock;
    http_internal_unordered_map<function_context, std::function<void(HC_LOG_LEVEL, const std::string&, const std::string&)>> m_loggingHandlers;
    function_context m_loggingHandlersCounter;

    uint64_t m_lastHttpCallId;
    HC_HTTP_CALL_PERFORM_FUNC m_performFunc;
    uint32_t m_timeoutWindowInSeconds;
    uint32_t m_retryDelayInSeconds;
    bool m_enableAssertsForThrottling;

    std::mutex m_mocksLock;
    std::vector<HC_CALL*> m_mocks;
    HC_CALL* m_lastMatchingMock;
    bool m_mocksEnabled;

#if UWP_API
    HANDLE get_pending_ready_handle();
    win32_handle m_pendingReadyHandle;
#endif
    void set_task_pending_ready();

    std::shared_ptr<xbox::httpclient::log::logger> m_logger;
};

http_singleton* get_http_singleton(_In_ bool createIfRequired = false);
void verify_http_singleton();
void cleanup_http_singleton();

NAMESPACE_XBOX_HTTP_CLIENT_END



