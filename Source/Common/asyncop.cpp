// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

void process_pending_async_op(_In_ std::shared_ptr<HC_ASYNC_INFO> info);

std::shared_ptr<HC_ASYNC_INFO>
http_asyncop_get_next_completed_async_op()
{
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_asyncLock);
    auto& completeQueue = get_http_singleton()->m_asyncCompleteQueue;
    if (!completeQueue.empty())
    {
        auto it = completeQueue.front();
        completeQueue.pop();
        return it;
    }
    return nullptr;
}

std::shared_ptr<HC_ASYNC_INFO>
http_asyncop_get_next_pending_async_op()
{
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_asyncLock);
    auto& pendingQueue = get_http_singleton()->m_asyncPendingQueue;
    if (!pendingQueue.empty())
    {
        auto it = pendingQueue.front();
        pendingQueue.pop();
        return it;
    }
    return nullptr;
}

void http_asyncop_push_pending_asyncop(
    _In_ std::shared_ptr<HC_ASYNC_INFO> info
    )
{
    info->state = http_async_state::pending;
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_asyncLock);
    auto& asyncPendingQueue = get_http_singleton()->m_asyncPendingQueue;
    asyncPendingQueue.push(info);
    get_http_singleton()->set_async_op_pending_ready();
}

bool HC_CALLING_CONV
HCTaskIsTaskPending()
{
    auto& map = get_http_singleton()->m_asyncPendingQueue;
    return !map.empty();
}

void process_completed_async_op(_In_ std::shared_ptr<HC_ASYNC_INFO> info)
{
    info->writeResultsRoutine(
        info->writeResultsRoutineContext,
        info.get()
        );
}

void process_pending_async_op(_In_ std::shared_ptr<HC_ASYNC_INFO> info)
{
    info->state = http_async_state::processing;

    {
        std::lock_guard<std::mutex> guard(get_http_singleton()->m_asyncLock);
        auto& asyncProcessingQueue = get_http_singleton()->m_asyncProcessingQueue;
        asyncProcessingQueue.push_back(info);
    }

    info->executionRoutine(
        info->executionRoutineContext,
        info.get()
        );
}

void queue_completed_async_op(_In_ HC_TASK_HANDLE taskHandle)
{
    taskHandle->state = http_async_state::completed;
#if UWP_API || UNITTEST_API
    SetEvent(taskHandle->resultsReady.get());
#endif

    std::shared_ptr<HC_ASYNC_INFO> info = nullptr;
    {
        std::lock_guard<std::mutex> guard(get_http_singleton()->m_asyncLock);
        auto& asyncProcessingQueue = get_http_singleton()->m_asyncProcessingQueue;
        for (auto& it : asyncProcessingQueue)
        {
            if (it.get() == taskHandle)
            {
                info = it;
            }
        }

        asyncProcessingQueue.erase(std::remove(asyncProcessingQueue.begin(), asyncProcessingQueue.end(), info), asyncProcessingQueue.end());

        auto& completeQueue = get_http_singleton()->m_asyncCompleteQueue;
        completeQueue.push(info);
    }

    get_http_singleton()->set_async_op_complete_ready();
}

HC_API void HC_CALLING_CONV
HCTaskSetResultReady(
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    queue_completed_async_op(taskHandle);
}

HC_API bool HC_CALLING_CONV
HCTaskIsResultReady(
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    return taskHandle->state == http_async_state::completed;
}

void HC_CALLING_CONV
HCTaskProcessNextResultReadyTask(_In_ uint32_t taskGroupId)
{
    std::shared_ptr<HC_ASYNC_INFO> info = http_asyncop_get_next_completed_async_op();
    if (info == nullptr)
        return;

    process_completed_async_op(info);
}

#if UWP_API || UNITTEST_API
HC_API void HC_CALLING_CONV
HCTaskWaitForResultReady(
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
HCTaskGetCompletedHandle()
{
    return get_http_singleton()->get_complete_ready_handle();
}
#endif

void HC_CALLING_CONV
HCTaskProcessNextPendingTask()
{
    std::shared_ptr<HC_ASYNC_INFO> info = http_asyncop_get_next_pending_async_op();
    if (info == nullptr)
        return;

    process_pending_async_op(info);
}

HC_API HC_TASK_HANDLE HC_CALLING_CONV
HCTaskCreate(
    _In_ uint32_t taskGroupId,
    _In_ HC_ASYNC_OP_FUNC executionRoutine,
    _In_opt_ void* executionRoutineContext,
    _In_ HC_ASYNC_OP_FUNC writeResultsRoutine,
    _In_opt_ void* writeResultsRoutineContext,
    _In_opt_ void* completionRoutine,
    _In_opt_ void* completionRoutineContext,
    _In_ bool executeNow
    )
{
    std::shared_ptr<HC_ASYNC_INFO> info = std::make_shared<HC_ASYNC_INFO>();
#if UWP_API || UNITTEST_API
    info->resultsReady.set(CreateEvent(NULL, FALSE, FALSE, NULL));
#endif
    info->executionRoutine = executionRoutine;
    info->executionRoutineContext = executionRoutineContext;
    info->writeResultsRoutine = writeResultsRoutine;
    info->writeResultsRoutineContext = writeResultsRoutineContext;
    info->completionRoutine = completionRoutine;
    info->completionRoutineContext = completionRoutineContext;
    if (executeNow)
    {
        process_pending_async_op(info);
    }
    else
    {
        http_asyncop_push_pending_asyncop(info);
    }

    return info.get();
}

