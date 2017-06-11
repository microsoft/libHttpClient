// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpClient/types.h"
#include "httpClient/httpClient.h"
#include "singleton.h"
#include "mem.h"

std::shared_ptr<http_async_info>
http_asyncop_get_next_async_op()
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

void http_asyncop_set_info_in_new_handle(
    _In_ std::shared_ptr<http_async_info> info,
    _In_ void* completionRoutineContext,
    _In_ void* completionRoutine
    )
{
    std::lock_guard<std::mutex> guard(get_http_singleton()->m_asyncLock);
    auto& asyncQueue = get_http_singleton()->m_asyncPendingQueue;
    info->state = http_async_state::pending;
    info->completionRoutineContext = completionRoutineContext;
    info->completionRoutine = completionRoutine;
    asyncQueue.push(info);

    get_http_singleton()->m_threadPool->set_async_op_ready();
}

bool HC_CALLING_CONV
HttpClientIsAsyncOpPending()
{
    auto& map = get_http_singleton()->m_asyncPendingQueue;
    return !map.empty();
}

void HC_CALLING_CONV
HCThreadProcessPendingAsyncOp()
{
    std::shared_ptr<http_async_info> info = http_asyncop_get_next_async_op();
    if (info == nullptr)
        return;

    info->state = http_async_state::processing;
    info->executeRoutine(info);
    info->state = http_async_state::completed;
}

