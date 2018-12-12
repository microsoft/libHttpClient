// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "LocklessList.h"
#include "StaticArray.h"
#include "ThreadPool.h"
#include "WaitTimer.h"

namespace ApiDiag
{
    void GlobalAddRef();
    void GlobalRelease();
}

template <ApiId iid, typename TInterface>
class Api : public TInterface
{
public:

    virtual ~Api() 
    {
    }
    
    uint32_t __stdcall AddRef()
    {
        ApiDiag::GlobalAddRef();
        return m_refs++;
    }

    uint32_t __stdcall Release()
    {
        ApiDiag::GlobalRelease();
        uint32_t refs = --m_refs;
        if (refs == 0)
        {
            delete this;
        }
        return refs;
    }

    HRESULT __stdcall QueryApi(ApiId id, void** ptr)
    {
        if (ptr == nullptr)
        {
            return E_POINTER;
        }

        *ptr = QueryApiImpl(id);

        if ((*ptr) != nullptr)
        {
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

protected:

    virtual void* QueryApiImpl(ApiId id) 
    {
        if (id == ApiId::Identity || id == iid)
        {
            return static_cast<TInterface*>(this);
        }
        return nullptr;
    }
    
private:
    std::atomic<uint32_t> m_refs{ 0 };
};

static uint32_t const SUBMIT_CALLBACK_MAX = 32;

class SubmitCallback
{
public:

    SubmitCallback(_In_ XTaskQueueHandle queue);

    HRESULT Register(_In_ void* context, _In_ XTaskQueueMonitorCallback* callback, _Out_ XTaskQueueRegistrationToken* token);
    void Unregister(_In_ XTaskQueueRegistrationToken token);
    void Invoke(_In_ XTaskQueuePort port);

private:

    struct CallbackRegistration
    {
        uint64_t Token;
        void* Context;
        XTaskQueueMonitorCallback* Callback;
    };

    std::atomic<uint64_t> m_nextToken{ 0 };
    std::mutex m_lock;
    CallbackRegistration m_buffer1[SUBMIT_CALLBACK_MAX];
    CallbackRegistration m_buffer2[SUBMIT_CALLBACK_MAX];
    CallbackRegistration* m_buffers[2]= { m_buffer1, m_buffer2 };
    std::atomic<uint32_t> m_indexAndRef = { 0 };
    XTaskQueueHandle m_queue;
};

#define PORT_WAIT_MAX 60
#define PORT_EVENT_MAX (PORT_WAIT_MAX + 1)
#define QUEUE_WAIT_MAX (PORT_WAIT_MAX * 2)

class QueueWaitRegistry
{
public:

    HRESULT Register(
        _In_ XTaskQueuePort port,
        _In_ const XTaskQueueRegistrationToken& portToken,
        _Out_ XTaskQueueRegistrationToken* token);

    std::pair<XTaskQueuePort, XTaskQueueRegistrationToken> Unregister(
        _In_ const XTaskQueueRegistrationToken& token);

private:

    struct WaitRegistration
    {
        uint64_t Token;
        uint64_t PortToken;
        XTaskQueuePort Port;
    };

    std::atomic<uint64_t> m_nextToken{ 0 };
    StaticArray<WaitRegistration, QUEUE_WAIT_MAX> m_callbacks;
    std::mutex m_lock;
};

class TaskQueuePortImpl: public Api<ApiId::XTaskQueuePort, ITaskQueuePort>
{
public:

    TaskQueuePortImpl();
    virtual ~TaskQueuePortImpl();

    HRESULT Initialize(
        _In_ XTaskQueuePort port, 
        _In_ XTaskQueueDispatchMode mode, 
        _Out_ SubmitCallback* submitCallback);

    XTaskQueuePortHandle __stdcall GetHandle() { return &m_header; }

    HRESULT __stdcall QueueItem(
        ITaskQueue* owner,
        uint32_t waitMs,
        void* context,
        XTaskQueueCallback* callback);

    HRESULT __stdcall RegisterWaitHandle(
        ITaskQueue* owner,
        _In_ HANDLE waitHandle,
        _In_opt_ void* callbackContext,
        _In_ XTaskQueueCallback* callback,
        _Out_ XTaskQueueRegistrationToken* token);

    void __stdcall UnregisterWaitHandle(
        _In_ XTaskQueueRegistrationToken token);

    HRESULT __stdcall PrepareTerminate(
        _In_ void* context,
        _In_ XTaskQueueTerminatedCallback* callback,
        _Out_ void** token);

    void __stdcall CancelTermination(
        _In_ void* token);

    void __stdcall Terminate(
        _In_ void* token);

    bool __stdcall DrainOneItem();

    bool __stdcall Wait(
        _In_ uint32_t timeout);

    bool __stdcall IsEmpty();

private:

    struct WaitRegistration;

    struct QueueEntry
    {
        IApi* owner;
        void* context;
        XTaskQueueCallback* callback;
        WaitRegistration* waitRegistration;
        uint64_t enqueueTime;
        std::atomic<uint32_t> refs;
    };

    typedef LocklessList<QueueEntry>::Node QueueEntryNode;

    enum class PortStatus
    {
        Active,
        Canceled,
        Terminating,
        Terminated
    };

    struct TerminationEntry
    {
        void* context;
        XTaskQueueTerminatedCallback* callback;
        LocklessList<TerminationEntry>::Node* node;
    };

    typedef LocklessList<TerminationEntry>::Node TerminationEntryNode;

#ifdef _WIN32
    struct WaitRegistration
    {
        uint64_t token;
        HANDLE waitHandle;
        HANDLE registeredWaitHandle;
        TaskQueuePortImpl* port;
        IApi* owner;
        QueueEntry* queueEntry;
    };
#endif

    XTaskQueuePortObject m_header = { };
    XTaskQueuePort m_type = XTaskQueuePort::Work;
    XTaskQueueDispatchMode m_dispatchMode = XTaskQueueDispatchMode::Manual;
    SubmitCallback* m_callbackSubmitted = nullptr;
    std::atomic<PortStatus> m_status = { PortStatus::Active };
    std::atomic<uint32_t> m_processingCallback{ 0 };
    std::condition_variable_any m_event;
    std::mutex m_lock;
    std::unique_ptr<LocklessList<QueueEntry>> m_queueList;
    std::unique_ptr<LocklessList<QueueEntry>> m_pendingList;
    std::unique_ptr<LocklessList<TerminationEntry>> m_terminationList;
    WaitTimer m_timer;
    ThreadPool m_threadPool;
    std::atomic<uint64_t> m_timerDue = { UINT64_MAX };

#ifdef _WIN32
    StaticArray<WaitRegistration*, PORT_WAIT_MAX> m_waits;
    StaticArray<HANDLE, PORT_EVENT_MAX> m_events;
    uint64_t m_nextWaitToken = 0;
#endif

    HRESULT VerifyNotTerminated();

    // Appends the given entry to the active queue.  The entry should already
    // be add-refd.
    bool AppendEntry(
        _In_ QueueEntry* entry,
        _In_opt_ QueueEntryNode* node = nullptr,
        _In_ bool signal = true);

    // Releases the entry.
    void ReleaseEntry(
        _In_ QueueEntry* entry);

    static void EraseQueue(
        _In_opt_ LocklessList<QueueEntry>* queue);

    void ScheduleNextPendingCallback(
        _In_ uint64_t dueTime,
        _Out_ QueueEntry** dueEntry,
        _Out_ QueueEntryNode** dueEntryNode);

    void SubmitPendingCallback();

    void SignalTerminations();

    void SignalQueue();

    void ProcessThreadPoolCallback();

#ifdef _WIN32
    HRESULT InitializeWaitRegistration(
        _In_ WaitRegistration* waitReg);

    bool AppendWaitRegistrationEntry(
        _In_ WaitRegistration* waitReg,
        _In_ bool signal = true);

    void ProcessWaitCallback(
        _In_ WaitRegistration* waitReg);

    static void CALLBACK WaitCallback(
        _In_ PVOID   parameter,
        _In_ BOOLEAN timerOrWaitFired);
#endif
};

class TaskQueueImpl : public Api<ApiId::TaskQueue, ITaskQueue>
{
public:

    TaskQueueImpl();
    virtual ~TaskQueueImpl();

    HRESULT Initialize(
        _In_ XTaskQueueDispatchMode workMode,
        _In_ XTaskQueueDispatchMode completionMode,
        _In_ bool allowTermination,
        _In_ bool allowClose);
    
    HRESULT Initialize(
        _In_ XTaskQueuePortHandle workPort,
        _In_ XTaskQueuePortHandle completionPort);
    
    XTaskQueueHandle __stdcall GetHandle() { return &m_header; }

    HRESULT __stdcall GetPort(
        _In_ XTaskQueuePort port,
        _Out_ ITaskQueuePort** portHandle);
    
    HRESULT __stdcall RegisterWaitHandle(
        _In_ XTaskQueuePort port,
        _In_ HANDLE waitHandle,
        _In_opt_ void* callbackContext,
        _In_ XTaskQueueCallback* callback,
        _Out_ XTaskQueueRegistrationToken* token);

    void __stdcall UnregisterWaitHandle(
        _In_ XTaskQueueRegistrationToken token);

    HRESULT __stdcall RegisterSubmitCallback(
        _In_opt_ void* context,
        _In_ XTaskQueueMonitorCallback* callback,
        _Out_ XTaskQueueRegistrationToken* token);
    
    void __stdcall UnregisterSubmitCallback(
        _In_ XTaskQueueRegistrationToken token);

    bool __stdcall CanTerminate();
    bool __stdcall CanClose();

    HRESULT __stdcall Terminate(
        _In_ bool wait, 
        _In_opt_ void* callbackContext, 
        _In_opt_ XTaskQueueTerminatedCallback* callback);

private:

    static void CALLBACK OnTerminationCallback(_In_ void* context);

private:

    enum class TerminationLevel
    {
        None,
        Work,
        Completion
    };

    struct TerminationEntry
    {
        TaskQueueImpl* owner;
        TerminationLevel level;
        void* completionPortToken;
        void* context;
        XTaskQueueTerminatedCallback* callback;
    };

    struct TerminationData
    {
        bool allowed;
        bool terminated;
        std::mutex lock;
        std::condition_variable cv;
    };

    XTaskQueueObject m_header = { };
    SubmitCallback m_callbackSubmitted;
    QueueWaitRegistry m_waitRegistry;
    TerminationData m_termination;
    bool m_allowClose;
    referenced_ptr<ITaskQueuePort> m_work;
    referenced_ptr<ITaskQueuePort> m_completion;
    referenced_ptr<ITaskQueue> m_workSource;
    referenced_ptr<ITaskQueue> m_completionSource;
};

inline ITaskQueue* GetQueue(XTaskQueueHandle handle)
{
    if (handle->m_signature != TASK_QUEUE_SIGNATURE)
    {
        ASSERT("Invalid XTaskQueueHandle");
        return nullptr;
    }
    
    return handle->m_queue;
}
