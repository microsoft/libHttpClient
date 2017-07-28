// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

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

bool HC_CALLING_CONV
HCTaskIsTaskPending()
{
    auto& map = get_http_singleton()->m_taskPendingQueue;
    return !map.empty();
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

HC_API void HC_CALLING_CONV
HCTaskSetCompleted(
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    http_task_queue_completed(taskHandle);
}

HC_API bool HC_CALLING_CONV
HCTaskIsCompleted(
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    return taskHandle->state == http_task_state::completed;
}

void HC_CALLING_CONV
HCTaskProcessNextCompletedTask(_In_ uint64_t taskGroupId)
{
    std::shared_ptr<HC_TASK> task = http_task_get_next_completed(taskGroupId);
    if (task == nullptr)
        return;

    LOGS_INFO << L"HCTaskProcessNextCompletedTask: taskGroupId=" << taskGroupId << " taskId=" << task->id;
    http_task_process_completed(task);
}

#if UWP_API || UNITTEST_API
HC_API void HC_CALLING_CONV
HCTaskWaitForCompleted(
    _In_ HC_TASK_HANDLE taskHandle,
    _In_ uint32_t timeoutInMilliseconds
)
{
    WaitForSingleObject(taskHandle->resultsReady.get(), timeoutInMilliseconds);
}


HANDLE HC_CALLING_CONV
HCTaskGetPendingHandle()
{
    return get_http_singleton()->get_pending_ready_handle();
}

HANDLE HC_CALLING_CONV
HCTaskGetCompletedHandle(_In_ uint64_t taskGroupId)
{
    return get_http_singleton()->get_task_completed_queue_for_taskgroup(taskGroupId)->get_complete_ready_handle();
}
#endif

void HC_CALLING_CONV
HCTaskProcessNextPendingTask()
{
    std::shared_ptr<HC_TASK> task = http_task_get_next_pending();
    if (task == nullptr)
        return;

    http_task_process_pending(task);
}

HC_API HC_TASK_HANDLE HC_CALLING_CONV
HCTaskCreate(
    _In_ uint64_t taskGroupId,
    _In_ HC_TASK_FUNC executionRoutine,
    _In_opt_ void* executionRoutineContext,
    _In_ HC_TASK_FUNC writeResultsRoutine,
    _In_opt_ void* writeResultsRoutineContext,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext,
    _In_ bool executeNow
    )
{
    std::shared_ptr<HC_TASK> task = std::make_shared<HC_TASK>();
#if UWP_API || UNITTEST_API
    task->resultsReady.set(CreateEvent(NULL, FALSE, FALSE, NULL));
#endif
    task->executionRoutine = executionRoutine;
    task->executionRoutineContext = executionRoutineContext;
    task->writeResultsRoutine = writeResultsRoutine;
    task->writeResultsRoutineContext = writeResultsRoutineContext;
    task->completionRoutine = completionRoutine;
    task->completionRoutineContext = completionRoutineContext;
    task->taskGroupId = taskGroupId;
    task->id = get_http_singleton()->m_lastHttpCallId++;
    LOGS_INFO << L"HCTaskCreate: taskGroupId=" << taskGroupId << " taskId=" << task->id;
    if (executeNow)
    {
        http_task_process_pending(task);
    }
    else
    {
        http_task_queue_pending(task);
    }


    return task.get();
}

