// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

void http_task_queue_pending(_In_ HC_TASK* task)
{
    task->state = http_task_state::pending;
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
    auto& taskPendingQueue = get_http_singleton()->m_taskPendingQueue;
    taskPendingQueue.push(task);

    HC_TRACE_INFORMATION(HTTPCLIENT, "Task queue pending: queueSize=%zu taskId=%llu",
        taskPendingQueue.size(), task->id);

    get_http_singleton()->set_task_pending_ready();
}

HC_TASK* http_task_get_next_pending()
{
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
    auto& taskPendingQueue = get_http_singleton()->m_taskPendingQueue;
    if (!taskPendingQueue.empty())
    {
        auto it = taskPendingQueue.front();
        taskPendingQueue.pop();
        return it;
    }
    return nullptr;
}

void http_task_process_pending(_In_ HC_TASK* task)
{
    task->state = http_task_state::processing;

    {
        std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
        auto& taskExecutingQueue = get_http_singleton()->m_taskExecutingQueue;
        taskExecutingQueue.push_back(task);

        HC_TRACE_INFORMATION(HTTPCLIENT, "Task execute: executeQueueSize=%zu taskId=%llu",
            taskExecutingQueue.size(), task->id);
    }

    task->executionRoutine(
        task->executionRoutineContext,
        task->id
        );
}

void http_task_queue_completed(_In_ HC_TASK_HANDLE taskHandleId)
{
    HC_TASK* taskHandle = http_task_get_task_from_handle_id(taskHandleId);
    if (taskHandle == nullptr)
        return; // invalid or old taskHandleId ?

    taskHandle->state = http_task_state::completed;

    HC_TASK* task = nullptr;
    {
        std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
        auto& taskProcessingQueue = get_http_singleton()->m_taskExecutingQueue;
        for (auto& it : taskProcessingQueue)
        {
            if (it == taskHandle)
            {
                task = it;
            }
        }
        // TODO what happens if task is not found?

        taskProcessingQueue.erase(std::remove(taskProcessingQueue.begin(), taskProcessingQueue.end(), task), taskProcessingQueue.end());

        auto& taskCompletedQueue = get_http_singleton()->get_task_completed_queue_for_taskgroup(taskHandle->taskGroupId)->get_completed_queue();
        taskCompletedQueue.push(task);

        HC_TRACE_INFORMATION(HTTPCLIENT, "Task queue completed: queueSize=%zu taskId=%llu",
            taskCompletedQueue.size(), task->id);
    }

#if UWP_API
    SetEvent(taskHandle->resultsReady.get());
#endif
    get_http_singleton()->get_task_completed_queue_for_taskgroup(taskHandle->taskGroupId)->set_task_completed_event();
}

HC_TASK* http_task_get_next_completed(_In_ uint64_t taskGroupId)
{
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
    auto& completedQueue = get_http_singleton()->get_task_completed_queue_for_taskgroup(taskGroupId)->get_completed_queue();
    if (!completedQueue.empty())
    {
        auto it = completedQueue.front();
        completedQueue.pop();
        return it;
    }
    return nullptr;
}

void http_task_process_completed(_In_ HC_TASK* task)
{
    task->writeResultsRoutine(
        task->writeResultsRoutineContext,
        task->id,
        task->completionRoutine,
        task->completionRoutineContext
        );
}

HC_TASK* http_task_get_task_from_handle_id(
    _In_ HC_TASK_HANDLE taskHandleId
    )
{
    std::lock_guard<std::mutex> lock(get_http_singleton()->m_taskHandleIdMapLock);
    auto& taskHandleIdMap = get_http_singleton()->m_taskHandleIdMap;
    auto it = taskHandleIdMap.find(taskHandleId);
    if (it != taskHandleIdMap.end())
    {
        return it->second.get();
    }

    return nullptr;
}

void http_task_store_task_from_handle_id(
    _In_ std::unique_ptr<HC_TASK> task
    )
{
    std::lock_guard<std::mutex> lock(get_http_singleton()->m_taskHandleIdMapLock);
    auto& taskHandleIdMap = get_http_singleton()->m_taskHandleIdMap;
    taskHandleIdMap[task->id] = std::move(task);
}

void http_task_clear_task_from_handle_id(
    _In_ HC_TASK_HANDLE taskHandleId
    )
{
    std::lock_guard<std::mutex> lock(get_http_singleton()->m_taskHandleIdMapLock);
    auto& taskHandleIdMap = get_http_singleton()->m_taskHandleIdMap;
    taskHandleIdMap.erase(taskHandleId);
}

NAMESPACE_XBOX_HTTP_CLIENT_END
