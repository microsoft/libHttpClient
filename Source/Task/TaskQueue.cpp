// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.#include "stdafx.h"
#include "pch.h"
#include "referenced_ptr.h"
#include "TaskQueueP.h"
#include "TaskQueueImpl.h"

//
// Note:  ApiDiag is only used for reference count validation during
//        unit tests.  Otherwise, g_globalApiRefs is unused.
//
namespace ApiDiag
{
    std::atomic<uint32_t> g_globalApiRefs = { 0 };
    
    void GlobalAddRef()
    {
        g_globalApiRefs++;
    }
    
    void GlobalRelease()
    {
        g_globalApiRefs--;
    }
}

namespace ProcessGlobals
{
    const XTaskQueueHandle g_invalidQueueHandle = (XTaskQueueHandle)(-1);
    std::atomic<XTaskQueueHandle> g_defaultProcessQueue = { g_invalidQueueHandle };
    std::atomic<XTaskQueueHandle> g_processQueue = { g_invalidQueueHandle };
}

//
// SubmitCallback
//

SubmitCallback::SubmitCallback(
    _In_ XTaskQueueHandle queue)
    : m_queue(queue)
{
    memset(m_buffer1, 0, sizeof(m_buffer1));
    memset(m_buffer2, 0, sizeof(m_buffer2));
}

HRESULT SubmitCallback::Register(_In_ void* context, _In_ XTaskQueueMonitorCallback* callback, _Out_ XTaskQueueRegistrationToken* token)
{
    RETURN_HR_IF(E_POINTER, callback == nullptr || token == nullptr);

    token->token = 0;

    std::lock_guard<std::mutex> lock(m_lock);
    uint32_t bufferReadIdx = (m_indexAndRef & 0x80000000 ? 1 : 0);
    uint32_t bufferWriteIdx = 1 - bufferReadIdx;

    for(uint32_t idx = 0; idx < ARRAYSIZE(m_buffer1); idx++)
    {
        if (token->token == 0 && m_buffers[bufferReadIdx][idx].Callback == nullptr)
        {
            token->token = ++m_nextToken;
            m_buffers[bufferWriteIdx][idx].Token = token->token;
            m_buffers[bufferWriteIdx][idx].Context = context;
            m_buffers[bufferWriteIdx][idx].Callback = callback;
        }
        else
        {
            m_buffers[bufferWriteIdx][idx] = m_buffers[bufferReadIdx][idx];
        }
    }

    RETURN_HR_IF(E_OUTOFMEMORY, token->token == 0);

    // Now spin wait to swap the active buffer.
    uint32_t expected = bufferReadIdx << 31;
    uint32_t desired = bufferWriteIdx << 31;

    while (!m_indexAndRef.compare_exchange_weak(expected, desired)) {}
    
    return S_OK;
}

void SubmitCallback::Unregister(_In_ XTaskQueueRegistrationToken token)
{
    std::lock_guard<std::mutex> lock(m_lock);
    uint32_t bufferReadIdx = (m_indexAndRef & 0x80000000 ? 1 : 0);
    uint32_t bufferWriteIdx = 1 - bufferReadIdx;

    for(uint32_t idx = 0; idx < ARRAYSIZE(m_buffer1); idx++)
    {
        if (m_buffers[bufferReadIdx][idx].Token == token.token)
        {
            m_buffers[bufferWriteIdx][idx].Callback = nullptr;
        }
        else
        {
            m_buffers[bufferWriteIdx][idx] = m_buffers[bufferReadIdx][idx];
        }
    }

    // Now spin wait to swap the active buffer.
    uint32_t expected = bufferReadIdx << 31;
    uint32_t desired = bufferWriteIdx << 31;

    while (!m_indexAndRef.compare_exchange_weak(expected, desired)) {}
}

void SubmitCallback::Invoke(_In_ ITaskQueuePort* port)
{
    uint32_t indexAndRef = ++m_indexAndRef;
    uint32_t bufferIdx = (indexAndRef & 0x80000000 ? 1 : 0);
    
    XTaskQueuePort portId = GetQueue(m_queue)->GetPort(port);
    
    for(uint32_t idx = 0; idx < ARRAYSIZE(m_buffer1); idx++)
    {
        if (m_buffers[bufferIdx][idx].Callback != nullptr)
        {
            m_buffers[bufferIdx][idx].Callback(
                m_buffers[bufferIdx][idx].Context,
                m_queue,
                portId);
        }
    }

    m_indexAndRef--;
}

//
// QueueWaitRegistry
//

HRESULT QueueWaitRegistry::Register(
    _In_ XTaskQueuePort port,
    _In_ const XTaskQueueRegistrationToken& portToken,
    _Out_ XTaskQueueRegistrationToken* token)
{
    RETURN_HR_IF(E_OUTOFMEMORY, m_callbacks.capacity() == 0);

    std::lock_guard<std::mutex> lock(m_lock);

    WaitRegistration reg = { };
    reg.Port = port;
    reg.Token = ++m_nextToken;
    reg.PortToken = portToken.token;
    token->token = reg.Token;
    m_callbacks.append(reg);

    return S_OK;
}

std::pair<XTaskQueuePort, XTaskQueueRegistrationToken> QueueWaitRegistry::Unregister(
    _In_ const XTaskQueueRegistrationToken& token)
{
    XTaskQueuePort port = XTaskQueuePort::Work;
    XTaskQueueRegistrationToken portToken;
    portToken.token = 0;

    std::lock_guard<std::mutex> lock(m_lock);

    for(uint32_t idx = 0; idx < m_callbacks.count(); idx++)
    {
        if (m_callbacks[idx].Token == token.token)
        {
            port = m_callbacks[idx].Port;
            portToken.token = m_callbacks[idx].PortToken;
            m_callbacks.removeAt(idx);
            break;
        }
    }

    return std::pair<XTaskQueuePort, XTaskQueueRegistrationToken>(port, portToken);
}

//
// TaskQueuePortImpl
//

TaskQueuePortImpl::TaskQueuePortImpl()
    : Api()
{
    m_header.m_signature = TASK_QUEUE_PORT_SIGNATURE;
    m_header.m_port = this;
    m_header.m_queue = nullptr;
}

TaskQueuePortImpl::~TaskQueuePortImpl()
{
    m_timer.Cancel();

    EraseQueue(m_queueList.get());
    EraseQueue(m_pendingList.get());

#ifdef _WIN32
    StaticArray<WaitRegistration*, PORT_WAIT_MAX> waits;

    // We have no control over when these event callbacks
    // fire, so we must guard during destruction.
    {
        std::lock_guard<std::mutex> lock(m_lock);
        waits = m_waits;
        m_waits.clear();
    }

    if (waits.count() != 0)
    {
        for (uint32_t idx = 0; idx < waits.count(); idx++)
        {
            if (waits[idx]->threadpoolWait != nullptr)
            {
                SetThreadpoolWait(waits[idx]->threadpoolWait, nullptr, nullptr);
                WaitForThreadpoolWaitCallbacks(waits[idx]->threadpoolWait, TRUE);
                CloseThreadpoolWait(waits[idx]->threadpoolWait);
            }

            // Note: queue entries that are parked on the wait registration
            // don't take a reference on the owner, so don't release here.
            delete waits[idx]->queueEntry;
            delete waits[idx];
        }
    }

#endif

    m_threadPool.Terminate();

#ifdef _WIN32
    if (m_events.count() != 0)
    {
        // Other events in here are owned by the caller.
        CloseHandle(m_events[0]);
    }
#endif
}

HRESULT TaskQueuePortImpl::Initialize(XTaskQueueDispatchMode mode, SubmitCallback* submitCallback)
{
    m_callbackSubmitted = submitCallback;
    m_dispatchMode = mode;

    m_queueList.reset(new (std::nothrow) LocklessList<QueueEntry>);
    RETURN_IF_NULL_ALLOC(m_queueList);

    m_pendingList.reset(new (std::nothrow) LocklessList<QueueEntry>);
    RETURN_IF_NULL_ALLOC(m_pendingList);

    m_terminationList.reset(new (std::nothrow) LocklessList<TerminationEntry>);
    RETURN_IF_NULL_ALLOC(m_terminationList);

    RETURN_IF_FAILED(m_timer.Initialize(this, [](void* context)
    {
        TaskQueuePortImpl* pthis = static_cast<TaskQueuePortImpl*>(context);
        pthis->SubmitPendingCallback();
    }));

#ifdef _WIN32
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    RETURN_LAST_ERROR_IF_NULL(evt);
    m_events.append(evt);
#endif

    switch (mode)
    {
    case XTaskQueueDispatchMode::Manual:
        // nothing
        break;

    case XTaskQueueDispatchMode::ThreadPool:
    case XTaskQueueDispatchMode::SerializedThreadPool:
        RETURN_IF_FAILED(m_threadPool.Initialize(this, [](void* context)
        {
            TaskQueuePortImpl* pthis = static_cast<TaskQueuePortImpl*>(context);
            pthis->ProcessThreadPoolCallback();
        }));
        break;
          
    case XTaskQueueDispatchMode::Immediate:
        // nothing
        break;
    }

    return S_OK;
}

HRESULT __stdcall TaskQueuePortImpl::QueueItem(
    _In_ ITaskQueue* owner,
    _In_ XTaskQueuePort port,
    _In_ uint32_t waitMs,
    _In_opt_ void* callbackContext,
    _In_ XTaskQueueCallback* callback)
{
    RETURN_IF_FAILED(VerifyNotTerminated(owner));

    std::unique_ptr<QueueEntry> entry(new (std::nothrow) QueueEntry);
    RETURN_IF_NULL_ALLOC(entry);

    entry->owner = owner;
    entry->port = port;
    entry->owner->AddRef();
    entry->callback = callback;
    entry->context = callbackContext;
    entry->waitRegistration = nullptr;
    entry->refs = 1;

    if (waitMs == 0)
    {
        entry->enqueueTime = 0;
        RETURN_HR_IF_FALSE(E_OUTOFMEMORY, AppendEntry(entry.get()));
    }
    else
    {
        entry->enqueueTime = m_timer.GetAbsoluteTime(waitMs);
        RETURN_HR_IF_FALSE(E_OUTOFMEMORY, m_pendingList->push_back(entry.get()));

        // If the entry's enqueue time is < our current time,
        // update the timer.
        while (true)
        {
            uint64_t due = m_timerDue;
            if (entry->enqueueTime < due)
            {
                if (m_timerDue.compare_exchange_weak(due, entry->enqueueTime))
                {
                    m_timer.Start(entry->enqueueTime);
                    break;
                }
            }
            else if (m_timerDue.compare_exchange_weak(due, due))
            {
                break;
            }
        }
    }

    entry.release();
    return S_OK;
}

HRESULT __stdcall TaskQueuePortImpl::RegisterWaitHandle(
    _In_ ITaskQueue* owner,
    _In_ XTaskQueuePort port,
    _In_ HANDLE waitHandle,
    _In_opt_ void* callbackContext,
    _In_ XTaskQueueCallback* callback,
    _Out_ XTaskQueueRegistrationToken* token)
{
    RETURN_HR_IF(E_INVALIDARG, callback == nullptr || waitHandle == nullptr || token == nullptr);
    RETURN_HR_IF(E_POINTER, token == nullptr);
    RETURN_IF_FAILED(VerifyNotTerminated(owner));

#ifdef _WIN32
    std::unique_ptr<WaitRegistration> waitReg(new (std::nothrow) WaitRegistration);
    RETURN_IF_NULL_ALLOC(waitReg);

    std::unique_ptr<QueueEntry> entry(new (std::nothrow) QueueEntry);
    RETURN_IF_NULL_ALLOC(entry);

    // Entry gets its owner and an addref on it when it
    // is added to the queue
    entry->owner = nullptr;
    entry->port = port;
    entry->callback = callback;
    entry->context = callbackContext;
    entry->waitRegistration = waitReg.get();
    entry->refs = 1;

    // Owner on waitReg is not add-ref'd because a registered
    // waiter does not keep the queue alive.
    waitReg->waitHandle = waitHandle;
    waitReg->token = ++m_nextWaitToken;
    waitReg->owner = owner;
    waitReg->port = this;
    waitReg->queueEntry = entry.get();
    waitReg->threadpoolWait = nullptr;

    std::lock_guard<std::mutex> lock(m_lock);
    RETURN_HR_IF(E_OUTOFMEMORY, m_events.capacity() == 0);
    RETURN_HR_IF(E_OUTOFMEMORY, m_waits.capacity() == 0);

    RETURN_IF_FAILED(InitializeWaitRegistration(waitReg.get()));

    m_events.append(waitHandle);
    m_waits.append(waitReg.get());

    token->token = waitReg->token;

    entry.release();
    waitReg.release();

    // The queue may be waiting on our handle array, but
    // we've just changed the array so the wait needs to
    // fetch it again.
    SignalQueue();

    return S_OK;
#else
    return E_NOTIMPL;
#endif
}

void __stdcall TaskQueuePortImpl::UnregisterWaitHandle(
    _In_ XTaskQueueRegistrationToken token)
{
#ifdef _WIN32
    WaitRegistration* toDelete = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_lock);
        for(uint32_t idx = 0; idx < m_waits.count(); idx++)
        {
            if (m_waits[idx]->token == token.token)
            {
                toDelete = m_waits[idx];
                m_waits.removeAt(idx);
                break;
            }
        }

        if (toDelete != nullptr)
        {
            // Any running entry will look for this and re-register,
            // so clear it under the lock.
            toDelete->queueEntry->waitRegistration = nullptr;

            // Remove the handle from our event list
            for (uint32_t idx = 1; idx < m_events.count(); idx++)
            {
                if (m_events[idx] == toDelete->waitHandle)
                {
                    m_events.removeAt(idx);
                    break;
                }
            }
        }
    }

    if (toDelete != nullptr)
    {
        SetThreadpoolWait(toDelete->threadpoolWait, nullptr, nullptr);
        WaitForThreadpoolWaitCallbacks(toDelete->threadpoolWait, TRUE);
        CloseThreadpoolWait(toDelete->threadpoolWait);
        ReleaseEntry(toDelete->queueEntry);
        delete toDelete;
    }

    // The queue may be waiting on our handle array, but
    // we've just changed the array so the wait needs to
    // fetch it again.
    SignalQueue();

#else
    UNREFERENCED_PARAMETER(token);
#endif
}

HRESULT __stdcall TaskQueuePortImpl::PrepareTerminate(
    _In_ ITaskQueue* owner,
    _In_ void* context,
    _In_ XTaskQueueTerminatedCallback* callback,
    _Out_ void** token)
{
    RETURN_HR_IF(E_POINTER, token == nullptr);

    std::unique_ptr<TerminationEntry> term(new (std::nothrow) TerminationEntry);
    RETURN_IF_NULL_ALLOC(term);

    std::unique_ptr<LocklessList<TerminationEntry>::Node> node(new (std::nothrow) LocklessList<TerminationEntry>::Node);
    RETURN_IF_NULL_ALLOC(node);

    term->context = context;
    term->callback = callback;
    term->node = node.release();

    // Mark the port as canceled, but don't overwrite
    // terminating or terminated status.
    owner->TrySetPortStatus(this, TaskQueuePortStatus::Active, TaskQueuePortStatus::Canceled);
    *token = term.release();

    return S_OK;
}

void __stdcall TaskQueuePortImpl::CancelTermination(
    _In_ void* token)
{
    TerminationEntry* term = static_cast<TerminationEntry*>(token);
    term->owner->TrySetPortStatus(this, TaskQueuePortStatus::Canceled, TaskQueuePortStatus::Active);
    delete term->node;
    delete term;
}

void __stdcall TaskQueuePortImpl::Terminate(
    _In_ void* token)
{
    TerminationEntry* term = static_cast<TerminationEntry*>(token);
    
    CancelPendingEntries(term->owner, true);

    // Insert the termination callback into the queue.  Even if the
    // main queue is empty, we still signal it and run through
    // a cycle.  This ensures we flush the queue out with no 
    // races and that the termination callback happens on the right
    // thread.

    if (term->callback != nullptr)
    {
        // This never fails because we preallocate the
        // list node.
        (void)m_terminationList->push_back(term, term->node);
        term->node = nullptr; // now owned by the list
    }

    // We will not signal until we are marked as terminated. The queue could
    // still be moving while we are running this terminate call.

    term->owner->SetPortStatus(this, TaskQueuePortStatus::Terminated);
    SignalQueue();

    // We must ensure we poke the queue threads in case there's
    // nothing submitted
    switch (m_dispatchMode)
    {
    case XTaskQueueDispatchMode::SerializedThreadPool:
    case XTaskQueueDispatchMode::ThreadPool:
        m_threadPool.Submit();
        break;

    case XTaskQueueDispatchMode::Immediate:
        DrainOneItem();
        break;

    default:
        break;
    }
}

void __stdcall TaskQueuePortImpl::Detach(
    _In_ ITaskQueue* owner)
{
    CancelPendingEntries(owner, false);
}

bool __stdcall TaskQueuePortImpl::DrainOneItem()
{
    m_processingCallback++;
    QueueEntry* entry = m_queueList->pop_front();
    if (entry == nullptr)
    {
        m_processingCallback--;
    }
    else
    {
        ASSERT(entry->refs.load() != 0);
    }

    if (entry != nullptr)
    {
        entry->callback(entry->context, IsCallCanceled(entry));
        m_processingCallback--;

#ifdef _WIN32
        // If this entry has a wait registration, it needs
        // to be reinitialized as we mark it to only execute
        // once.
        if (entry->waitRegistration != nullptr)
        {
            std::lock_guard<std::mutex> lock(m_lock);            
            if (entry->waitRegistration != nullptr)
            {
                InitializeWaitRegistration(entry->waitRegistration);
            }            
        }
#endif
        ReleaseEntry(entry);
    }

    if (m_queueList->empty())
    {
        SignalQueue();
        SignalTerminations();
    }

    return entry != nullptr;
}

bool __stdcall TaskQueuePortImpl::Wait(
    _In_ ITaskQueue* owner,
    _In_ uint32_t timeout)
{
#ifdef _WIN32
    while(true)
    {
        StaticArray<HANDLE, PORT_EVENT_MAX> events;

        {
            std::lock_guard<std::mutex> lock(m_lock);
            events = m_events;
        }

        DWORD waitResult = WaitForMultipleObjects(events.count(), events.data(), FALSE, timeout);

        if (waitResult > WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + events.count())
        {
            // One of our waiters was signaled.  Find it, and then process it.
            std::lock_guard<std::mutex> lock(m_lock);
            for (uint32_t idx = 0; idx < m_waits.count(); idx++)
            {
                if (m_waits[idx]->waitHandle == events[waitResult - WAIT_OBJECT_0])
                {
                    m_waits[idx]->queueEntry->refs++;

                    if (!AppendWaitRegistrationEntry(m_waits[idx]))
                    {
                        // If we fail adding to the queue, re-initialize our wait
                        LOG_IF_FAILED(InitializeWaitRegistration(m_waits[idx]));
                        ReleaseEntry(m_waits[idx]->queueEntry);
                    }
                    break;
                }
            }
        }
        else if (waitResult == WAIT_OBJECT_0)
        {
            // We are using event 0 like a condition variable.  It's
            // auto reset, so if nothing is in the queue we continue
            // waiting.
            if (owner->GetPortStatus(this) == TaskQueuePortStatus::Terminated || !m_queueList->empty())
            {
                break;
            }
        }
        else
        {
            break;
        }
    }

#else
    while (m_queueList->empty() && owner->GetPortStatus(this) != TaskQueuePortStatus::Terminated)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        if (m_event.wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout)
        {
            break;
        }
    }
#endif

    return !m_queueList->empty() || !m_terminationList->empty();
}

bool __stdcall TaskQueuePortImpl::IsEmpty()
{
    bool empty =
        (m_queueList->empty()) &&
        (m_pendingList->empty()) &&
        (m_processingCallback == 0);

    return empty;
}

HRESULT TaskQueuePortImpl::VerifyNotTerminated(
    _In_ ITaskQueue* owner)
{
    // N.B.  This looks wrong but it's not.  We only error adding new items
    // if we are terminating or terminated.  If we're just canceled we allow
    // new items in but we invoke them with the canceled flag set to true.
    RETURN_HR_IF(__HRESULT_FROM_WIN32(ERROR_CANCELLED), owner->GetPortStatus(this) > TaskQueuePortStatus::Canceled);
    return S_OK;
}

bool TaskQueuePortImpl::IsCallCanceled(_In_ QueueEntry* entry)
{
    return entry->owner->GetPortStatus(this) != TaskQueuePortStatus::Active;
}

// Appends the given entry to the active queue.  The entry should already
// be add-refd. This will return false on failure.
bool TaskQueuePortImpl::AppendEntry(
    _In_ QueueEntry* entry,
    _In_opt_ QueueEntryNode* node,
    _In_ bool signal)
{
    if (!m_queueList->push_back(entry, node))
    {
        return false;
    }

    if (signal)
    {
        SignalQueue();
    }

    switch (m_dispatchMode)
    {
    case XTaskQueueDispatchMode::Manual:
        // nothing
        break;

    case XTaskQueueDispatchMode::SerializedThreadPool:
    case XTaskQueueDispatchMode::ThreadPool:
        m_threadPool.Submit();
        break;

    case XTaskQueueDispatchMode::Immediate:
        // We will handle this after we invoke
        // callback submitted.
        break;
    }

    m_callbackSubmitted->Invoke(this);
    
    if (m_dispatchMode == XTaskQueueDispatchMode::Immediate)
    {
        DrainOneItem();
    }

    return true;
}

// Releases the entry and the ref on the owner.
void TaskQueuePortImpl::ReleaseEntry(
    _In_ QueueEntry* entry)
{
    if (--entry->refs == 0)
    {
        if (entry->owner != nullptr)
        {
            entry->owner->Release();
        }
        delete entry;
    }
}

void TaskQueuePortImpl::CancelPendingEntries(
    _In_ ITaskQueue* owner,
    _In_ bool appendToQueue)
{
    // Stop wait timer and promote pending calbacks that are used
    // by the queue that invoked this termination. Other callbacks
    // are placed back on the pending list.
    
    m_timer.Cancel();
    
    QueueEntryNode* queueEntryNode;
    QueueEntry* queueEntry = m_pendingList->pop_front(&queueEntryNode);
    QueueEntry* initialPushedEntry = nullptr;
    while(queueEntry != nullptr)
    {
        if (queueEntry == initialPushedEntry)
        {
            m_pendingList->push_back(queueEntry, queueEntryNode);
            break;
        }
        
        if (queueEntry->owner == owner)
        {
            if (!appendToQueue || !AppendEntry(queueEntry, queueEntryNode))
            {
                ReleaseEntry(queueEntry);
                delete queueEntryNode;
            }
        }
        else
        {
            if (initialPushedEntry == nullptr)
            {
                queueEntry = initialPushedEntry;
            }
            m_pendingList->push_back(queueEntry, queueEntryNode);
        }
        queueEntry = m_pendingList->pop_front(&queueEntryNode);
    }
    
    SubmitPendingCallback();
    
#ifdef _WIN32
    
    // Abort any registered waits and promote their entries too.
    // Wait registration is not lock free.
    std::unique_lock<std::mutex> lock(m_lock);
    
    for (uint32_t index = m_waits.count(); index > 0; index--)
    {
        uint32_t idx = index - 1;
        if (m_waits[idx]->owner == owner)
        {
            CloseThreadpoolWait(m_waits[idx]->threadpoolWait);
            m_waits[idx]->queueEntry->waitRegistration = nullptr;
            
            if (!appendToQueue || !AppendWaitRegistrationEntry(m_waits[idx]))
            {
                ReleaseEntry(m_waits[idx]->queueEntry);
            }
            
            delete m_waits[idx];
            m_waits.removeAt(idx);
        }
    }
    lock.unlock();
    
#endif
}

void TaskQueuePortImpl::EraseQueue(
    _In_opt_ LocklessList<QueueEntry>* queue)
{
    if (queue != nullptr)
    {
        QueueEntry* entry = queue->pop_front();
        while(entry != nullptr)
        {
            ASSERT(entry->owner != nullptr);
            entry->owner->Release();
            delete entry;
            entry = queue->pop_front();
        }
    }
}

// Examines the pending callback list, optionally popping the entry off the
// list that matches m_timerDue, and schedules the timer for the next entry.
void TaskQueuePortImpl::ScheduleNextPendingCallback(
    _In_ uint64_t dueTime,
    _Out_ QueueEntry** dueEntry,
    _Out_ QueueEntryNode** dueEntryNode)
{
    QueueEntry* nextItem = nullptr;
    *dueEntry = nullptr;
    *dueEntryNode = nullptr;

    // We pop items off the pending list till it is empty, looking for 
    // our due time.  We can do this without a lock because we only call
    // this once the timer has come due, so there will be nothing else competing
    // and messing with this list.  We must not rely on a lock here because 
    // we need to allow QueueItem to remain lock free.

    LocklessList<QueueEntry> pendingList;
    QueueEntryNode* node;
    QueueEntry* entry = m_pendingList->pop_front(&node);
    while (entry != nullptr)
    {
        if ((*dueEntry) == nullptr && entry->enqueueTime == dueTime)
        {
            *dueEntry = entry;
            *dueEntryNode = node;
        }
        else if (nextItem == nullptr || nextItem->enqueueTime > entry->enqueueTime)
        {
            nextItem = entry;
            pendingList.push_back(entry, node);
        }
        else
        {
            pendingList.push_back(entry, node);
        }

        entry = m_pendingList->pop_front(&node);
    }

    entry = pendingList.pop_front(&node);
    while (entry != nullptr)
    {
        m_pendingList->push_back(entry, node);
        entry = pendingList.pop_front(&node);
    }

    if (nextItem != nullptr)
    {
        while (true)
        {
            if (m_timerDue.compare_exchange_weak(dueTime, nextItem->enqueueTime))
            {
                m_timer.Start(nextItem->enqueueTime);
                break;
            }

            dueTime = m_timerDue.load();

            if (dueTime <= nextItem->enqueueTime)
            {
                break;
            }
        }
    }
    else
    {
        uint64_t noDueTime = UINT64_MAX;
        if (m_timerDue.compare_exchange_strong(dueTime, noDueTime))
        {
            m_timer.Cancel();
        }
    }
}

void TaskQueuePortImpl::SubmitPendingCallback()
{
    QueueEntry* dueEntry;
    QueueEntryNode* dueEntryNode;
    ScheduleNextPendingCallback(m_timerDue.load(), &dueEntry, &dueEntryNode);

    if (dueEntry != nullptr)
    {
        if (!AppendEntry(dueEntry, dueEntryNode))
        {
            ReleaseEntry(dueEntry);
        }
    }
}

// Called from thread pool callback
void TaskQueuePortImpl::ProcessThreadPoolCallback()
{
    referenced_ptr<ITaskQueuePort>(this);
    uint32_t wasProcessing = m_processingCallback++;
    if (m_dispatchMode == XTaskQueueDispatchMode::SerializedThreadPool)
    {
        if (wasProcessing == 0)
        {
            while (DrainOneItem());
        }
    }
    else
    {
        DrainOneItem();
    }
    m_processingCallback--;
}

void TaskQueuePortImpl::SignalQueue()
{
#ifdef _WIN32
    SetEvent(m_events[0]);
#endif
    m_event.notify_all();
}

void TaskQueuePortImpl::SignalTerminations()
{
    TerminationEntryNode* termNode;
    TerminationEntry* term = m_terminationList->pop_front(&termNode);
    TerminationEntry* initialPushBackNode = nullptr;
    
    while(term != nullptr)
    {
        if (term == initialPushBackNode)
        {
            m_terminationList->push_back(term, termNode);
            break;
        }
        
        if (term->owner->GetPortStatus(this) == TaskQueuePortStatus::Terminated)
        {
            term->callback(term->context);
            delete term;
            delete termNode;
        }
        else
        {
            if (initialPushBackNode == nullptr)
            {
                initialPushBackNode = term;
            }
            m_terminationList->push_back(term, termNode);
        }
        term = m_terminationList->pop_front();
    }
}

#ifdef _WIN32
void CALLBACK TaskQueuePortImpl::WaitCallback(
    _In_ PTP_CALLBACK_INSTANCE instance,
    _Inout_opt_ void* context,
    _Inout_ PTP_WAIT wait,
    _In_ TP_WAIT_RESULT waitResult)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(wait);

    if (waitResult == WAIT_OBJECT_0)
    {
        WaitRegistration* waitReg = static_cast<WaitRegistration*>(context);
        waitReg->port->ProcessWaitCallback(waitReg);
    }
}

HRESULT TaskQueuePortImpl::InitializeWaitRegistration(
    _In_ WaitRegistration* waitReg)
{
    if (waitReg->queueEntry->owner != nullptr)
    {
        waitReg->queueEntry->owner->Release();
        waitReg->queueEntry->owner = nullptr;
    }

    if (waitReg->threadpoolWait == nullptr)
    {
        waitReg->threadpoolWait = CreateThreadpoolWait(WaitCallback, waitReg, nullptr);
        RETURN_LAST_ERROR_IF_NULL(waitReg->threadpoolWait);
    }

    SetThreadpoolWait(waitReg->threadpoolWait, waitReg->waitHandle, nullptr);

    return S_OK;
}

// Like append entry this assumes the queue entry has
// already been addref'd
bool TaskQueuePortImpl::AppendWaitRegistrationEntry(
    _In_ WaitRegistration* waitReg,
    _In_ bool signal)
{
    // Prepare the queue entry for insert
    QueueEntry* entry = waitReg->queueEntry;
    ASSERT(entry->owner == nullptr);
    entry->owner = waitReg->owner;
    entry->owner->AddRef();

    bool success = AppendEntry(entry, nullptr, signal);

    if (!success)
    {
        entry->owner->Release();
        entry->owner = nullptr;
    }

    return success;
}

// Called from thread pool when a wait is satisfied
void TaskQueuePortImpl::ProcessWaitCallback(
    _In_ WaitRegistration* waitReg)
{
    QueueEntry* queueEntry = waitReg->queueEntry;
    queueEntry->refs++;
    if (!AppendWaitRegistrationEntry(waitReg))
    {
        LOG_IF_FAILED(InitializeWaitRegistration(waitReg));
        ReleaseEntry(queueEntry);
    }
}

#endif

//
// TaskQueueImpl
//

TaskQueueImpl::TaskQueueImpl()
    : Api()
    , m_callbackSubmitted(&m_header)
    , m_allowClose(true)
{
    m_header.m_signature = TASK_QUEUE_SIGNATURE;
    m_header.m_queue = this;

    m_termination.allowed = true;
    m_termination.terminated = false;
}

TaskQueueImpl::~TaskQueueImpl()
{
}

HRESULT TaskQueueImpl::Initialize(
    _In_ XTaskQueueDispatchMode workMode,
    _In_ XTaskQueueDispatchMode completionMode,
    _In_ bool allowTermination,
    _In_ bool allowClose)
{
    m_termination.allowed = allowTermination;
    m_allowClose = allowClose;

    referenced_ptr<TaskQueuePortImpl> work(new (std::nothrow) TaskQueuePortImpl);
    RETURN_IF_NULL_ALLOC(work);
    RETURN_IF_FAILED(work->Initialize(workMode, &m_callbackSubmitted));

    referenced_ptr<TaskQueuePortImpl> completion(new (std::nothrow) TaskQueuePortImpl);
    RETURN_IF_NULL_ALLOC(completion);
    RETURN_IF_FAILED(completion->Initialize(completionMode, &m_callbackSubmitted));
    
    work->GetHandle()->m_queue = this;
    completion->GetHandle()->m_queue = this;
    
    RETURN_IF_FAILED(work->QueryApi(ApiId::XTaskQueuePort, (void**)&m_work.port));
    RETURN_IF_FAILED(completion->QueryApi(ApiId::XTaskQueuePort, (void**)&m_completion.port));

    if (!allowClose)
    {
        // This queue will never be closed.  Subtract off the
        // global api refs so tests don't think there's a leak.  We
        // need to subtract off our own ref and the refs of each of our ports.
        ApiDiag::g_globalApiRefs -= 3;
    }

    return S_OK;
}

HRESULT TaskQueueImpl::Initialize(
    _In_ XTaskQueuePortHandle workPort,
    _In_ XTaskQueuePortHandle completionPort)
{
    RETURN_HR_IF(E_INVALIDARG, workPort== nullptr || workPort->m_signature != TASK_QUEUE_PORT_SIGNATURE);
    RETURN_HR_IF(E_INVALIDARG, completionPort == nullptr || completionPort->m_signature != TASK_QUEUE_PORT_SIGNATURE);
    
    m_work.port = referenced_ptr<ITaskQueuePort>(workPort->m_port);
    m_completion.port = referenced_ptr<ITaskQueuePort>(completionPort->m_port);
    m_work.source = referenced_ptr<ITaskQueue>(workPort->m_queue);
    m_completion.source = referenced_ptr<ITaskQueue>(completionPort->m_queue);

    m_termination.allowed = 
        m_work.source->CanTerminate() &&
        m_completion.source->CanTerminate();

    m_allowClose = true;
    
    return S_OK;
}

HRESULT __stdcall TaskQueueImpl::GetPortHandle(
    _In_ XTaskQueuePort port,
    _Out_ ITaskQueuePort** portHandle)
{
    RETURN_HR_IF(E_POINTER, portHandle == nullptr);
    
    switch(port)
    {
    case XTaskQueuePort::Work:
        *portHandle = m_work.port.get();
        m_work.port->AddRef();
        break;
        
    case XTaskQueuePort::Completion:
        *portHandle = m_completion.port.get();
        m_completion.port->AddRef();
        break;
        
    default:
        RETURN_HR(E_INVALIDARG);
    }
    
    return S_OK;
}

XTaskQueuePort __stdcall TaskQueueImpl::GetPort(
    _In_ ITaskQueuePort* portHandle)
{
    if (portHandle == m_work.port.get())
    {
        return XTaskQueuePort::Work;
    }
    else if (portHandle == m_completion.port.get())
    {
        return XTaskQueuePort::Completion;
    }
    else
    {
        ASSERT(false);
        return XTaskQueuePort::Work;
    }
}

TaskQueuePortStatus __stdcall TaskQueueImpl::GetPortStatus(
    _In_ ITaskQueuePort* portHandle)
{
    if (portHandle == m_work.port.get())
    {
        return m_work.status;
    }
    else if (portHandle == m_completion.port.get())
    {
        return m_completion.status;
    }
    else
    {
        ASSERT(false);
        return TaskQueuePortStatus::Terminated;
    }
}

bool __stdcall TaskQueueImpl::TrySetPortStatus(
    _In_ ITaskQueuePort* portHandle,
    _In_ TaskQueuePortStatus expectedStatus,
    _In_ TaskQueuePortStatus status)
{
    if (portHandle == m_work.port.get())
    {
        return m_work.status.compare_exchange_strong(expectedStatus, status);
    }
    else if (portHandle == m_completion.port.get())
    {
        return m_completion.status.compare_exchange_strong(expectedStatus, status);
    }
    else
    {
        ASSERT(false);
        return false;
    }
}

void __stdcall TaskQueueImpl::SetPortStatus(
    _In_ ITaskQueuePort* portHandle,
    _In_ TaskQueuePortStatus status)
{
    if (portHandle == m_work.port.get())
    {
        m_work.status = status;
    }
    else if (portHandle == m_completion.port.get())
    {
        m_completion.status = status;
    }
    else
    {
        ASSERT(false);
    }
}

HRESULT __stdcall TaskQueueImpl::RegisterWaitHandle(
    _In_ XTaskQueuePort port,
    _In_ HANDLE waitHandle,
    _In_opt_ void* callbackContext,
    _In_ XTaskQueueCallback* callback,
    _Out_ XTaskQueueRegistrationToken* token)
{
    RETURN_HR_IF(E_POINTER, callback == nullptr || token == nullptr);

    XTaskQueueRegistrationToken portToken;
    referenced_ptr<ITaskQueuePort> portHandle;

    RETURN_IF_FAILED(GetPortHandle(port, portHandle.address_of()));
    RETURN_IF_FAILED(portHandle->RegisterWaitHandle(this, port, waitHandle, callbackContext, callback, &portToken));

    HRESULT hr = m_waitRegistry.Register(port, portToken, token);
    if (FAILED(hr))
    {
        portHandle->UnregisterWaitHandle(portToken);
    }
    RETURN_IF_FAILED(hr);

    return S_OK;
}

void __stdcall TaskQueueImpl::UnregisterWaitHandle(
    _In_ XTaskQueueRegistrationToken token)
{
    std::pair<XTaskQueuePort, XTaskQueueRegistrationToken> pair = m_waitRegistry.Unregister(token);
    if (pair.second.token != 0)
    {
        referenced_ptr<ITaskQueuePort> portHandle;
        if (SUCCEEDED(GetPortHandle(pair.first, portHandle.address_of())))
        {
            portHandle->UnregisterWaitHandle(pair.second);
        }
    }
}

HRESULT __stdcall TaskQueueImpl::RegisterSubmitCallback(
    _In_opt_ void* context,
    _In_ XTaskQueueMonitorCallback* callback,
    _Out_ XTaskQueueRegistrationToken* token)
{
    return m_callbackSubmitted.Register(context, callback, token);
}

void __stdcall TaskQueueImpl::UnregisterSubmitCallback(
    _In_ XTaskQueueRegistrationToken token)
{
    m_callbackSubmitted.Unregister(token);
}

bool __stdcall TaskQueueImpl::CanTerminate()
{
    return m_termination.allowed;
}

bool __stdcall TaskQueueImpl::CanClose()
{
    return m_allowClose;
}

HRESULT __stdcall TaskQueueImpl::Terminate(
    _In_ bool wait, 
    _In_opt_ void* callbackContext, 
    _In_opt_ XTaskQueueTerminatedCallback* callback)
{
    RETURN_HR_IF(E_ACCESSDENIED, !m_termination.allowed);

    std::unique_ptr<TerminationEntry> entry(new (std::nothrow) TerminationEntry);
    RETURN_IF_NULL_ALLOC(entry);

    entry->owner = this;
    entry->level = TerminationLevel::Work;
    entry->context = callbackContext;
    entry->callback = callback;

    void* workToken;
    
    RETURN_IF_FAILED(m_work.port->PrepareTerminate(this, entry.get(), OnTerminationCallback, &workToken));

    HRESULT hr = m_completion.port->PrepareTerminate(this, entry.get(), OnTerminationCallback, &entry->completionPortToken);
    if (FAILED(hr))
    {
        m_work.port->CancelTermination(workToken);
        RETURN_HR(hr);
    }

    // At this point both ports have been marked for termination and have pre-alocated any state they
    // need, so we can proceed with the actual termination.
    
    entry.release();

    m_work.port->Terminate(workToken);
    
    if (wait)
    {
        std::unique_lock<std::mutex> lock(m_termination.lock);
        while(!m_termination.terminated)
        {
            m_termination.cv.wait(lock);
        }
    }

    return S_OK;
}

void TaskQueueImpl::FinalRelease()
{
    m_work.status = TaskQueuePortStatus::Terminated;
    m_completion.status = TaskQueuePortStatus::Terminated;
    m_work.port->Detach(this);
    m_completion.port->Detach(this);
}

void TaskQueueImpl::OnTerminationCallback(_In_ void* context)
{
    TerminationEntry* entry = static_cast<TerminationEntry*>(context);
    switch(entry->level)
    {
        case TerminationLevel::Work:
            entry->level = TerminationLevel::Completion;
            entry->owner->m_completion.port->Terminate(entry->completionPortToken);
            break;

        case TerminationLevel::Completion:
            if (entry->callback != nullptr)
            {
                entry->callback(entry->context);
            }

            entry->owner->m_termination.terminated = true;
            entry->owner->m_termination.cv.notify_all();

            delete entry;
            break;

        default:
            ASSERT(false);
    }
}

///////////////////
// XTaskQueue.h APIs
///////////////////

//
// Creates a Task Queue, which can be used to queue
// different calls together.
//
STDAPI XTaskQueueCreate(
    _In_ XTaskQueueDispatchMode workDispatchMode,
    _In_ XTaskQueueDispatchMode completionDispatchMode,
    _Out_ XTaskQueueHandle* queue
    ) noexcept
{
    referenced_ptr<TaskQueueImpl> aq(new (std::nothrow) TaskQueueImpl);
    RETURN_IF_NULL_ALLOC(aq);
    RETURN_IF_FAILED(aq->Initialize(
        workDispatchMode, 
        completionDispatchMode, 
        true, /* can terminate */ 
        true /* can close */));
    *queue = aq.release()->GetHandle();
    return S_OK;
}

/// <summary>
/// Returns the task queue port handle for the given
/// port. Task queue port handles are owned by the
/// task queue and do not have to be closed. They are
/// used when creating composite task queues.
/// </summary>
/// <param name='queue'>The task queue to get the port from.</param>
/// <param name='port'>The port to get.</param>
STDAPI XTaskQueueGetPort(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port,
    _Out_ XTaskQueuePortHandle* portHandle
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    if (aq == nullptr)
    {
        RETURN_HR(E_INVALIDARG);
    }
    
    referenced_ptr<ITaskQueuePort> aqs;
    RETURN_IF_FAILED(aq->GetPortHandle(port, aqs.address_of()));
    
    *portHandle = aqs->GetHandle();
    
    return S_OK;
}

/// <summary>
/// Creates a task queue composed of ports of other
/// task queue ports. A composite task queue will duplicate
/// the handles of queues that own the provided ports.
/// </summary>
/// <param name='workPort'>The port to use for queuing work callbacks.</param>
/// <param name='completionPort'>The port to use for queuing completion callbacks.</param>
STDAPI XTaskQueueCreateComposite(
     _In_ XTaskQueuePortHandle workPort,
     _In_ XTaskQueuePortHandle completionPort,
     _Out_ XTaskQueueHandle* queue
     ) noexcept
{
    referenced_ptr<TaskQueueImpl> aq(new (std::nothrow) TaskQueueImpl);
    RETURN_IF_NULL_ALLOC(aq);
    RETURN_IF_FAILED(aq->Initialize(workPort, completionPort));
    *queue = aq.release()->GetHandle();
    return S_OK;
}

//
// Processes items in the task queue of the given type. If an item
// is processed this will return TRUE. If there are no items to process
// this returns FALSE.  You can pass a timeout, which will cause
// XTaskQueueDispatch to wait for something to arrive in the queue.
//
STDAPI_(bool) XTaskQueueDispatch(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port,
    _In_ uint32_t timeoutInMs
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    if (aq == nullptr)
    {
        return false;
    }
    
    referenced_ptr<ITaskQueuePort> s;
    if (FAILED(aq->GetPortHandle(port, s.address_of())))
    {
        return false;
    }
        
    bool found = s->DrainOneItem();
    if (!found && timeoutInMs != 0)
    {
        found = s->Wait(GetQueue(queue), timeoutInMs);
        if (found) s->DrainOneItem();
    }

    return found;
}

//
// Returns TRUE if there is no outstanding work in this
// queue.  Note this API is only used for testing and
// is not exposed outside the library.
//
STDAPI_(bool) XTaskQueueIsEmpty(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    if (aq == nullptr)
    {
        return false;
    }
    
    referenced_ptr<ITaskQueuePort> s;
    if (FAILED(aq->GetPortHandle(port, s.address_of())))
    {
        return false;
    }

    return s->IsEmpty();
}

//
// Closes the task queue.  A queue can only be closed if it
// is not in use by a task or is empty.  If not true, the queue
// will be marked for closure and closed when it can. 
//
STDAPI_(void) XTaskQueueCloseHandle(
    _In_ XTaskQueueHandle queue
    ) noexcept
{
    ITaskQueue* aq = GetQueue(queue);
    if (aq != nullptr && aq->CanClose())
    {
        aq->Release();
    }
}

//
// Terminates a task queue by canceling all pending items and
// preventning new items from being queued.  Once a queue is terminated
// its handle can be closed.
//
STDAPI XTaskQueueTerminate(
    _In_ XTaskQueueHandle queue, 
    _In_ bool wait, 
    _In_opt_ void* callbackContext, 
    _In_opt_ XTaskQueueTerminatedCallback* callback
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    return aq->Terminate(wait, callbackContext, callback);
}

//
// Submits either a work or completion callback immediately.
//
STDAPI XTaskQueueSubmitCallback(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port,
    _In_opt_ void* callbackContext,
    _In_ XTaskQueueCallback* callback
    ) noexcept
{
    return XTaskQueueSubmitDelayedCallback(queue, port, 0, callbackContext, callback);
}

//
// Submits either a work or completion callback.
//
STDAPI XTaskQueueSubmitDelayedCallback(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port,
    _In_ uint32_t delayMs,
    _In_opt_ void* callbackContext,
    _In_ XTaskQueueCallback* callback
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    RETURN_HR_IF(E_INVALIDARG, aq == nullptr);

    referenced_ptr<ITaskQueuePort> s;
    RETURN_IF_FAILED(aq->GetPortHandle(port, s.address_of()));

    RETURN_HR(s->QueueItem(aq.get(), port, delayMs, callbackContext, callback));
}

//
// Registers a wait handle with the task queue.  When the wait handle
// is satisfied the task queue will invoke the given calback. This
// provides an efficient way to add items to a task queue in 
// response to handles becomming signaled.
//
STDAPI XTaskQueueRegisterWaiter(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port,
    _In_ HANDLE waitHandle,
    _In_opt_ void* callbackContext,
    _In_ XTaskQueueCallback* callback,
    _Out_ XTaskQueueRegistrationToken* token
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    RETURN_HR_IF(E_INVALIDARG, aq == nullptr);
    RETURN_IF_FAILED(aq->RegisterWaitHandle(port, waitHandle, callbackContext, callback, token));
    return S_OK;
}

//
// Unregisters a previously registered task queue waiter.
//
STDAPI_(void) XTaskQueueUnregisterWaiter(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueueRegistrationToken token
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    if (aq != nullptr)
    {
        aq->UnregisterWaitHandle(token);
    }
}

//
// Increments the refcount on the queue
//
STDAPI XTaskQueueDuplicateHandle(
    _In_ XTaskQueueHandle queueHandle,
    _Out_ XTaskQueueHandle* duplicatedHandle
    ) noexcept
{
    RETURN_HR_IF(E_POINTER, duplicatedHandle == nullptr);

    auto queue = GetQueue(queueHandle);
    RETURN_HR_IF(E_INVALIDARG, queue == nullptr);

    // For queues that cannot be closed, don't add ref, since
    // closing does nothing.
    if (queue->CanClose())
    {
        queue->AddRef();
    }

    *duplicatedHandle = queueHandle;

    return S_OK;
}

//
// Registers a callback that will be called when a new callback
// is submitted. The callback will be directly invoked when
// the call is submitted.
//
STDAPI XTaskQueueRegisterMonitor(
    _In_ XTaskQueueHandle queue,
    _In_opt_ void* callbackContext,
    _In_ XTaskQueueMonitorCallback* callback,
    _Out_ XTaskQueueRegistrationToken* token
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    RETURN_HR_IF(E_INVALIDARG, aq == nullptr);
    RETURN_HR(aq->RegisterSubmitCallback(callbackContext, callback, token));
}

//
// Unregisters a previously added callback.
//
STDAPI_(void) XTaskQueueUnregisterMonitor(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueueRegistrationToken token
    ) noexcept
{
    referenced_ptr<ITaskQueue> aq(GetQueue(queue));
    if (aq != nullptr)
    {
        aq->UnregisterSubmitCallback(token);
    }
}

//
// Returns a handle to the process task queue, or nullptr if there is no
// process task queue.  By default, there is a default process task queue
// that uses the thread pool for both work and completion ports.
//
STDAPI_(bool) XTaskQueueGetCurrentProcessTaskQueue(
    _Out_ XTaskQueueHandle* queue
    ) noexcept
{
    XTaskQueueHandle processQueue = ProcessGlobals::g_processQueue;
    if (processQueue == ProcessGlobals::g_invalidQueueHandle)
    {
        XTaskQueueHandle defaultProcessQueue = ProcessGlobals::g_defaultProcessQueue;
        if (defaultProcessQueue == ProcessGlobals::g_invalidQueueHandle)
        {
            // The default process queue hasn't been created yet.  Create it locally
            // then swap it into the atomic.

            referenced_ptr<TaskQueueImpl> aq(new (std::nothrow) TaskQueueImpl);
            if (aq != nullptr && SUCCEEDED(aq->Initialize(
                XTaskQueueDispatchMode::ThreadPool,
                XTaskQueueDispatchMode::ThreadPool,
                false, /* can terminate */
                false /* can close */)))
            {
                XTaskQueueHandle expected = ProcessGlobals::g_invalidQueueHandle;
                if (ProcessGlobals::g_defaultProcessQueue.compare_exchange_strong(
                    expected,
                    aq->GetHandle()))
                {
                    // We successfully set the default handle.
                    aq.release();
                }
            }

            defaultProcessQueue = ProcessGlobals::g_defaultProcessQueue;
        }

        processQueue = defaultProcessQueue;
    }

    if (processQueue == ProcessGlobals::g_invalidQueueHandle)
    {
        processQueue = nullptr;
    }

    if (processQueue != nullptr)
    {
        *queue = processQueue;

        // The default process queue does not addref or release.
        if (processQueue->m_queue->CanClose())
        {
            processQueue->m_queue->AddRef();
        }
    }
    else
    {
        *queue = nullptr;
    }

    return processQueue != nullptr;
}

//
// Sets the given task queue as the process wide task queue.  The
// queue can be set to nullptr, in which case XTaskQueueGetCurrentProcessTaskQueue will
// also return nullptr. The provided queue will have its handle duplicated
// and any existing process task queue will have its handle closed.
//
STDAPI_(void) XTaskQueueSetCurrentProcessTaskQueue(
    _In_ XTaskQueueHandle queue
    ) noexcept
{
    XTaskQueueHandle newQueue = nullptr;
    if (queue != nullptr)
    {
        XTaskQueueDuplicateHandle(queue, &newQueue);
    }

    XTaskQueueHandle previous = ProcessGlobals::g_processQueue.exchange(newQueue);
    if (previous != nullptr && previous != ProcessGlobals::g_invalidQueueHandle)
    {
        XTaskQueueCloseHandle(previous);
    }
}
