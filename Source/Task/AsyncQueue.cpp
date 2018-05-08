// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.#include "stdafx.h"
#include "pch.h"
#include "async.h"
#include "asyncQueue.h"
#include "CriticalThread.h"

#include <mutex>

#if HC_PLATFORM_IS_MICROSOFT
#include "Callback.h"
#else
#include "Callback_STL.h"
#endif

static uint32_t const QUEUE_SIGNATURE = 0x41515545;
static uint64_t const INVALID_SHARE_ID = 0xFFFFFFFFFFFFFFFF;

#if !HC_PLATFORM_IS_MICROSOFT
using PTP_WORK = void*;
#endif

// Support for shared queues
static LIST_ENTRY s_sharedList;
static std::mutex s_sharedCs;
static std::once_flag s_onceFlag;

#define MAKE_SHARED_ID(threadId, workMode, completionMode) \
    (uint64_t)(((uint64_t)workMode) << 48 | ((uint64_t)completionMode) << 32 | threadId)

struct AsyncQueueCallbackSubmittedData
{
    async_queue_handle_t queue;
    AsyncQueueCallbackType type;
};

struct AsyncQueueCallbackSubmittedThunk
{
    void operator()(AsyncQueueCallbackSubmitted* callback, void* context, AsyncQueueCallbackSubmittedData* data)
    {
        callback(context, data->queue, data->type);
    }
};

typedef Callback<AsyncQueueCallbackSubmitted, AsyncQueueCallbackSubmittedData, AsyncQueueCallbackSubmittedThunk> SubmitCallback;

class Queue
{
public:

    Queue()
    {
        InitializeListHead(&m_queueHead);
    }

    ~Queue()
    {
        {
            std::lock_guard<std::mutex> lock(m_cs);
            PLIST_ENTRY listEntry = RemoveHeadList(&m_queueHead);

            while (listEntry != &m_queueHead)
            {
                QueueEntry* entry = CONTAINING_RECORD(listEntry, QueueEntry, entry);
                (*entry->refsPointer)--;
                delete entry;
                listEntry = RemoveHeadList(&m_queueHead);
            }
        }

        if (m_event != nullptr)
        {
#if HC_PLATFORM_IS_MICROSOFT
            CloseHandle(m_event);
#else
            assert(false);
#endif
        }

        if (m_apcThread != nullptr)
        {
#if HC_PLATFORM_IS_MICROSOFT
            CloseHandle(m_apcThread);
#else
            assert(false);
#endif
        }

        if (m_work != nullptr)
        {
#if HC_PLATFORM_IS_MICROSOFT
            WaitForThreadpoolWorkCallbacks(m_work, TRUE);
            CloseThreadpoolWork(m_work);
#else
            assert(false);
#endif
        }
    }

    HRESULT Initialize(async_queue_handle_t owner, AsyncQueueCallbackType type, AsyncQueueDispatchMode mode, SubmitCallback* submitCallback)
    {
        m_owner = owner;
        m_type = type;
        m_callbackSubmitted = submitCallback;
        m_dispatchMode = mode;

        switch (mode)
        {
        case AsyncQueueDispatchMode_Manual:
            // nothing
            break;

        case AsyncQueueDispatchMode_FixedThread:
#if HC_PLATFORM_IS_MICROSOFT
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
#if HC_PLATFORM_IS_MICROSOFT
            m_work = CreateThreadpoolWork(TPCallback, this, nullptr);
            RETURN_LAST_ERROR_IF_NULL(m_work);
#else
            RETURN_HR(E_NOTIMPL);
#endif
            break;
        }

#if HC_PLATFORM_IS_MICROSOFT
        m_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        RETURN_LAST_ERROR_IF_NULL(m_event);
#endif

        return S_OK;
    }

    HRESULT AppendItem(
        std::atomic<uint32_t>* refsPointer,
        AsyncQueueCallback* callback,
        void* context)
    {
        QueueEntry* entry = new (std::nothrow) QueueEntry;
        RETURN_IF_NULL_ALLOC(entry);

        entry->refsPointer = refsPointer;
        entry->callback = callback;
        entry->context = context;

        bool queueCallback;

        {
            std::lock_guard<std::mutex> lock(m_cs);
            InsertTailList(&m_queueHead, &entry->entry);
#if HC_PLATFORM_IS_MICROSOFT
            SetEvent(m_event);
#endif
            (*refsPointer)++;
            queueCallback = !m_isCallbackQueued;
            m_isCallbackQueued = true;
        }

        if (queueCallback)
        {
            switch (m_dispatchMode)
            {
            case AsyncQueueDispatchMode_Manual:
                // nothing
                break;

            case AsyncQueueDispatchMode_FixedThread:
#if HC_PLATFORM_IS_MICROSOFT
                if (QueueUserAPC(APCCallback, m_apcThread, (ULONG_PTR)this) == 0)
                {
                    HRESULT result = HRESULT_FROM_WIN32(GetLastError());
                    std::lock_guard<std::mutex> lock(m_cs);
                    (*refsPointer)--;
                    RemoveEntryList(&entry->entry);
                    m_isCallbackQueued = false;
                    delete entry;
                    return result;
                }
#else
                assert(false);
#endif
                break;

            case AsyncQueueDispatchMode_ThreadPool:
#if HC_PLATFORM_IS_MICROSOFT
                SubmitThreadpoolWork(m_work);
#else
                assert(false);
#endif
                break;
            }
        }

        AsyncQueueCallbackSubmittedData data;
        data.queue = m_owner;
        data.type = m_type;
        (void)m_callbackSubmitted->Invoke(&data);

        return S_OK;
    }

    bool IsEmpty()
    {
        std::lock_guard<std::mutex> lock(m_cs);
        bool empty = m_queueHead.Flink == &m_queueHead;

        if (empty)
        {
            empty = (m_processingCallback == 0);
        }

        return empty;
    }

    bool DrainOneItem(AsyncQueueDispatchMode dispatcher)
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
#if HC_PLATFORM_IS_MICROSOFT
                ResetEvent(m_event);
#endif
                if (dispatcher != AsyncQueueDispatchMode_Manual)
                {
                    m_isCallbackQueued = false;
                }
            }
        }

        if (entry != nullptr)
        {
            entry->callback(entry->context);
            (*entry->refsPointer)--;
            m_processingCallback--;
            delete entry;
        }

        return entry != nullptr;
    }

    void DrainQueue(AsyncQueueDispatchMode dispatcher)
    {
        while (DrainOneItem(dispatcher))
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
#if HC_PLATFORM_IS_MICROSOFT
                ResetEvent(m_event);
#endif
            }
        }

        if (isEmpty && m_dispatchMode == AsyncQueueDispatchMode_FixedThread)
        {
#if HC_PLATFORM_IS_MICROSOFT
            if (GetThreadId(m_apcThread) == GetCurrentThreadId())
            {
                SleepEx(0, TRUE);
            }
#else
            assert(false);
#endif
        }
    }

    bool Wait(uint32_t timeout)
    {
#if HC_PLATFORM_IS_MICROSOFT
        uint32_t w = WaitForSingleObject(m_event, timeout);
        return (w == WAIT_OBJECT_0);
#else
        assert(false);
        return true;
#endif
    }

private:

    async_queue_handle_t m_owner = nullptr;
    AsyncQueueCallbackType m_type = AsyncQueueCallbackType_Work;
    AsyncQueueDispatchMode m_dispatchMode = AsyncQueueDispatchMode_Manual;
    SubmitCallback* m_callbackSubmitted = nullptr;
    HANDLE m_apcThread = nullptr;
    PTP_WORK m_work = nullptr;
    HANDLE m_event = nullptr;
    bool m_isCallbackQueued = false;
    std::atomic<uint32_t> m_processingCallback{ 0 };
    std::mutex m_cs;
    LIST_ENTRY m_queueHead;

    struct QueueEntry
    {
        LIST_ENTRY entry;
        std::atomic<uint32_t>* refsPointer;
        void* context;
        AsyncQueueCallback* callback;
    };

#if HC_PLATFORM_IS_MICROSOFT
    static void CALLBACK APCCallback(ULONG_PTR context)
    {
        Queue* queue = (Queue*)context;
        queue->DrainQueue(AsyncQueueDispatchMode_FixedThread);
    }

    static void CALLBACK TPCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK)
    {
        // Prevent any callbacks from declaring this thread as time critical.
        (void)LockTimeCriticalThread();
        Queue* queue = (Queue*)context;
        queue->DrainQueue(AsyncQueueDispatchMode_ThreadPool);
    }
#endif
};

struct async_queue_t
{
    ~async_queue_t()
    {
        if (m_parent != nullptr)
        {
            CloseAsyncQueue(m_parent);
        }
        else
        {
            if (m_work != nullptr)
            {
                delete m_work;
            }

            if (m_completion != nullptr)
            {
                delete m_completion;
            }
        }
    }

    void Initialize(async_queue_t* parent)
    {
        m_work = parent->m_work;
        m_completion = parent->m_work;  // N.B. When creating a child queue its completions share the work queue
        m_parent = parent;
        m_parent->AddRef();
    }

    HRESULT Initialize(AsyncQueueDispatchMode workMode, AsyncQueueDispatchMode completionMode)
    {
        m_work = new (std::nothrow) Queue;
        RETURN_IF_NULL_ALLOC(m_work);
        RETURN_IF_FAILED(m_work->Initialize(this, AsyncQueueCallbackType_Work, workMode, &m_callbackSubmitted));

        m_completion = new (std::nothrow) Queue;
        RETURN_IF_NULL_ALLOC(m_completion);
        RETURN_IF_FAILED(m_completion->Initialize(this, AsyncQueueCallbackType_Completion, completionMode, &m_callbackSubmitted));

        return S_OK;
    }

    void AddRef()
    {
        m_refs++;
    }

    void Release()
    {
        if (m_refs.fetch_sub(1) == 0)
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
        AsyncQueueCallback* callback,
        void* context)
    {
        Queue* q = GetSide(type);
        return q->AppendItem(&m_refs, callback, context);
    }

    HRESULT AddCallback(
        _In_ void* context,
        _In_ AsyncQueueCallbackSubmitted* callback,
        _Out_ uint32_t* token)
    {
        return m_callbackSubmitted.Add(nullptr, context, callback, token);
    }

    void RemoveCallback(
        _In_ uint32_t token)
    {
        m_callbackSubmitted.Remove(token);
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
    async_queue_t* m_parent = nullptr;
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
    async_queue_t* parent = async_queue_t::GetQueue(parentQueue);
    if (parent == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }

    async_queue_t* aq = new (std::nothrow) async_queue_t;
    RETURN_IF_NULL_ALLOC(aq);

    aq->Initialize(parent);

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
#pragma warning(suppress:4309) // 'static_cast': truncation of constant value
    if (id == static_cast<uint32_t>(INVALID_SHARE_ID)) // truncate to 32bit
    {
        RETURN_HR(E_INVALIDARG);
    }

    EnsureSharedInitialization();
    std::lock_guard<std::mutex> lock(s_sharedCs);
    uint64_t queueId = MAKE_SHARED_ID(id, workerMode, completionMode);
    async_queue_t* q = async_queue_t::FindSharedQueue(queueId);

    if (q != nullptr)
    {
        ReferenceAsyncQueue(q);
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

    bool found = q->DrainOneItem(AsyncQueueDispatchMode_Manual);
    if (!found && timeoutInMs != 0)
    {
        found = q->Wait(timeoutInMs);
        if (found) q->DrainOneItem(AsyncQueueDispatchMode_Manual);
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
    _In_opt_ void* callbackContext,
    _In_ AsyncQueueCallback* callback)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }

    RETURN_HR(aq->AppendItem(type, callback, callbackContext));
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
STDAPI_(void) ReferenceAsyncQueue(
    _In_ async_queue_handle_t queue)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    aq->AddRef();
}

//
// Adds a callback that will be called when a new callback
// is submitted. The callback will be directly invoked when
// the call is submitted.
//
STDAPI AddAsyncQueueCallbackSubmitted(
    _In_ async_queue_handle_t queue,
    _In_opt_ void* context,
    _In_ AsyncQueueCallbackSubmitted* callback,
    _Out_ uint32_t* token)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }

    RETURN_HR(aq->AddCallback(context, callback, token));
}

//
// Removes a previously added callback.
//
STDAPI_(void) RemoveAsyncQueueCallbackSubmitted(
    _In_ async_queue_handle_t queue,
    _In_ uint32_t token)
{
    async_queue_t* aq = async_queue_t::GetQueue(queue);
    if (aq != nullptr)
    {
        aq->RemoveCallback(token);
    }
}
