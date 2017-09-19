// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

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

struct http_singleton
{
    http_singleton();
    ~http_singleton();

    std::mutex m_singletonLock;

    // Task state
    std::atomic<std::uint64_t> m_lastId;
    std::mutex m_taskHandleIdMapLock;
    http_internal_map<uint64_t, std::unique_ptr<HC_TASK>> m_taskHandleIdMap;

    std::mutex m_taskLock;
    http_internal_queue<HC_TASK*> m_taskPendingQueue;
    http_internal_vector<HC_TASK*> m_taskExecutingQueue;

    std::mutex m_taskCompletedQueueLock;
    http_internal_map<uint64_t, std::shared_ptr<http_task_completed_queue>> m_taskCompletedQueue;
    std::shared_ptr<http_task_completed_queue> get_task_completed_queue_for_taskgroup(_In_ uint64_t taskGroupId);

    // HTTP state
    HC_HTTP_CALL_PERFORM_FUNC m_performFunc;
    bool m_retryAllowed;
    uint32_t m_timeoutInSeconds;
    uint32_t m_timeoutWindowInSeconds;
    uint32_t m_retryDelayInSeconds;
    bool m_enableAssertsForThrottling;

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
};

std::shared_ptr<http_singleton> get_http_singleton();
HC_RESULT init_http_singleton();
void cleanup_http_singleton();

NAMESPACE_XBOX_HTTP_CLIENT_END



