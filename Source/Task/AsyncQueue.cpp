// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.#include "stdafx.h"
#include "pch.h"
#include "httpClient/async.h"
#include "httpClient/asyncQueue.h"
#include "CallbackLib.h"

#define QUEUE_SIGNATURE 0x41515545

#define SHARED_UNINITIALIZED 0x0
#define SHARED_INITIALIZING  0x1
#define SHARED_INITIALIZED   0x2

#define INVALID_SHARE_ID     0xFFFFFFFFFFFFFFFF

// Support for shared queues
static LIST_ENTRY _sharedList;
static CRITICAL_SECTION _sharedCs;
static DWORD _sharedInit = SHARED_UNINITIALIZED;

#define MAKE_SHARED_ID(threadId, workMode, completionMode) \
    (uint64_t)(((uint64_t)workMode) << 48 | ((uint64_t)completionMode) << 32 | threadId)

struct AsyncQueueCallbackSubmittedData
{
    async_queue_t queue;
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

struct QueueEntry
{
    LIST_ENTRY Entry;
    uint32_t* RefsPointer;
    void* Context;
    AsyncQueueCallback* Callback;
};

class Queue
{
public:

    Queue()
        : ApcThread(nullptr)
        , Event(nullptr)
        , Work(nullptr)
        , Owner(nullptr)
        , Type(AsyncQueueCallbackType_Work)
        , DispatchMode(AsyncQueueDispatchMode_Manual)
        , CallbackSubmitted(nullptr)
        , IsApcQueued(false)
    {
    }

    ~Queue()
    {
        EnterCriticalSection(&Cs);

        PLIST_ENTRY listEntry = RemoveHeadList(&QueueHead);

        while (listEntry != &QueueHead)
        {
            QueueEntry* entry = CONTAINING_RECORD(listEntry, QueueEntry, Entry);
            InterlockedDecrement(entry->RefsPointer);
            delete entry;
            listEntry = RemoveHeadList(&QueueHead);
        }

        LeaveCriticalSection(&Cs);
        DeleteCriticalSection(&Cs);

        if (Event != nullptr)
        {
            CloseHandle(Event);
        }

        if (ApcThread != nullptr)
        {
            CloseHandle(ApcThread);
        }

        if (Work != nullptr)
        {
            CloseThreadpoolWork(Work);
        }
    }

    HRESULT Initialize(async_queue_t owner, AsyncQueueCallbackType type, AsyncQueueDispatchMode mode, SubmitCallback* submitCallback)
    {
        Owner = owner;
        Type = type;
        CallbackSubmitted = submitCallback;
        DispatchMode = mode;

        InitializeCriticalSection(&Cs);
        InitializeListHead(&QueueHead);

        switch (mode)
        {
        case AsyncQueueDispatchMode_FixedThread:
            if (!DuplicateHandle(
                GetCurrentProcess(), 
                GetCurrentThread(), 
                GetCurrentProcess(), 
                &ApcThread, 0, 
                FALSE, 
                DUPLICATE_SAME_ACCESS))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
            break;

        case AsyncQueueDispatchMode_ThreadPool:
            Work = CreateThreadpoolWork(TPCallback, this, nullptr);
            if (Work == nullptr)
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }
        }

        Event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (Event == nullptr)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        return S_OK;
    }

    HRESULT AppendItem(
        uint32_t* refsPointer,
        AsyncQueueCallback* callback,
        void* context)
    {
        QueueEntry* entry = new (std::nothrow) QueueEntry;
        if (entry == nullptr) return E_OUTOFMEMORY;
        entry->RefsPointer = refsPointer;
        entry->Callback = callback;
        entry->Context = context;

        EnterCriticalSection(&Cs);
        InsertTailList(&QueueHead, &entry->Entry);
        SetEvent(Event);
        InterlockedIncrement(refsPointer);
        bool queueApc = !IsApcQueued;
        IsApcQueued = true;
        LeaveCriticalSection(&Cs);

        switch (DispatchMode)
        {
        case AsyncQueueDispatchMode_FixedThread:
            if (queueApc && QueueUserAPC(APCCallback, ApcThread, (ULONG_PTR)this) == 0)
            {
                HRESULT result = HRESULT_FROM_WIN32(GetLastError());
                InterlockedDecrement(refsPointer);
                EnterCriticalSection(&Cs);
                RemoveEntryList(&entry->Entry);
                IsApcQueued = false;
                LeaveCriticalSection(&Cs);
                delete entry;
                return result;
            }
            break;

        case AsyncQueueDispatchMode_ThreadPool:
            SubmitThreadpoolWork(Work);
            break;
        }

        AsyncQueueCallbackSubmittedData data;
        data.queue = Owner;
        data.type = Type;
        CallbackSubmitted->Invoke(&data);

        return S_OK;
    }

    bool IsEmpty()
    {
        EnterCriticalSection(&Cs);
        bool empty = QueueHead.Flink == &QueueHead;
        LeaveCriticalSection(&Cs);
        return empty;
    }

    bool DrainOneItem(bool insideApc)
    {
        QueueEntry* entry = nullptr;

        EnterCriticalSection(&Cs);
        PLIST_ENTRY listEntry = RemoveHeadList(&QueueHead);

        if (listEntry != &QueueHead)
        {
            entry = CONTAINING_RECORD(listEntry, QueueEntry, Entry);
        }
        else
        {
            ResetEvent(Event);
            if (insideApc)
            {
                IsApcQueued = false;
            }
        }
        LeaveCriticalSection(&Cs);

        if (entry != nullptr)
        {
            entry->Callback(entry->Context);
            InterlockedDecrement(entry->RefsPointer);
            delete entry;
        }

        return entry != nullptr;
    }

    void DrainQueue(bool insideApc)
    {
        while (DrainOneItem(insideApc));
    }

    void RemoveCallbacks(
        _In_ AsyncQueueCallback* searchCallback,
        _In_opt_ void* predicateContext,
        _In_ AsyncQueueRemovePredicate* removePredicate)
    {
        EnterCriticalSection(&Cs);
        PLIST_ENTRY entry = QueueHead.Flink;

        while (entry != &QueueHead)
        {
            QueueEntry* candidate = CONTAINING_RECORD(entry, QueueEntry, Entry);
            entry = entry->Flink;

            if (candidate->Callback == searchCallback && removePredicate(predicateContext, candidate->Context))
            {
                RemoveEntryList(&candidate->Entry);
                InterlockedDecrement(candidate->RefsPointer);
                delete candidate;
            }
        }

        bool isEmpty = QueueHead.Flink == &QueueHead;
        if (isEmpty)
        {
            ResetEvent(Event);
        }

        LeaveCriticalSection(&Cs);

        if (isEmpty && DispatchMode == AsyncQueueDispatchMode_FixedThread &&
            GetThreadId(ApcThread) == GetCurrentThreadId())
        {
            SleepEx(0, TRUE);
        }
    }

    bool Wait(DWORD timeout)
    {
        DWORD w = WaitForSingleObject(Event, timeout);
        return (w == WAIT_OBJECT_0);
    }

private:

    CRITICAL_SECTION Cs;
    LIST_ENTRY QueueHead;
    async_queue_t Owner;
    AsyncQueueCallbackType Type;
    AsyncQueueDispatchMode DispatchMode;
    SubmitCallback* CallbackSubmitted;
    HANDLE ApcThread;
    PTP_WORK Work;
    HANDLE Event;
    bool IsApcQueued;

    static VOID CALLBACK APCCallback(ULONG_PTR context)
    {
        Queue* queue = (Queue*)context;
        queue->DrainQueue(true);
    }

    static VOID CALLBACK TPCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK)
    {
        Queue* queue = (Queue*)context;
        queue->DrainQueue(false);
    }
};

class AsyncQueue
{
public:

    DWORD Signature;
    LIST_ENTRY ShareEntry;
    uint32_t Refs;
    uint64_t ShareId;
    Queue* Work;
    Queue* Completion;
    SubmitCallback CallbackSubmitted;
    AsyncQueue* Parent;

    AsyncQueue()
        : Signature(QUEUE_SIGNATURE)
        , Refs(1)
        , ShareId(INVALID_SHARE_ID)
        , Work(nullptr)
        , Completion(nullptr)
        , Parent(nullptr)
    {
    }

    ~AsyncQueue()
    {
        if (Parent != nullptr)
        {
            CloseAsyncQueue((async_queue_t)Parent);
        }
        else
        {
            if (Work != nullptr)
            {
                delete Work;
            }

            if (Completion != nullptr)
            {
                delete Completion;
            }
        }
    }

    void Initialize(AsyncQueue* parent)
    {
        Work = parent->Work;
        Completion = parent->Work;  // N.B. When creating a child queue its completions share the work queue
        Parent = parent;
        InterlockedIncrement(&Parent->Refs);
    }

    HRESULT Initialize(AsyncQueueDispatchMode workMode, AsyncQueueDispatchMode completionMode)
    {
        HRESULT hr;

        Queue* workQueue = nullptr;
        Queue* completionQueue = nullptr;

        async_queue_t q = (async_queue_t)this;

        workQueue = new (std::nothrow) Queue;
        if (workQueue == nullptr)
        {
            hr = E_OUTOFMEMORY;
        }
        else
        {
            hr = workQueue->Initialize(q, AsyncQueueCallbackType_Work, workMode, &CallbackSubmitted);
        }

        if (SUCCEEDED(hr))
        {
            completionQueue = new (std::nothrow) Queue;
            if (completionQueue == nullptr)
            {
                hr = E_OUTOFMEMORY;
            }
            else
            {
                hr = completionQueue->Initialize(q, AsyncQueueCallbackType_Completion, completionMode, &CallbackSubmitted);
            }
        }

        if (SUCCEEDED(hr))
        {
            Work = workQueue;
            Completion = completionQueue;
            workQueue = nullptr;
            completionQueue = nullptr;
        }

        if (workQueue != nullptr)
        {
            delete workQueue;
        }

        if (completionQueue != nullptr)
        {
            delete completionQueue;
        }

        return hr;
    }

    bool IsEmpty(_In_ AsyncQueueCallbackType type)
    {
        Queue* q = type == AsyncQueueCallbackType_Work ? Work : Completion;
        return q->IsEmpty();
    }
};

static void EnsureSharedInitialization()
{
    DWORD init = InterlockedCompareExchange(&_sharedInit, SHARED_INITIALIZING, SHARED_UNINITIALIZED);

    switch (init)
    {
    case SHARED_UNINITIALIZED:
        InitializeCriticalSection(&_sharedCs);
        InitializeListHead(&_sharedList);
        InterlockedCompareExchange(&_sharedInit, SHARED_INITIALIZED, SHARED_INITIALIZING);
        break;

    case SHARED_INITIALIZING:
        do
        {
            init = InterlockedCompareExchange(&_sharedInit, 0xFFFFFFFF, 0xFFFFFFFF);
        } while (init != SHARED_INITIALIZED);
        break;
    }
}

static AsyncQueue* FindSharedQueue(_In_ uint64_t id)
{
    EnterCriticalSection(&_sharedCs);
    for (PLIST_ENTRY entry = _sharedList.Flink; entry != &_sharedList; entry = entry->Flink)
    {
        AsyncQueue* queue = (AsyncQueue*)CONTAINING_RECORD(entry, AsyncQueue, ShareEntry);
        if (queue->ShareId == id)
        {
            LeaveCriticalSection(&_sharedCs);
            return queue;
        }
    }

    LeaveCriticalSection(&_sharedCs);
    return nullptr;
}

static void AddSharedQueue(_In_ uint64_t id, AsyncQueue* queue)
{
    queue->ShareId = id;
    EnterCriticalSection(&_sharedCs);
    InsertHeadList(&_sharedList, &queue->ShareEntry);
    LeaveCriticalSection(&_sharedCs);
}

static void RemoveSharedQueue(AsyncQueue* queue)
{
    EnterCriticalSection(&_sharedCs);
    RemoveEntryList(&queue->ShareEntry);
    LeaveCriticalSection(&_sharedCs);
    queue->ShareId = INVALID_SHARE_ID;
}

//
// Creates an Async Queue, which can be used to queue
// different async calls together.
//
HCAPI CreateAsyncQueue(
    _In_ AsyncQueueDispatchMode workDispatchMode,
    _In_ AsyncQueueDispatchMode completionDispatchMode,
    _Out_ async_queue_t* queue)
{
    if (queue == nullptr)
    {
        return E_POINTER;
    }

    AsyncQueue* aq = new (std::nothrow) AsyncQueue;
    if (aq == nullptr) return E_OUTOFMEMORY;

    HRESULT hr = aq->Initialize(workDispatchMode, completionDispatchMode);

    if (FAILED(hr))
    {
        delete aq;
        aq = nullptr;
    }

    *queue = (async_queue_t)aq;
    return hr;
}

/// <summary>
/// Creates an async queue suitable for invoking child tasks.
/// A nested queue dispatches its work through the parent
/// queue.  Both work and completions are dispatched through
/// the parent as "work" callback types.  A nested queue is useful
/// for performing intermediate work within a larger task.
/// </summary>
HCAPI CreateNestedAsyncQueue(
    _In_ async_queue_t parentQueue,
    _Out_ async_queue_t* queue)
{
    if (queue == nullptr)
    {
        return E_POINTER;
    }

    AsyncQueue* parent = (AsyncQueue*)parentQueue;
    if (parent == nullptr || parent->Signature != QUEUE_SIGNATURE) return E_INVALIDARG;
    
    AsyncQueue* aq = new (std::nothrow) AsyncQueue;
    if (aq == nullptr) return E_OUTOFMEMORY;

    aq->Initialize(parent);

    *queue = (async_queue_t)aq;
    return S_OK;
}

//
// Creates a shared queue.  Queues with the same ID and
// dispatch modes can share a single instance.
//
HCAPI CreateSharedAsyncQueue(
    _In_ uint32_t id,
    _In_ AsyncQueueDispatchMode workerMode,
    _In_ AsyncQueueDispatchMode completionMode,
    _Out_ async_queue_t* queue)
{
    if (id == INVALID_SHARE_ID) return E_INVALIDARG;
    if (queue == nullptr) return E_POINTER;

    HRESULT hr = S_OK;
    EnsureSharedInitialization();
    EnterCriticalSection(&_sharedCs);
    uint64_t queueId = MAKE_SHARED_ID(id, workerMode, completionMode);
    AsyncQueue* q = FindSharedQueue(queueId);

    if (q != nullptr)
    {
        *queue = q;
        ReferenceAsyncQueue(*queue);
    }
    else
    {
        hr = CreateAsyncQueue(workerMode, completionMode, queue);
        if (SUCCEEDED(hr))
        {
            AddSharedQueue(queueId, (AsyncQueue*)(*queue));
        }
    }

    LeaveCriticalSection(&_sharedCs);

    return hr;
}

//
// Processes items in the async queue of the given type. If an item
// is processed this will return TRUE. If there are no items to process
// this returns FALSE.  You can pass a timeout, which will cause
// DispatchAsyncQueue to wait for something to arrive in the queue.
//
HCAPI_(bool) DispatchAsyncQueue(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ uint32_t timeout)
{
    AsyncQueue* aq = (AsyncQueue*)queue;
    if (aq == nullptr || aq->Signature != QUEUE_SIGNATURE)
    {
        //DebugBreak();
        return false;
    }

    Queue* q = type == AsyncQueueCallbackType_Work ? aq->Work : aq->Completion;

    bool found = q->DrainOneItem(false);
    if (!found && timeout != 0)
    {
        found = q->Wait((DWORD)timeout);
        if (found) q->DrainOneItem(false);
    }

    return found;
}

//
// Returns TRUE if there is no outstanding work in this
// queue.
//
HCAPI_(bool) IsAsyncQueueEmpty(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type)
{
    AsyncQueue* q = (AsyncQueue*)queue;
    if (q == nullptr || q->Signature != QUEUE_SIGNATURE)
    {
        //DebugBreak();
        return false;
    }

    return q->IsEmpty(type);
}

//
// Closes the async queue.  A queue can only be closed if it
// is not in use by an async api or is empty.  If not true, the queue
// will be marked for closure and closed when it can. 
//
HCAPI_(void) CloseAsyncQueue(
    _In_ async_queue_t queue)
{
    AsyncQueue* aq = (AsyncQueue*)queue;
    if (aq == nullptr || aq->Signature != QUEUE_SIGNATURE)
    {
        //DebugBreak();
        return;
    }

    if (InterlockedDecrement(&aq->Refs) == 0)
    {
        if (aq->ShareId != INVALID_SHARE_ID)
        {
            EnsureSharedInitialization();
            EnterCriticalSection(&_sharedCs);
            RemoveSharedQueue(aq);
            LeaveCriticalSection(&_sharedCs);
        }

        delete aq;
    }
}

//
// Submits either a work or completion callback.
//
HCAPI SubmitAsyncCallback(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ void* callbackContext,
    _In_ AsyncQueueCallback* callback)
{
    AsyncQueue* aq = (AsyncQueue*)queue;
    if (aq == nullptr || aq->Signature != QUEUE_SIGNATURE) return E_INVALIDARG;

    Queue* q = type == AsyncQueueCallbackType_Work ? aq->Work : aq->Completion;
    return q->AppendItem(&aq->Refs, callback, callbackContext);
}

/// <summary>
/// Walks all callbacks in the queue of the given type and,
/// if their callback and context object are equal, invokes
/// the predicate callback and removes the callback from the 
/// queue.  This should be called before an object is deleted
/// to ensure there are no orphan callbacks in the async queue
/// that could later call back into the deleted object.
/// </summary>
HCAPI_(void) RemoveAsyncQueueCallbacks(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ AsyncQueueCallback* searchCallback,
    _In_opt_ void* predicateContext,
    _In_ AsyncQueueRemovePredicate* removePredicate)
{
    AsyncQueue* aq = (AsyncQueue*)queue;
    if (aq != nullptr && aq->Signature == QUEUE_SIGNATURE)
    {
        Queue* q = type == AsyncQueueCallbackType_Work ? aq->Work : aq->Completion;
        return q->RemoveCallbacks(searchCallback, predicateContext, removePredicate);
    }
}

//
// Increments the refcount on the queue
//
HCAPI ReferenceAsyncQueue(
    _In_ async_queue_t queue)
{
    AsyncQueue* q = (AsyncQueue*)queue;
    if (q == nullptr || q->Signature != QUEUE_SIGNATURE)
    {
        //DebugBreak();
        return E_INVALIDARG;
    }

    InterlockedIncrement(&q->Refs);

    return S_OK;
}

//
// Adds a callback that will be called when a new callback
// is submitted. The callback will be directly invoked when
// the call is submitted.
//
HCAPI AddAsyncCallbackSubmitted(
    _In_ async_queue_t queue,
    _In_opt_ void* context,
    _In_ AsyncQueueCallbackSubmitted* callback,
    _Out_ uint32_t* token)
{
    AsyncQueue* aq = (AsyncQueue*)queue;
    if (aq == nullptr || aq->Signature != QUEUE_SIGNATURE) return E_INVALIDARG;

    if (callback == nullptr || token == nullptr)
    {
        return E_POINTER;
    }

    return aq->CallbackSubmitted.Add(nullptr, context, callback, token);
}

//
// Removes a previously added callback.
//
HCAPI_(void) RemoveAsyncQueueCallbackSubmitted(
    _In_ async_queue_t queue,
    _In_ uint32_t token)
{
    AsyncQueue* aq = (AsyncQueue*)queue;
    if (aq != nullptr && aq->Signature == QUEUE_SIGNATURE)
    {
        aq->CallbackSubmitted.Remove(token);
    }
}
