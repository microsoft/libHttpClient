// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "task_impl.h"

using namespace xbox::httpclient;

HC_API HC_RESULT HC_CALLING_CONV
HCAddTaskEventHandler(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_opt_ void* context,
    _In_opt_ HC_TASK_EVENT_FUNC taskEventFunc,
    _Out_opt_ HC_TASK_EVENT_HANDLE* eventHandle
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    HC_TASK_EVENT_HANDLE handleId = ++httpSingleton->m_lastId;

    HC_TASK_EVENT_FUNC_NODE node = { taskEventFunc, context, taskSubsystemId };

    {
        std::lock_guard<std::mutex> guard(httpSingleton->m_taskEventListLock);
        httpSingleton->m_taskEventFuncList[handleId] = node;
    }

    if (eventHandle != nullptr)
    {
        *eventHandle = handleId;
    }
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCRemoveTaskEventHandler(
    _Out_ HC_TASK_EVENT_HANDLE eventHandle
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    std::lock_guard<std::mutex> guard(httpSingleton->m_taskEventListLock);
    httpSingleton->m_taskEventFuncList.erase(eventHandle);
    return HC_OK;
}
CATCH_RETURN()

bool HC_CALLING_CONV
HCTaskIsTaskPending()
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return false;

    auto& map = httpSingleton->m_taskPendingQueue;
    return !map.empty();
}
CATCH_RETURN_WITH(false)

HC_API HC_RESULT HC_CALLING_CONV
HCTaskSetCompleted(
    _In_ HC_TASK_HANDLE taskHandle
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    http_task_queue_completed(taskHandle);
    return HC_OK;
}
CATCH_RETURN()

HC_API bool HC_CALLING_CONV
HCTaskIsCompleted(
    _In_ HC_TASK_HANDLE taskHandleId
    ) HC_NOEXCEPT
try
{
    HC_TASK* taskHandle = http_task_get_task_from_handle_id(taskHandleId);
    if (taskHandle == nullptr)
        return true;

    return taskHandle->state == http_task_state::completed;
}
CATCH_RETURN_WITH(true)

HC_API HC_SUBSYSTEM_ID HC_CALLING_CONV
HCTaskGetSubsystemId(
    _In_ HC_TASK_HANDLE taskHandleId
    ) HC_NOEXCEPT
try
{
    HC_TASK* taskHandle = http_task_get_task_from_handle_id(taskHandleId);
    if (taskHandle == nullptr)
        return HC_SUBSYSTEM_ID_GAME;

    return taskHandle->taskSubsystemId;
}
CATCH_RETURN_WITH(HC_SUBSYSTEM_ID_GAME)

HC_API uint64_t HC_CALLING_CONV
HCTaskGetTaskGroupId(
    _In_ HC_TASK_HANDLE taskHandleId
    ) HC_NOEXCEPT
try
{
    HC_TASK* taskHandle = http_task_get_task_from_handle_id(taskHandleId);
    if (taskHandle == nullptr)
        return 0;

    return taskHandle->taskGroupId;
}
CATCH_RETURN_WITH(0)

HC_API uint64_t HC_CALLING_CONV
HCTaskGetCompletedTaskQueueSize(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    std::lock_guard<std::mutex> guard(httpSingleton->m_taskLock);
    auto& completedQueue = httpSingleton->get_task_completed_queue_for_taskgroup(taskSubsystemId, taskGroupId)->get_completed_queue();
    return completedQueue.size();
}
CATCH_RETURN_WITH(0)

HC_API uint64_t HC_CALLING_CONV
HCTaskGetPendingTaskQueueSize(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    std::lock_guard<std::mutex> guard(httpSingleton->m_taskLock);
    auto& taskPendingQueue = httpSingleton->get_task_pending_queue(taskSubsystemId);
    return taskPendingQueue.size();
}
CATCH_RETURN_WITH(0)

HC_API HC_RESULT HC_CALLING_CONV
HCTaskProcessNextCompletedTask(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    HC_TASK* task = http_task_get_next_completed(taskSubsystemId, taskGroupId);
    if (task == nullptr)
        return HC_OK;

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCTaskProcessNextCompletedTask: taskSubsystemId=%llu taskGroupId=%llu taskId=%llu",
        taskSubsystemId, taskGroupId, task->id);

    http_task_process_completed(task);

    http_task_clear_task_from_handle_id(task->id);
    return HC_OK;
}
CATCH_RETURN()

#if HC_USE_HANDLES
HC_API bool HC_CALLING_CONV
HCTaskWaitForCompleted(
    _In_ HC_TASK_HANDLE taskHandleId,
    _In_ uint32_t timeoutInMilliseconds
    ) HC_NOEXCEPT
try
{
    HC_TASK* taskHandle = http_task_get_task_from_handle_id(taskHandleId);
    if (taskHandle == nullptr)
        return true; // already completed

    DWORD dwResult = WaitForSingleObject(taskHandle->eventTaskCompleted.get(), timeoutInMilliseconds);
    return (dwResult == WAIT_OBJECT_0);
}
CATCH_RETURN_WITH(true)

HANDLE HC_CALLING_CONV
HCTaskGetPendingHandle()
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return nullptr;

    return httpSingleton->get_pending_ready_handle();
}

HANDLE HC_CALLING_CONV
HCTaskGetCompletedHandle(_In_ HC_SUBSYSTEM_ID taskSubsystemId, _In_ uint64_t taskGroupId)
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return nullptr;

    return httpSingleton->get_task_completed_queue_for_taskgroup(taskSubsystemId, taskGroupId)->get_complete_ready_handle();
}
#endif

HC_RESULT HC_CALLING_CONV
HCTaskProcessNextPendingTask(_In_ HC_SUBSYSTEM_ID taskSubsystemId) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(true);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    HC_TASK* task = http_task_get_next_pending(taskSubsystemId);
    if (task == nullptr)
        return HC_OK;

    http_task_process_pending(task);
    return HC_OK;
}
CATCH_RETURN()

HC_API HC_RESULT HC_CALLING_CONV
HCTaskCreate(
    _In_ HC_SUBSYSTEM_ID taskSubsystemId,
    _In_ uint64_t taskGroupId,
    _In_opt_ HC_TASK_EXECUTE_FUNC executionRoutine,
    _In_opt_ void* executionRoutineContext,
    _In_opt_ HC_TASK_WRITE_RESULTS_FUNC writeResultsRoutine,
    _In_opt_ void* writeResultsRoutineContext,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext,
    _Out_opt_ HC_TASK_HANDLE* taskHandle
    ) HC_NOEXCEPT
try
{
    auto httpSingleton = get_http_singleton(false);
    if (nullptr == httpSingleton)
        return HC_E_NOTINITIALISED;

    HC_TASK* pTask = nullptr;

    {
        HC_TASK_PTR task = http_allocate_unique<HC_TASK>();

        pTask = task.get();
        task->executionRoutine = executionRoutine;
        task->executionRoutineContext = executionRoutineContext;
        task->writeResultsRoutine = writeResultsRoutine;
        task->writeResultsRoutineContext = writeResultsRoutineContext;
        task->completionRoutine = completionRoutine;
        task->completionRoutineContext = completionRoutineContext;
        task->taskSubsystemId = taskSubsystemId;
        task->taskGroupId = taskGroupId;
        task->id = httpSingleton->m_lastId++;

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCTaskCreate: taskGroupId=%llu taskId=%llu", taskGroupId, task->id);

        http_task_store_task_from_handle_id(std::move(task));
    }

    if (pTask->executionRoutine != nullptr)
    {
        http_task_queue_pending(pTask);
    }
    else
    {
        http_task_process_pending(pTask);
    }

    if (taskHandle != nullptr)
    {
        *taskHandle = pTask->id;
    }
    return HC_OK;
}
CATCH_RETURN()
