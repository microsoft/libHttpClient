// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../http/httpcall.h"
#include "buildver.h"
#include "singleton.h"

using namespace xbox::httpclient;

static const uint32_t DEFAULT_TIMEOUT_WINDOW_IN_SECONDS = 20;
static const uint32_t DEFAULT_RETRY_DELAY_IN_SECONDS = 2;

static std::shared_ptr<http_singleton> g_httpSingleton_atomicReadsOnly;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

http_singleton::http_singleton() 
{
    m_lastId = 0;
    m_performFunc = Internal_HCHttpCallPerform;
    m_timeoutWindowInSeconds = DEFAULT_TIMEOUT_WINDOW_IN_SECONDS;
    m_retryDelayInSeconds = DEFAULT_RETRY_DELAY_IN_SECONDS;
    m_enableAssertsForThrottling = true;
    m_mocksEnabled = false;
    m_lastMatchingMock = nullptr;
    m_pendingReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
}

http_singleton::~http_singleton()
{
    for (auto& mockCall : m_mocks)
    {
        HCHttpCallCleanup(mockCall);
    }
    m_mocks.clear();
}

std::shared_ptr<http_singleton> get_http_singleton()
{
    auto httpSingleton = std::atomic_load(&g_httpSingleton_atomicReadsOnly);

#if ENABLE_ASSERTS
    if (httpSingleton == nullptr)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "Call HCGlobalInitialize() fist");
        assert(httpSingleton != nullptr);
    }
#endif

    return httpSingleton;
}

void init_http_singleton()
{
    // TODO still need to figure out the best way to support multiple clients
    auto httpSingleton = std::atomic_load(&g_httpSingleton_atomicReadsOnly);
    if (!httpSingleton)
    {
        auto newSingleton = std::make_shared<http_singleton>();
        std::atomic_compare_exchange_strong(
            &g_httpSingleton_atomicReadsOnly,
            &httpSingleton,
            newSingleton
        );
        // At this point there is a singleton (ours or someone else's)
    }
}

void cleanup_http_singleton()
{
    std::shared_ptr<http_singleton> httpSingleton;
    httpSingleton = std::atomic_exchange(&g_httpSingleton_atomicReadsOnly, httpSingleton);

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

std::shared_ptr<http_task_completed_queue> http_singleton::get_task_completed_queue_for_taskgroup(_In_ uint64_t taskGroupId)
{
    std::lock_guard<std::mutex> lock(m_taskCompletedQueueLock);
    auto it = m_taskCompletedQueue.find(taskGroupId);
    if (it != m_taskCompletedQueue.end())
    {
        return it->second;
    }

    std::shared_ptr<http_task_completed_queue> taskQueue = std::make_shared<http_task_completed_queue>();
    taskQueue->m_completeReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));

    m_taskCompletedQueue[taskGroupId] = taskQueue;
    return taskQueue;
}

#if HC_USE_HANDLES
HANDLE http_singleton::get_pending_ready_handle()
{
    return m_pendingReadyHandle.get();
}

void http_singleton::set_task_pending_ready()
{
    SetEvent(get_pending_ready_handle());
}
#endif

#if HC_USE_HANDLES
HANDLE http_task_completed_queue::get_complete_ready_handle()
{
    return m_completeReadyHandle.get();
}

void http_task_completed_queue::set_task_completed_event()
{
    SetEvent(get_complete_ready_handle());
}
#endif

http_internal_queue<HC_TASK*>& http_task_completed_queue::get_completed_queue()
{
    return m_completedQueue;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
