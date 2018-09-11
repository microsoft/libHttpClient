// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.#include "stdafx.h"
#include "pch.h"
#include "WaitTimer.h"

static uint32_t const QUEUE_SIGNATURE = 0x41515545;
static uint64_t const INVALID_SHARE_ID = 0xFFFFFFFFFFFFFFFF;
static uint32_t const SUBMIT_CALLBACK_MAX = 32;

// Support for shared queues
static LIST_ENTRY s_sharedList;
static std::mutex s_sharedCs;
static std::once_flag s_onceFlag;

#define MAKE_SHARED_ID(threadId, workMode, completionMode) \
    (uint64_t)(((uint64_t)workMode) << 48 | ((uint64_t)completionMode) << 32 | threadId)

class SubmitCallback
{
public:

    SubmitCallback(_In_ async_queue_handle_t queue)
        : m_queue(queue)
        , m_callbackCount(0)
    {
    }

    HRESULT Register(_In_ void* context, _In_ AsyncQueueCallbackSubmitted* callback, _Out_ registration_token_t* token)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_callbackCount == SUBMIT_CALLBACK_MAX)
        {
            return E_OUTOFMEMORY;
        }

        m_callbacks[m_callbackCount].Token = ++m_nextToken;
        m_callbacks[m_callbackCount].Context = context;
        m_callbacks[m_callbackCount].Callback = callback;
        *token = m_callbacks[m_callbackCount].Token;
        m_callbackCount++;
        
        return S_OK;
    }

    void Unregister(_In_ registration_token_t token)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool shuffling = false;

        for(uint32_t idx = 0; idx < SUBMIT_CALLBACK_MAX - 1; idx++)
        {
            if (!shuffling && m_callbacks[idx].Token == token)
            {
                shuffling = true;
            }

            if (shuffling)
            {
                m_callbacks[idx] = m_callbacks[idx + 1];
            }
        }

        if (shuffling || m_callbacks[SUBMIT_CALLBACK_MAX - 1].Token == token)
        {
            m_callbackCount--;
        }
    }

    void Invoke(_In_ AsyncQueueCallbackType type)
    {
        CallbackRegistration callbacks[SUBMIT_CALLBACK_MAX];
        uint32_t callbackCount;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_callbackCount == 0)
            {
                return;
            }

            callbackCount = m_callbackCount;
            memcpy(callbacks, m_callbacks, sizeof(CallbackRegistration) * callbackCount);
        }

        for(uint32_t idx = 0; idx < callbackCount; idx++)
        {
            callbacks[idx].Callback(callbacks[idx].Context, m_queue, type);
        }
    }

private:

    struct CallbackRegistration
    {
        registration_token_t Token;
        void* Context;
        AsyncQueueCallbackSubmitted* Callback;
    };

    std::atomic<registration_token_t> m_nextToken;
    CallbackRegistration m_callbacks[SUBMIT_CALLBACK_MAX];
    uint32_t m_callbackCount;
    std::mutex m_mutex;
    async_queue_handle_t m_queue;
};

class Queue
{
public:

    Queue()
    {
        InitializeListHead(&m_queueHead);
        InitializeListHead(&m_pendingHead);
    }

    ~Queue()
    {
        m_timer.Cancel();

        {
            std::lock_guard<std::mutex> lock(m_cs);
            DrainQueue(&m_queueHead);
            DrainQueue(&m_pendingHead);
        }

#ifdef _WIN32

        if (m_apcThread != nullptr)
        {
            CloseHandle(m_apcThread);
        }

        if (m_work != nullptr)
        {
            WaitForThreadpoolWorkCallbacks(m_work, TRUE);
            CloseThreadpoolWork(m_work);
        }

#endif
    }

    HRESULT Initialize(AsyncQueueCallbackType type, AsyncQueueDispatchMode mode, SubmitCallback* submitCallback)
    {
        m_type = type;
        m_callbackSubmitted = submitCallback;
        m_dispatchMode = mode;

        RETURN_IF_FAILED(m_timer.Initialize(this, [](void* context)
        {
            Queue* pthis = static_cast<Queue*>(context);
            pthis->SubmitPendingCallback();
        }));

        switch (mode)
        {
        case AsyncQueueDispatchMode_Manual:
            // nothing
            break;

        case AsyncQueueDispatchMode_FixedThread:
#ifdef _WIN32
            RETURN_IF_WIN32_BOOL_FALSE(DuplicateHandle(
                GetCurrentProcess(),
                GetCurrentThread(),
                GetCurrentProcess(),
                &m_apcThread, 0,
                FALSE,
                DUPLICATE_SAME_ACCESS));
#else
            RETURN_HR(E_NOTIMPL);
#endif
            break;

        case AsyncQueueDispatchMode_ThreadPool:
#ifdef _WIN32
            m_work = CreateThreadpoolWork(TPCallback, this, nullptr);
            RETURN_LAST_ERROR_IF_NULL(m_work);
#else
            RETURN_HR(E_NOTIMPL);
#endif
            break;
        }

        return S_OK;
    }

    HRESULT AppendItem(
        std::atomic<uint32_t>* refsPointer,
        uint32_t waitMs,
        AsyncQueueCallback* callback,
        void* context)
    {
        QueueEntry* entry = new (std::nothrow) QueueEntry;
        RETURN_IF_NULL_ALLOC(entry);

        entry->refsPointer = refsPointer;
        entry->callback = callback;
        entry->context = context;
        (*entry->refsPointer)++;

        if (waitMs == 0)
        {
            entry->enqueueTime = 0;

            // Owns the entry -- on error will delete
            return AppendEntry(entry);
        }
        else
        {
            entry->enqueueTime = m_timer.GetAbsoluteTime(waitMs);

            std::lock_guard<std::mutex> lock(m_cs);
            InsertTailList(&m_pendingHead, &entry->entry);

            // If the entry's enqueue time is < our current time, 
            // update the timer.
            if (entry->enqueueTime < m_timerDue)
            {
                m_timerDue = entry->enqueueTime;
                m_timer.Start(entry->enqueueTime);
            }

            return S_OK;
        }
    }

    bool IsEmpty()
    {
        std::lock_guard<std::mutex> lock(m_cs);
        bool empty = m_queueHead.Flink == &m_queueHead;

        if (empty)
        {
            empty = m_pendingHead.Flink == &m_pendingHead;
        }

        if (empty)
        {
            empty = (m_processingCallback == 0);
        }

        return empty;
    }

    bool DrainOneItem()
    {
        QueueEntry* entry = nullptr;

        {
            std::lock_guard<std::mutex> lock(m_cs);
            PLIST_ENTRY listEntry = RemoveHeadList(&m_queueHead);

            if (listEntry != &m_queueHead)
            {
                entry = CONTAINING_RECORD(listEntry, QueueEntry, entry);
                m_processingCallback++;
            }
            else
            {
                m_hasItems = false;
                m_event.notify_all();
            }
        }

        if (entry != nullptr)
        {
            entry->callback(entry->context);
            (*entry->refsPointer)--;
            m_processingCallback--;
            delete entry;
            return true;
        }

        return entry != nullptr;
    }

    void DrainQueue()
    {
        while (DrainOneItem())
        {
            // Do nothing.
        }
    }

    void RemoveCallbacks(
        _In_ AsyncQueueCallback* searchCallback,
        _In_opt_ void* predicateContext,
        _In_ AsyncQueueRemovePredicate* removePredicate)
    {
        bool isEmpty;

        {
            std::lock_guard<std::mutex> lock(m_cs);
            PLIST_ENTRY entry = m_queueHead.Flink;

            while (entry != &m_queueHead)
            {
                QueueEntry* candidate = CONTAINING_RECORD(entry, QueueEntry, entry);
                entry = entry->Flink;

                if (candidate->callback == searchCallback && removePredicate(predicateContext, candidate->context))
                {
                    RemoveEntryList(&candidate->entry);
                    (*candidate->refsPointer)--;
                    delete candidate;
                }
            }

            isEmpty = m_queueHead.Flink == &m_queueHead;

            if (isEmpty)
            {
                m_hasItems = false;
                m_event.notify_all();
            }

            entry = m_pendingHead.Flink;
            bool removedPendingItem = false;

            while (entry != &m_pendingHead)
            {
                QueueEntry* candidate = CONTAINING_RECORD(entry, QueueEntry, entry);
                entry = entry->Flink;

                if (candidate->callback == searchCallback && removePredicate(predicateContext, candidate->context))
                {
                    RemoveEntryList(&candidate->entry);
                    (*candidate->refsPointer)--;
                    delete candidate;
                    removedPendingItem = true;
                }
            }

            if (removedPendingItem)
            {
                ScheduleNextPendingCallbackNoLock(nullptr);
            }
        }

#ifdef _WIN32
        if (isEmpty && 
            m_dispatchMode == AsyncQueueDispatchMode_FixedThread && 
            GetThreadId(m_apcThread) == GetCurrentThreadId())
        {
            SleepEx(0, TRUE);
        }
#endif
    }

    bool Wait(uint32_t timeout)
    {
        std::unique_lock<std::mutex> lock(m_cs);
        if (!m_hasItems)
        {
            m_event.wait_for(lock, std::chrono::milliseconds(timeout));
        }
        return m_hasItems;
    }

private:

    AsyncQueueCallbackType m_type = AsyncQueueCallbackType_Work;
    AsyncQueueDispatchMode m_dispatchMode = AsyncQueueDispatchMode_Manual;
    SubmitCallback* m_callbackSubmitted = nullptr;
    bool m_hasItems = false;
    std::atomic<uint32_t> m_processingCallback{ 0 };
    std::condition_variable m_event;
    std::mutex m_cs;
    LIST_ENTRY m_queueHead;
    LIST_ENTRY m_pendingHead;
    WaitTimer m_timer;
    uint64_t m_timerDue = UINT64_MAX;

#ifdef _WIN32
    HANDLE m_apcThread = nullptr;
    PTP_WORK m_work = nullptr;
#endif

    struct QueueEntry
    {
        LIST_ENTRY entry;
        std::atomic<uint32_t>* refsPointer;
        void* context;
        AsyncQueueCallback* callback;
        uint64_t enqueueTime;
    };

#ifdef _WIN32
    static void CALLBACK APCCallback(ULONG_PTR context)
    {
        Queue* queue = (Queue*)context;
        queue->DrainOneItem();
    }

    static void CALLBACK TPCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK)
    {
        // Prevent any callbacks from declaring this thread as time sensitive.
        ASYNC_LIB_LOCK_TIME_SENSITIVE_THREAD();
        Queue* queue = (Queue*)context;
        queue->DrainOneItem();
    }
#endif

    // Appends the given entry to the active queue.  The entry should already
    // be add-refd, and this API owns the lifetime of the entry.  If the
    // API fails, the entry will be released and deleted.
    HRESULT AppendEntry(
        QueueEntry* entry)
    {
        {
            std::lock_guard<std::mutex> lock(m_cs);
            InsertTailList(&m_queueHead, &entry->entry);
            m_hasItems = true;
            m_event.notify_all();
        }

        switch (m_dispatchMode)
        {
        case AsyncQueueDispatchMode_Manual:
            // nothing
            break;

        case AsyncQueueDispatchMode_FixedThread:
#ifdef _WIN32
            if (QueueUserAPC(APCCallback, m_apcThread, (ULONG_PTR)this) == 0)
            {
                HRESULT result = HRESULT_FROM_WIN32(GetLastError());
                std::lock_guard<std::mutex> lock(m_cs);
                (*entry->refsPointer)--;
                RemoveEntryList(&entry->entry);
                delete entry;
                return result;
            }
#else
            ASSERT(false);
#endif
            break;

        case AsyncQueueDispatchMode_ThreadPool:
#ifdef _WIN32
            SubmitThreadpoolWork(m_work);
#else
            ASSERT(false);
#endif
            break;
        }

        m_callbackSubmitted->Invoke(m_type);
        return S_OK;
    }

    void DrainQueue(PLIST_ENTRY head)
    {
        PLIST_ENTRY listEntry = RemoveHeadList(head);
        while (listEntry != head)
        {
            QueueEntry* entry = CONTAINING_RECORD(listEntry, QueueEntry, entry);
            (*entry->refsPointer)--;
            delete entry;
            listEntry = RemoveHeadList(head);
        }
    }

    // Examines the pending callback list, optionally popping the entry off the
    // list that matches m_timerDue, and schedules the timer for the next entry.
    // this assumes m_cs is held.
    void ScheduleNextPendingCallbackNoLock(_Out_opt_ QueueEntry** dueEntry)
    {
        QueueEntry* nextItem = nullptr;
        PLIST_ENTRY entry = m_pendingHead.Flink;

        if (dueEntry != nullptr)
        {
            *dueEntry = nullptr;
        }
        
        while(entry != &m_pendingHead)
        {
            QueueEntry* candidate = CONTAINING_RECORD(entry, QueueEntry, entry);
            entry = entry->Flink;
            if (dueEntry != nullptr && candidate->enqueueTime == m_timerDue)
            {
                RemoveEntryList(&candidate->entry);
                *dueEntry = candidate;
            }
            else if (nextItem == nullptr || nextItem->enqueueTime > candidate->enqueueTime)
            {
                nextItem = candidate;
            }
        }

        if (nextItem != nullptr)
        {
            m_timerDue = nextItem->enqueueTime;
            m_timer.Start(m_timerDue);
        }
        else
        {
            m_timerDue = UINT64_MAX;
            m_timer.Cancel();
        }
    }

    void SubmitPendingCallback()
    {
        QueueEntry* dueEntry;

        {
            std::lock_guard<std::mutex> lock(m_cs);
            ScheduleNextPendingCallbackNoLock(&dueEntry);
        }

        if (dueEntry != nullptr)
        {
            HRESULT hr = AppendEntry(dueEntry);
            if (FAILED(hr))
            {
                FAIL_FAST_MSG("Failed to append due entry: 0x%08x", hr);
            }
        }
    }
};

struct async_queue_t
{
    async_queue_t()
        : m_callbackSubmitted(this)
    {
    }

    ~async_queue_t()
    {
        if (m_workSource != nullptr)
        {
            CloseAsyncQueue(m_workSource);
        }
        else
        {
            if (m_work != nullptr)
            {
                delete m_work;
            }
        }

        if (m_completionSource != nullptr)
        {
            CloseAsyncQueue(m_completionSource);
        }
        else
        {
            if (m_completion != nullptr)
            {
                delete m_completion;
            }
        }
    }

    void Initialize(
        _In_ async_queue_t* workerSource,
        _In_ AsyncQueueCallbackType workerSourceCallbackType,
        _In_ async_queue_t* completionSource,
        _In_ AsyncQueueCallbackType completionSourceCallbackType)
    {
        Queue* work = workerSourceCallbackType == AsyncQueueCallbackType_Work ?
            workerSource->m_work : workerSource->m_completion;
        Queue* completion = completionSourceCallbackType == AsyncQueueCallbackType_Work ?
            completionSource->m_work : completionSource->m_completion;

        m_work = work;
        m_completion = completion;

        m_workSource = workerSource;
        m_workSource->AddRef();

        m_completionSource = completionSource;
        m_completionSource->AddRef();
    }

    HRESULT Initialize(AsyncQueueDispatchMode workMode, AsyncQueueDispatchMode completionMode)
    {
        m_work = new (std::nothrow) Queue;
        RETURN_IF_NULL_ALLOC(m_work);
        RETURN_IF_FAILED(m_work->Initialize(AsyncQueueCallbackType_Work, workMode, &m_callbackSubmitted));

        m_completion = new (std::nothrow) Queue;
        RETURN_IF_NULL_ALLOC(m_completion);
        RETURN_IF_FAILED(m_completion->Initialize(AsyncQueueCallbackType_Completion, completionMode, &m_callbackSubmitted));

        return S_OK;
    }

    void AddRef()
    {
        m_refs++;
    }

    void Release()
    {
        if (m_refs.fetch_sub(1) == 1)
        {
            if (m_shareId != INVALID_SHARE_ID)
            {
                std::lock_guard<std::mutex> lock(s_sharedCs);
                RemoveEntryList(&m_shareEntry);
                m_shareId = INVALID_SHARE_ID;
            }

            delete this;
        }
    }

    Queue* GetSide(_In_ AsyncQueueCallbackType type)
    {
        return type == AsyncQueueCallbackType_Work ? m_work : m_completion;
    }

    HRESULT AppendItem(
        AsyncQueueCallbackType type,
        uint32_t delayMs,
        AsyncQueueCallback* callback,
        void* context)
    {
        Queue* q = GetSide(type);
        return q->AppendItem(&m_refs, delayMs, callback, context);
    }

    HRESULT RegisterCallback(
        _In_opt_ void* context,
        _In_ AsyncQueueCallbackSubmitted* callback,
        _Out_ registration_token_t* token)
    {
        return m_callbackSubmitted.Register(context, callback, token);
    }

    void UnregisterCallback(
        _In_ registration_token_t token)
    {
        m_callbackSubmitted.Unregister(token);
    }

    void MakeShared(_In_ uint64_t shareId)
    {
        ASSERT(m_shareId == INVALID_SHARE_ID);
        m_shareId = shareId;
        InsertHeadList(&s_sharedList, &m_shareEntry);
    }

    static async_queue_t* GetQueue(_In_ async_queue_handle_t queue)
    {
        async_queue_t* aq = (async_queue_t*)queue;
        if (queue == nullptr || aq->m_signature != QUEUE_SIGNATURE)
        {
            ASSERT(false);
            aq = nullptr;
        }

        return aq;
    }

    static async_queue_t* FindSharedQueue(_In_ uint64_t id)
    {
        for (PLIST_ENTRY entry = s_sharedList.Flink; entry != &s_sharedList; entry = entry->Flink)
        {
            async_queue_t* queue = (async_queue_t*)CONTAINING_RECORD(entry, async_queue_t, m_shareEntry);
            if (queue->m_shareId == id)
            {
                return queue;
            }
        }
        return nullptr;
    }

private:

    uint32_t m_signature = QUEUE_SIGNATURE;
    std::atomic<uint32_t> m_refs{ 1 };
    uint64_t m_shareId = INVALID_SHARE_ID;
    LIST_ENTRY m_shareEntry;
    SubmitCallback m_callbackSubmitted;
    Queue* m_work = nullptr;
    Queue* m_completion = nullptr;
    async_queue_t* m_workSource = nullptr;
    async_queue_t* m_completionSource = nullptr;
};

static void EnsureSharedInitialization()
{
    std::call_once(s_onceFlag, []()
    {
        InitializeListHead(&s_sharedList);
    });
}

//
// Creates an Async Queue, which can be used to queue
// different async calls together.
//
STDAPI CreateAsyncQueue(
    _In_ AsyncQueueDispatchMode workDispatchMode,
    _In_ AsyncQueueDispatchMode completionDispatchMode,
    _Out_ async_queue_handle_t* queue)
{
    std::unique_ptr<async_queue_t> aq(new (std::nothrow) async_queue_t);
    RETURN_IF_NULL_ALLOC(aq);
    RETURN_IF_FAILED(aq->Initialize(workDispatchMode, completionDispatchMode));
    *queue = aq.release();

    return S_OK;
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
    async_queue_t* workerSource = async_queue_t::GetQueue(workerSourceQueue);
    async_queue_t* completionSource = async_queue_t::GetQueue(completionSourceQueue);
    if (workerSource == nullptr || completionSource == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }

    async_queue_t* aq = new (std::nothrow) async_queue_t;
    RETURN_IF_NULL_ALLOC(aq);

    aq->Initialize(workerSource, workerSourceCallbackType, completionSource, completionSourceCallbackType);

    *queue = aq;
    return S_OK;
}

//
// Creates a shared queue.  Queues with the same ID and
// dispatch modes can share a single instance.
//
STDAPI CreateSharedAsyncQueue(
    _In_ uint32_t id,
    _In_ AsyncQueueDispatchMode workerMode,
    _In_ AsyncQueueDispatchMode completionMode,
    _Out_ async_queue_handle_t* queue)
{
    EnsureSharedInitialization();
    std::lock_guard<std::mutex> lock(s_sharedCs);
    uint64_t queueId = MAKE_SHARED_ID(id, workerMode, completionMode);
    async_queue_t* q = async_queue_t::FindSharedQueue(queueId);

    if (q != nullptr)
    {
        q = DuplicateAsyncQueueHandle(q);
        *queue = q;
    }
    else
    {
        RETURN_IF_FAILED(CreateAsyncQueue(workerMode, completionMode, queue));
        (*queue)->MakeShared(queueId);
    }

    return S_OK;
}

//
// Processes items in the async queue of the given type. If an item
// is processed this will return TRUE. If there are no items to process
// this returns FALSE.  You can pass a timeout, which will cause
// DispatchAsyncQueue to wait for something to arrive in the queue.
//
STDAPI_(bool) DispatchAsyncQueue(
    _In_ async_queue_handle_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ uint32_t timeoutInMs)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq == nullptr)
    {
        return false;
    }

    Queue* q = aq->GetSide(type);

    bool found = q->DrainOneItem();
    if (!found && timeoutInMs != 0)
    {
        found = q->Wait(timeoutInMs);
        if (found) q->DrainOneItem();
    }

    return found;
}

//
// Returns TRUE if there is no outstanding work in this
// queue.
//
STDAPI_(bool) IsAsyncQueueEmpty(
    _In_ async_queue_handle_t queue,
    _In_ AsyncQueueCallbackType type)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq == nullptr)
    {
        return false;
    }

    return aq->GetSide(type)->IsEmpty();
}

//
// Closes the async queue.  A queue can only be closed if it
// is not in use by an async api or is empty.  If not true, the queue
// will be marked for closure and closed when it can. 
//
STDAPI_(void) CloseAsyncQueue(
    _In_ async_queue_handle_t queue)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq != nullptr)
    {
        aq->Release();
    }
}

//
// Submits either a work or completion callback.
//
STDAPI SubmitAsyncCallback(
    _In_ async_queue_handle_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ uint32_t delayMs,
    _In_opt_ void* callbackContext,
    _In_ AsyncQueueCallback* callback)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }

    RETURN_HR(aq->AppendItem(type, delayMs, callback, callbackContext));
}

/// <summary>
/// Walks all callbacks in the queue of the given type and,
/// if their callback and context object are equal, invokes
/// the predicate callback and removes the callback from the 
/// queue.  This should be called before an object is deleted
/// to ensure there are no orphan callbacks in the async queue
/// that could later call back into the deleted object.
/// </summary>
STDAPI_(void) RemoveAsyncQueueCallbacks(
    _In_ async_queue_handle_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ AsyncQueueCallback* searchCallback,
    _In_opt_ void* predicateContext,
    _In_ AsyncQueueRemovePredicate* removePredicate)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq != nullptr)
    {
        Queue* q = aq->GetSide(type);
        q->RemoveCallbacks(searchCallback, predicateContext, removePredicate);
    }
}

//
// Increments the refcount on the queue
//
STDAPI_(async_queue_handle_t) DuplicateAsyncQueueHandle(
    _In_ async_queue_handle_t queue)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    aq->AddRef();
    return queue;
}

//
// Registers a callback that will be called when a new callback
// is submitted. The callback will be directly invoked when
// the call is submitted.
//
STDAPI RegisterAsyncQueueCallbackSubmitted(
    _In_ async_queue_handle_t queue,
    _In_opt_ void* context,
    _In_ AsyncQueueCallbackSubmitted* callback,
    _Out_ registration_token_t* token)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }

    RETURN_HR(aq->RegisterCallback(context, callback, token));
}

//
// Unregisters a previously added callback.
//
STDAPI_(void) UnregisterAsyncQueueCallbackSubmitted(
    _In_ async_queue_handle_t queue,
    _In_ registration_token_t token)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq != nullptr)
    {
        aq->UnregisterCallback(token);
    }
}
