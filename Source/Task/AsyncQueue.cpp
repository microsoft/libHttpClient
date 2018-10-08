// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.#include "stdafx.h"
#include "pch.h"
#include "asyncQueueP.h"
#include "AsyncQueueImpl.h"

static std::atomic<uint32_t> s_processorCount = { 0 } ;

namespace ApiDiag
{
    std::atomic<uint32_t> g_globalApiRefs = {};
}

//
// SubmitCallback
//

HRESULT SubmitCallback::Register(_In_ void* context, _In_ AsyncQueueCallbackSubmitted* callback, _Out_ registration_token_t* token)
{
    std::lock_guard<std::mutex> lock(m_lock);
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

void SubmitCallback::Unregister(_In_ registration_token_t token)
{
    std::lock_guard<std::mutex> lock(m_lock);
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

void SubmitCallback::Invoke(_In_ AsyncQueueCallbackType type)
{
    CallbackRegistration callbacks[SUBMIT_CALLBACK_MAX];
    uint32_t callbackCount;

    {
        std::lock_guard<std::mutex> lock(m_lock);
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

//
// AsyncQueueSection
//

#ifdef _WIN32
static void CALLBACK APCCallback(ULONG_PTR context)
{
    AsyncQueueSection* queue = (AsyncQueueSection*)context;
    queue->ProcessCallback();
}

static void CALLBACK TPCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK)
{
    AsyncQueueSection* queue = (AsyncQueueSection*)context;
    queue->ProcessCallback();
}
#endif

AsyncQueueSection::AsyncQueueSection()
    : Api()
{
    InitializeListHead(&m_queueHead);
    InitializeListHead(&m_pendingHead);
}

AsyncQueueSection::~AsyncQueueSection()
{
    m_timer.Cancel();

    {
        std::lock_guard<std::mutex> lock(m_lock);
        EraseQueue(&m_queueHead);
        EraseQueue(&m_pendingHead);
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

HRESULT AsyncQueueSection::Initialize(AsyncQueueCallbackType type, AsyncQueueDispatchMode mode, SubmitCallback* submitCallback)
{
    m_type = type;
    m_callbackSubmitted = submitCallback;
    m_dispatchMode = mode;

    RETURN_IF_FAILED(m_timer.Initialize(this, [](void* context)
    {
        AsyncQueueSection* pthis = static_cast<AsyncQueueSection*>(context);
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
    case AsyncQueueDispatchMode_SerializedThreadPool:
#ifdef _WIN32
        m_work = CreateThreadpoolWork(TPCallback, this, nullptr);
        RETURN_LAST_ERROR_IF_NULL(m_work);
#else
        RETURN_HR(E_NOTIMPL);
#endif
        break;
          
    case AsyncQueueDispatchMode_Immediate:
        // nothing
        break;
    }

    return S_OK;
}

HRESULT __stdcall AsyncQueueSection::QueueItem(
    IAsyncQueue* owner,
    uint32_t waitMs,
    void* context,
    AsyncQueueCallback* callback)
{
    QueueEntry* entry = new (std::nothrow) QueueEntry;
    RETURN_IF_NULL_ALLOC(entry);

    entry->owner = owner;
    entry->owner->AddRef();
    entry->callback = callback;
    entry->context = context;
    entry->refs = 1;
    entry->busy = false;
    entry->detached = false;

    if (waitMs == 0)
    {
        entry->enqueueTime = 0;

        // Owns the entry -- on error will delete
        return AppendEntry(entry);
    }
    else
    {
        entry->enqueueTime = m_timer.GetAbsoluteTime(waitMs);

        std::lock_guard<std::mutex> lock(m_lock);
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

void __stdcall AsyncQueueSection::RemoveItems(
    _In_ AsyncQueueCallback* searchCallback,
    _In_opt_ void* predicateContext,
    _In_ AsyncQueueRemovePredicate* removePredicate)
{
    bool removedPendingItem = RemoveItems(&m_pendingHead, searchCallback, predicateContext, removePredicate);

    if (removedPendingItem)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        ScheduleNextPendingCallback(nullptr);
    }

    RemoveItems(&m_queueHead, searchCallback, predicateContext, removePredicate);

    bool isEmpty;

    {
        std::lock_guard<std::mutex> lock(m_lock);
        isEmpty = m_queueHead.Flink == &m_queueHead;
        if (isEmpty) m_hasItems = false;
    }
        
    if (isEmpty)
    {
        m_event.notify_all();
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

bool AsyncQueueSection::RemoveItems(
    _In_ PLIST_ENTRY queueHead,
    _In_ AsyncQueueCallback* searchCallback,
    _In_opt_ void* predicateContext,
    _In_ AsyncQueueRemovePredicate* removePredicate)
{
    bool itemRemoved = false;

    std::lock_guard<std::mutex> lock(m_lock);

Restart:

    bool restart;
    QueueEntry* candidate = EnumNextRemoveCandidate(searchCallback, queueHead, queueHead, false, restart);

    if (restart)
    {
        goto Restart;
    }

    while (candidate != nullptr)
    {
        bool remove = false;

        if (removePredicate(predicateContext, candidate->context))
        {
            remove = true;
            itemRemoved = true;
        }

        candidate = EnumNextRemoveCandidate(searchCallback, queueHead, &candidate->entry, remove, restart);

        if (restart)
        {
            goto Restart;
        }
    }

    return itemRemoved;
}

// Returns the next candidate element for removal.  If an item is returned
// its ref count will be incremented and it's busy flag set.  Call
// ReleaseEntry when done.  If restart is true the callback was busy in 
// another call and we need to restart the enumeration.  Next is the next
// position to start looking, or null to start from the beginning of the
// list.
AsyncQueueSection::QueueEntry* AsyncQueueSection::EnumNextRemoveCandidate(
    _In_opt_ AsyncQueueCallback* searchCallback,
    _In_ PLIST_ENTRY head,
    _In_ PLIST_ENTRY current,
    _In_ bool removeCurrent,
    _Out_ bool& restart)
{
    QueueEntry* candidate = nullptr;
    restart = false;

    PLIST_ENTRY next = current->Flink;

    // If we are not processing the beginning of the list our
    // current item needs to be released and possibly removed.
    // This needs to happen under the lock.

    if (current != head)
    {
        QueueEntry* currentEntry = CONTAINING_RECORD(current, QueueEntry, entry);

        // Now release the artificial ref we added.
        ReleaseEntry(currentEntry, false);

        if (removeCurrent)
        {
            ReleaseEntry(currentEntry, true);
        }
    }

    // Next will be null if this item was pulled off the queue by someone
    // else.  We need to restart because the queue has changed shape.
    
    if (next == nullptr)
    {
        restart = true;
        return nullptr;
    }

    // Loop around to the first candidate entry that matches the search callback.  We support
    // a null search callback as a wild card (needed for decent support of lambdas).

    while (next != head)
    {
        candidate = CONTAINING_RECORD(next, QueueEntry, entry);

        if (searchCallback == nullptr || candidate->callback == searchCallback)
        {
            candidate->refs++;

            // If this item is currently being processed, wait for
            // its busy flag to clear.  Since waiting unlocks our
            // std::mutex, we must restart our loop.

            // Note:  doing this lock free and causing a restart on contention
            // can cause pathological performance problems when there are a lot
            // of items in the queue (eg, running 10,000 async operations, where each
            // operation removes callbacks when it is done causes this to continue
            // to allocate TP threads and essentially never finishes).  We are redesigning
            // this currently, but for now we grab a lock during all of remove items.

            //if (candidate->busy)
            //{
            //    while (candidate->busy)
            //    {
            //        m_busy.wait(lock);
            //        restart = true;
            //    }

            //    if (restart)
            //    {
            //        ReleaseEntry(candidate, false);
            //        candidate = nullptr;
            //        break;
            //    }
            //}

            if (candidate->detached)
            {
                ReleaseEntry(candidate, false);
            }
            else
            {
                candidate->busy = true;
                break;
            }
        }

        candidate = nullptr;
        next = next->Flink;
    }

    return candidate;
}

bool __stdcall AsyncQueueSection::DrainOneItem()
{
    QueueEntry* entry = nullptr;
    bool notify = false;

    // Get the first item in the queue to execute.  Because items
    // could be in the process of being removed, we must check their
    // busy flag.  If set we need to wait until it clears and get
    // a new item if removal detached this one.

    {
        std::unique_lock<std::mutex> lock(m_lock);

        while (true)
        {
            PLIST_ENTRY listEntry = RemoveHeadList(&m_queueHead);

            if (listEntry != &m_queueHead)
            {
                listEntry->Flink = listEntry->Blink = nullptr;
                entry = CONTAINING_RECORD(listEntry, QueueEntry, entry);
                entry->refs++;
                m_processingCallback++;

                while (entry->busy)
                {
                    m_busy.wait(lock);
                }

                if (entry->detached)
                {
                    m_processingCallback--;
                    ReleaseEntry(entry, false);
                }
                else
                {
                    entry->busy = true;
                    entry->detached = true;
                    entry->refs--;
                    ASSERT(entry->refs != 0);
                    break;
                }
            }
            else
            {
                m_hasItems = false;
                notify = true;
                break;
            }
        }
    }

    if (notify)
    {
        m_event.notify_all();
    }

    if (entry != nullptr)
    {
        entry->callback(entry->context);
        m_processingCallback--;

        std::lock_guard<std::mutex> lock(m_lock);
        ReleaseEntry(entry, false);
        return true;
    }

    return false;
}

bool __stdcall AsyncQueueSection::Wait(uint32_t timeout)
{
    std::unique_lock<std::mutex> lock(m_lock);
    while (!m_hasItems)
    {
        if (m_event.wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
        {
            break;
        }
    }
    return m_hasItems;
}

bool __stdcall AsyncQueueSection::IsEmpty()
{
    std::lock_guard<std::mutex> lock(m_lock);
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

// Called from thread pool or APC system callbacks
void AsyncQueueSection::ProcessCallback()
{
    if (m_dispatchMode == AsyncQueueDispatchMode_SerializedThreadPool)
    {
        while(DrainOneItem());
    }
    else
    {
        DrainOneItem();
    }
}

// Appends the given entry to the active queue.  The entry should already
// be add-refd, and this API owns the lifetime of the entry.  If the
// API fails, the entry will be released and deleted.
HRESULT AsyncQueueSection::AppendEntry(
    _In_ QueueEntry* entry)
{
    bool firstItem;

    {
        std::lock_guard<std::mutex> lock(m_lock);
        firstItem = (m_queueHead.Flink == &m_queueHead);
        InsertTailList(&m_queueHead, &entry->entry);
        m_hasItems = true;
    }

    m_event.notify_all();

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

            std::lock_guard<std::mutex> lock(m_lock);
            ReleaseEntry(entry, true);
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

    case AsyncQueueDispatchMode_SerializedThreadPool:
#ifdef _WIN32
        if (firstItem)
        {
            SubmitThreadpoolWork(m_work);
        }
#else
        ASSERT(false);
#endif
        break;

    case AsyncQueueDispatchMode_Immediate:
        // We will handle this after we invoke
        // callback submitted.
        break;
    }

    m_callbackSubmitted->Invoke(m_type);
    
    if (m_dispatchMode == AsyncQueueDispatchMode_Immediate)
    {
        DrainOneItem();
    }

    return S_OK;
}

// Releases the entry and optionally removes it from its owning list.
// Also clears the entry busy flag and, if it was set, notifies the
// m_busy condition variable.
// m_lock must be held before calling
void AsyncQueueSection::ReleaseEntry(
    _In_ QueueEntry* entry,
    _In_ bool remove)
{
    bool wasBusy;
    wasBusy = entry->busy;
    entry->busy = false;

    if (remove)
    {
        entry->detached = true;

        // When we remove an entry from the list, we clear the
        // flink and blink to ensure we don't remove more than
        // once.  Removing an entry that is already removed can
        // corrupt the list, and nulling the links forces a crash
        // if we do it.

        if (entry->entry.Flink != nullptr)
        {
            RemoveEntryList(&entry->entry);
            entry->entry.Flink = entry->entry.Blink = nullptr;
        }
    }

    entry->refs--;
    if (entry->refs == 0)
    {
        ASSERT(entry->detached);
        entry->owner->Release();
        delete entry;
    }

    if (wasBusy)
    {
        m_busy.notify_all();
    }
}

void AsyncQueueSection::EraseQueue(_In_ PLIST_ENTRY head)
{
    PLIST_ENTRY listEntry = RemoveHeadList(head);
    while (listEntry != head)
    {
        QueueEntry* entry = CONTAINING_RECORD(listEntry, QueueEntry, entry);
        entry->owner->Release();
        delete entry;
        listEntry = RemoveHeadList(head);
    }
}

// Examines the pending callback list, optionally popping the entry off the
// list that matches m_timerDue, and schedules the timer for the next entry.
// this requires m_lock held.
void AsyncQueueSection::ScheduleNextPendingCallback(_Out_opt_ QueueEntry** dueEntry)
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
        if (dueEntry != nullptr && (*dueEntry) == nullptr && candidate->enqueueTime == m_timerDue)
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

void AsyncQueueSection::SubmitPendingCallback()
{
    QueueEntry* dueEntry;

    {
        std::lock_guard<std::mutex> lock(m_lock);
        ScheduleNextPendingCallback(&dueEntry);
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

//
// AsyncQueue
//

AsyncQueue::AsyncQueue()
    : Api()
    , m_callbackSubmitted(&m_header)
{
    m_header.m_signature = ASYNC_QUEUE_SIGNATURE;
    m_header.m_queue = this;
}

AsyncQueue::~AsyncQueue()
{
}

HRESULT AsyncQueue::Initialize(AsyncQueueDispatchMode workMode, AsyncQueueDispatchMode completionMode)
{
    referenced_ptr<AsyncQueueSection> work(new (std::nothrow) AsyncQueueSection);
    RETURN_IF_NULL_ALLOC(work);
    RETURN_IF_FAILED(work->Initialize(AsyncQueueCallbackType_Work, workMode, &m_callbackSubmitted));

    referenced_ptr<AsyncQueueSection> completion(new (std::nothrow) AsyncQueueSection);
    RETURN_IF_NULL_ALLOC(completion);
    RETURN_IF_FAILED(completion->Initialize(AsyncQueueCallbackType_Completion, completionMode, &m_callbackSubmitted));
    
    RETURN_IF_FAILED(work->QueryApi(ApiId::AsyncQueueSection, (void**)&m_work));
    RETURN_IF_FAILED(completion->QueryApi(ApiId::AsyncQueueSection, (void**)&m_completion));

    return S_OK;
}

HRESULT __stdcall AsyncQueue::GetSection(
    _In_ AsyncQueueCallbackType type,
    _Out_ IAsyncQueueSection** section)
{
    RETURN_HR_IF(E_POINTER, section == nullptr);
    
    switch(type)
    {
        case AsyncQueueCallbackType_Work:
            *section = m_work.get();
            m_work->AddRef();
            break;
            
        case AsyncQueueCallbackType_Completion:
            *section = m_completion.get();
            m_completion->AddRef();
            break;
            
        default:
            RETURN_HR(E_INVALIDARG);
    }
    
    return S_OK;
}

HRESULT __stdcall AsyncQueue::RegisterSubmitCallback(
    _In_opt_ void* context,
    _In_ AsyncQueueCallbackSubmitted* callback,
    _Out_ registration_token_t* token)
{
    return m_callbackSubmitted.Register(context, callback, token);
}

void __stdcall AsyncQueue::UnregisterSubmitCallback(
    _In_ registration_token_t token)
{
    m_callbackSubmitted.Unregister(token);
}

///////////////////
// AsyncQueue.h APIs
///////////////////

//
// Creates an Async Queue, which can be used to queue
// different async calls together.
//
STDAPI CreateAsyncQueue(
    _In_ AsyncQueueDispatchMode workDispatchMode,
    _In_ AsyncQueueDispatchMode completionDispatchMode,
    _Out_ async_queue_handle_t* queue)
{
    referenced_ptr<AsyncQueue> aq(new (std::nothrow) AsyncQueue);
    RETURN_IF_NULL_ALLOC(aq);
    RETURN_IF_FAILED(aq->Initialize(workDispatchMode, completionDispatchMode));
    *queue = aq.release()->GetHandle();
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
    referenced_ptr<IAsyncQueue> aq(GetQueue(queue));
    if (aq == nullptr)
    {
        return false;
    }
    
    referenced_ptr<IAsyncQueueSection> q;
    if (FAILED(aq->GetSection(type, q.address_of())))
    {
        return false;
    }
        
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
    referenced_ptr<IAsyncQueue> aq(GetQueue(queue));
    if (aq == nullptr)
    {
        return false;
    }
    
    referenced_ptr<IAsyncQueueSection> q;
    if (FAILED(aq->GetSection(type, q.address_of())))
    {
        return false;
    }

    return q->IsEmpty();
}

//
// Closes the async queue.  A queue can only be closed if it
// is not in use by an async api or is empty.  If not true, the queue
// will be marked for closure and closed when it can. 
//
STDAPI_(void) CloseAsyncQueue(
    _In_ async_queue_handle_t queue)
{
    IAsyncQueue* aq = GetQueue(queue);
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
    referenced_ptr<IAsyncQueue> aq(GetQueue(queue));
    RETURN_HR_IF(E_INVALIDARG, aq == nullptr);

    referenced_ptr<IAsyncQueueSection> q;
    RETURN_IF_FAILED(aq->GetSection(type, q.address_of()));

    RETURN_HR(q->QueueItem(aq.get(), delayMs, callbackContext, callback));
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
    _In_opt_ AsyncQueueCallback* searchCallback,
    _In_opt_ void* predicateContext,
    _In_ AsyncQueueRemovePredicate* removePredicate)
{
    referenced_ptr<IAsyncQueue> aq(GetQueue(queue));
    if (aq != nullptr)
    {
        referenced_ptr<IAsyncQueueSection> q;
        if (SUCCEEDED(aq->GetSection(type, q.address_of())))
        {
            q->RemoveItems(searchCallback, predicateContext, removePredicate);
        }
    }
}

//
// Increments the refcount on the queue
//
STDAPI DuplicateAsyncQueueHandle(
    _In_ async_queue_handle_t queueHandle,
    _Out_ async_queue_handle_t* duplicatedHandle)
{
    RETURN_HR_IF(E_POINTER, duplicatedHandle == nullptr);

    auto queue = GetQueue(queueHandle);
    RETURN_HR_IF(E_INVALIDARG, queue == nullptr);

    queue->AddRef();
    *duplicatedHandle = queueHandle;

    return S_OK;
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
    referenced_ptr<IAsyncQueue> aq(GetQueue(queue));
    RETURN_HR_IF(E_INVALIDARG, aq == nullptr);
    RETURN_HR(aq->RegisterSubmitCallback(context, callback, token));
}

//
// Unregisters a previously added callback.
//
STDAPI_(void) UnregisterAsyncQueueCallbackSubmitted(
    _In_ async_queue_handle_t queue,
    _In_ registration_token_t token)
{
    referenced_ptr<IAsyncQueue> aq(GetQueue(queue));
    if (aq != nullptr)
    {
        aq->UnregisterSubmitCallback(token);
    }
}
