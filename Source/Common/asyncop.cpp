// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

std::shared_ptr<HC_TASK>
http_task_get_next_completed(_In_ uint64_t taskGroupId)
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

std::shared_ptr<HC_TASK>
http_task_get_next_pending()
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

void http_task_queue_pending(
    _In_ std::shared_ptr<HC_TASK> task
    )
{
    task->state = http_task_state::pending;
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
    auto& taskPendingQueue = get_http_singleton()->m_taskPendingQueue;
    taskPendingQueue.push(task);
    LOGS_INFO << L"Task queue pending: queueSize=" << taskPendingQueue.size() << " taskId=" << task->id;

    get_http_singleton()->set_task_pending_ready();
}

void http_task_process_completed(_In_ std::shared_ptr<HC_TASK> task)
{
    task->writeResultsRoutine(
        task->writeResultsRoutineContext,
        task.get()
        );
}

void http_task_process_pending(_In_ std::shared_ptr<HC_TASK> task)
{
    task->state = http_task_state::processing;

    {
        std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
        auto& taskExecutingQueue = get_http_singleton()->m_taskExecutingQueue;
        taskExecutingQueue.push_back(task);
        LOGS_INFO << L"Task execute: executeQueueSize=" << taskExecutingQueue.size() << " taskId=" << task->id;
    }

    task->executionRoutine(
        task->executionRoutineContext,
        task.get()
        );
}

void http_task_queue_completed(_In_ HC_TASK_HANDLE taskHandle)
{
    taskHandle->state = http_task_state::completed;

    std::shared_ptr<HC_TASK> task = nullptr;
    {
        std::lock_guard<std::mutex> guard(get_http_singleton()->m_taskLock);
        auto& taskProcessingQueue = get_http_singleton()->m_taskExecutingQueue;
        for (auto& it : taskProcessingQueue)
        {
            if (it.get() == taskHandle)
            {
                task = it;
            }
        }

        taskProcessingQueue.erase(std::remove(taskProcessingQueue.begin(), taskProcessingQueue.end(), task), taskProcessingQueue.end());

        auto& taskCompletedQueue = get_http_singleton()->get_task_completed_queue_for_taskgroup(taskHandle->taskGroupId)->get_completed_queue();
        taskCompletedQueue.push(task);
        LOGS_INFO << L"Task queue completed: queueSize=" << taskCompletedQueue.size() << " taskGroupId=" << taskHandle->taskGroupId;
    }

#if UWP_API || UNITTEST_API
    SetEvent(taskHandle->resultsReady.get());
#endif
    get_http_singleton()->get_task_completed_queue_for_taskgroup(taskHandle->taskGroupId)->set_task_completed_event();
}

NAMESPACE_XBOX_HTTP_CLIENT_END
