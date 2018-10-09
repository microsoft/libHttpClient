// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.#include "stdafx.h"
#include "pch.h"
#include <httpClient/asyncQueueEx.h>
#include "asyncQueueP.h"
#include "AsyncQueueImpl.h"

static uint64_t const INVALID_SHARE_ID = 0xFFFFFFFFFFFFFFFF;

static LIST_ENTRY s_sharedList;
static std::mutex s_sharedLock;
static std::once_flag s_sharedOnceFlag;

#define MAKE_SHARED_ID(threadId, workMode, completionMode) \
    (uint64_t)(((uint64_t)workMode) << 48 | ((uint64_t)completionMode) << 32 | threadId)

class AsyncQueueEx : public AsyncQueue
{
public:

    AsyncQueueEx()
        : AsyncQueue()
    {
    }
    
    virtual ~AsyncQueueEx()
    {
        if (m_shareId != INVALID_SHARE_ID)
        {
            std::lock_guard<std::mutex> lock(s_sharedLock);
            RemoveEntryList(&m_shareEntry);
            m_shareId = INVALID_SHARE_ID;
        }
    }
    
    void InitializeEx(
        _In_ IAsyncQueue* workerSource,
        _In_ AsyncQueueCallbackType workerSourceCallbackType,
        _In_ IAsyncQueue* completionSource,
        _In_ AsyncQueueCallbackType completionSourceCallbackType)
    {
        HRESULT hr = workerSource->GetSection(workerSourceCallbackType, m_work.address_of());
        if (FAILED(hr))
        {
            FAIL_FAST_MSG("Unexpected failure getting worker queue");
        }
        
        hr = completionSource->GetSection(completionSourceCallbackType, m_completion.address_of());
        if (FAILED(hr))
        {
            FAIL_FAST_MSG("Unexpected failure getting worker queue");
        }

        m_workSource = referenced_ptr<IAsyncQueue>(workerSource);
        m_completionSource = referenced_ptr<IAsyncQueue>(completionSource);
    }

    void MakeShared(_In_ uint64_t shareId)
    {
        ASSERT(m_shareId == INVALID_SHARE_ID);
        m_shareId = shareId;
        InsertHeadList(&s_sharedList, &m_shareEntry);
    }

    static AsyncQueueEx* FindSharedQueue(_In_ uint64_t id)
    {
        for (PLIST_ENTRY entry = s_sharedList.Flink; entry != &s_sharedList; entry = entry->Flink)
        {
            AsyncQueueEx* queue = CONTAINING_RECORD(entry, AsyncQueueEx, m_shareEntry);
            if (queue->m_shareId == id)
            {
                return queue;
            }
        }
        
        return nullptr;
    }

private:
    
    uint64_t m_shareId = INVALID_SHARE_ID;
    LIST_ENTRY m_shareEntry;
    referenced_ptr<IAsyncQueue> m_workSource;
    referenced_ptr<IAsyncQueue> m_completionSource;
};

static void EnsureSharedInitialization()
{
    std::call_once(s_sharedOnceFlag, []()
    {
        InitializeListHead(&s_sharedList);
    });
}

/// <summary>
/// Creates an async queue suitable for invoking child tasks.
/// A nested queue dispatches its work through the parent
/// queue.  Both work and completions are dispatched through
/// the parent as "work" callback types.  A nested queue is useful
/// for performing intermediate work within a larger task.
/// </summary>
STDAPI CreateNestedAsyncQueue(
    _In_ async_queue_handle_t parentQueue,
    _Out_ async_queue_handle_t* queue)
{
    return CreateCompositeAsyncQueue(
        parentQueue,
        AsyncQueueCallbackType_Work,
        parentQueue,
        AsyncQueueCallbackType_Work,
        queue);
}

/// <summary>
/// Creates an async queue by composing elements of 2 other queues.
/// </summary>
STDAPI CreateCompositeAsyncQueue(
    _In_ async_queue_handle_t workerSourceQueue,
    _In_ AsyncQueueCallbackType workerSourceCallbackType,
    _In_ async_queue_handle_t completionSourceQueue,
    _In_ AsyncQueueCallbackType completionSourceCallbackType,
    _Out_ async_queue_handle_t* queue)
{
    referenced_ptr<IAsyncQueue> workerSource(GetQueue(workerSourceQueue));
    referenced_ptr<IAsyncQueue> completionSource(GetQueue(completionSourceQueue));
    
    if (workerSource == nullptr || completionSource == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }

    referenced_ptr<AsyncQueueEx> aq(new (std::nothrow) AsyncQueueEx);
    RETURN_IF_NULL_ALLOC(aq);

    aq->InitializeEx(workerSource.get(), workerSourceCallbackType, completionSource.get(), completionSourceCallbackType);

    *queue = aq.release()->GetHandle();
    return S_OK;
}

//
// Creates a shared queue.  Queues with the same ID and
// dispatch modes can share a single instance.
//
STDAPI CreateSharedAsyncQueue(
    _In_ uint32_t id,
    _In_ AsyncQueueDispatchMode workDispatchMode,
    _In_ AsyncQueueDispatchMode completionDispatchMode,
    _Out_ async_queue_handle_t* queue)
{
    EnsureSharedInitialization();
    uint64_t queueId = MAKE_SHARED_ID(id, workDispatchMode, completionDispatchMode);
    referenced_ptr<AsyncQueueEx> aq;

    {
        std::lock_guard<std::mutex> lock(s_sharedLock);
        aq = referenced_ptr<AsyncQueueEx>(AsyncQueueEx::FindSharedQueue(queueId));
    }

    if (aq != nullptr)
    {
        RETURN_IF_FAILED(DuplicateAsyncQueueHandle(aq->GetHandle(), queue));
    }
    else
    {
        aq = referenced_ptr<AsyncQueueEx>(new (std::nothrow) AsyncQueueEx);
        RETURN_IF_NULL_ALLOC(aq);
        RETURN_IF_FAILED(aq->Initialize(workDispatchMode, completionDispatchMode));

        std::unique_lock<std::mutex> lock(s_sharedLock);
        referenced_ptr<AsyncQueueEx> aq2(AsyncQueueEx::FindSharedQueue(queueId));
        if (aq2 != nullptr)
        {
            // We raced with someone else
            lock.unlock();
            RETURN_IF_FAILED(DuplicateAsyncQueueHandle(aq2->GetHandle(), queue));
        }
        else
        {
            aq->MakeShared(queueId);
            *queue = aq.release()->GetHandle();
        }
    }

    return S_OK;
}

