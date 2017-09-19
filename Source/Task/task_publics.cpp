// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "task_impl.h"

using namespace xbox::httpclient;

bool HC_CALLING_CONV
HCTaskIsTaskPending()
{
    CONVERT_STD_EXCEPTION_RETURN(false,
        auto httpSingleton = get_http_singleton();
        auto& map = httpSingleton->m_taskPendingQueue;
        return !map.empty();
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCTaskSetCompleted(
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();
        http_task_queue_completed(taskHandle);
    );
}

HC_API bool HC_CALLING_CONV
HCTaskIsCompleted(
    _In_ HC_TASK_HANDLE taskHandleId
    )
{
    CONVERT_STD_EXCEPTION_RETURN(true,
        HC_TASK* taskHandle = http_task_get_task_from_handle_id(taskHandleId);
        if (taskHandle == nullptr)
            return true;

        return taskHandle->state == http_task_state::completed;
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCTaskProcessNextCompletedTask(_In_ uint64_t taskGroupId)
{
    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();

        HC_TASK* task = http_task_get_next_completed(taskGroupId);
        if (task == nullptr)
            return HC_OK;

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCTaskProcessNextCompletedTask: taskGroupId=%llu taskId=%llu",
            taskGroupId, task->id);

        http_task_process_completed(task);

        http_task_clear_task_from_handle_id(task->id);
    );
}

#if HC_USE_HANDLES
HC_API bool HC_CALLING_CONV
HCTaskWaitForCompleted(
    _In_ HC_TASK_HANDLE taskHandleId,
    _In_ uint32_t timeoutInMilliseconds
)
{
    CONVERT_STD_EXCEPTION_RETURN(true,
        HC_TASK* taskHandle = http_task_get_task_from_handle_id(taskHandleId);
        if (taskHandle == nullptr)
            return true; // already completed

        DWORD dwResult = WaitForSingleObject(taskHandle->resultsReady.get(), timeoutInMilliseconds);
        return (dwResult == WAIT_OBJECT_0);
    );
}


HANDLE HC_CALLING_CONV
HCTaskGetPendingHandle()
{
    auto httpSingleton = get_http_singleton();
    return httpSingleton->get_pending_ready_handle();
}

HANDLE HC_CALLING_CONV
HCTaskGetCompletedHandle(_In_ uint64_t taskGroupId)
{
    auto httpSingleton = get_http_singleton();
    return httpSingleton->get_task_completed_queue_for_taskgroup(taskGroupId)->get_complete_ready_handle();
}
#endif

HC_RESULT HC_CALLING_CONV
HCTaskProcessNextPendingTask()
{
    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();
        HC_TASK* task = http_task_get_next_pending();
        if (task == nullptr)
            return HC_OK;

        http_task_process_pending(task);
    );
}

HC_API HC_RESULT HC_CALLING_CONV
HCTaskCreate(
    _In_ uint64_t taskGroupId,
    _In_ HC_TASK_EXECUTE_FUNC executionRoutine,
    _In_opt_ void* executionRoutineContext,
    _In_ HC_TASK_WRITE_RESULTS_FUNC writeResultsRoutine,
    _In_opt_ void* writeResultsRoutineContext,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext,
    _Out_opt_ HC_TASK_HANDLE* taskHandle
    )
{
    CONVERT_STD_EXCEPTION(
        auto httpSingleton = get_http_singleton();

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
            task->taskGroupId = taskGroupId;
            task->id = httpSingleton->m_lastId++;

            HC_TRACE_INFORMATION(HTTPCLIENT, "HCTaskCreate: taskGroupId=%llu taskId=%llu", taskGroupId, task->id);

            http_task_store_task_from_handle_id(std::move(task));
        }

        http_task_queue_pending(pTask);

        if (taskHandle != nullptr)
        {
            *taskHandle = pTask->id;
        }
    );
}

