// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "WaitTimer.h"

template <typename TInterface>
struct referenced_ptr
{
    referenced_ptr()
        : _ptr(nullptr)
    {
    }

    referenced_ptr(TInterface* ptr)
        : _ptr(ptr)
    {
        if (ptr) ptr->AddRef();
    }

    referenced_ptr(const referenced_ptr&) = delete;
    referenced_ptr(referenced_ptr&&) = delete;
    referenced_ptr& operator=(const referenced_ptr&&) = delete;

    referenced_ptr& operator=(TInterface* ptr)
    {
        reset();
        _ptr = ptr;
        if (ptr) ptr->AddRef();
        return *this;
    }
    
    ~referenced_ptr()
    {
        reset();
    }
    
    TInterface* get() 
    {
        return _ptr; 
    }
    
    TInterface* release()
    {
        TInterface* ptr = _ptr;
        _ptr = nullptr;
        return ptr;
    }
    
    void reset()
    {
        if (_ptr) _ptr->Release();
        _ptr = nullptr;
    }
    
    explicit operator bool() const noexcept
    {
        return _ptr != nullptr;
    }
    
    TInterface* operator->() const noexcept
    {
        return _ptr;
    }
    
    TInterface** operator&() noexcept
    {
        reset();
        return &_ptr;
    }
        
private:
    TInterface* _ptr = nullptr;
};

template <typename TInterface>
bool operator==(referenced_ptr<TInterface>& p, nullptr_t)
{
    return p.get() == nullptr;
}

template <typename TInterface>
bool operator!=(referenced_ptr<TInterface>& p, nullptr_t)
{
    return p.get() != nullptr;
}

struct spinlock
{
    void lock() 
    {
        if (IsSingleProcSystem())
        {
            m_mutex.lock();
        }
        else
        {
            while (m_locked.test_and_set(std::memory_order_acquire)) { ; }
            m_tid = std::this_thread::get_id();
        }
    }
    
    void unlock() 
    {
        if (IsSingleProcSystem())
        {
            m_mutex.unlock();
        }
        else
        {
            m_tid = {};
            m_locked.clear(std::memory_order_release);
        }
    }

    bool owned()
    {
        return std::this_thread::get_id() == m_tid;
    }
    
private:
    std::atomic_flag m_locked = ATOMIC_FLAG_INIT;
    std::mutex m_mutex;
    std::thread::id m_tid;
    static bool IsSingleProcSystem();
};

namespace ApiDiag
{
    extern std::atomic<uint32_t> g_globalApiRefs;
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
        ApiDiag::g_globalApiRefs++;
        return m_refs++;
    }

    uint32_t __stdcall Release()
    {
        ApiDiag::g_globalApiRefs--;
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

    SubmitCallback(_In_ async_queue_handle_t queue)
        : m_queue(queue)
    {
    }

    HRESULT Register(_In_ void* context, _In_ AsyncQueueCallbackSubmitted* callback, _Out_ registration_token_t* token);
    void Unregister(_In_ registration_token_t token);
    void Invoke(_In_ AsyncQueueCallbackType type);

private:

    struct CallbackRegistration
    {
        registration_token_t Token;
        void* Context;
        AsyncQueueCallbackSubmitted* Callback;
    };

    std::atomic<registration_token_t> m_nextToken{ 0 };
    CallbackRegistration m_callbacks[SUBMIT_CALLBACK_MAX];
    uint32_t m_callbackCount = 0;
    spinlock m_lock;
    async_queue_handle_t m_queue;
};

class AsyncQueueSection : public Api<ApiId::AsyncQueueSection, IAsyncQueueSection>
{
public:

    AsyncQueueSection();
    virtual ~AsyncQueueSection();

    HRESULT Initialize(
        _In_ AsyncQueueCallbackType type, 
        _In_ AsyncQueueDispatchMode mode, 
        _Out_ SubmitCallback* submitCallback);

    HRESULT __stdcall QueueItem(
        IAsyncQueue* owner,
        uint32_t waitMs,
        void* context,
        AsyncQueueCallback* callback);

    void __stdcall RemoveItems(
       _In_ AsyncQueueCallback* searchCallback,
       _In_opt_ void* predicateContext,
       _In_ AsyncQueueRemovePredicate* removePredicate);

    bool __stdcall DrainOneItem();

    bool __stdcall Wait(
        _In_ uint32_t timeout);

    bool __stdcall IsEmpty();
    
    void ProcessCallback();

private:

    AsyncQueueCallbackType m_type = AsyncQueueCallbackType_Work;
    AsyncQueueDispatchMode m_dispatchMode = AsyncQueueDispatchMode_Manual;
    SubmitCallback* m_callbackSubmitted = nullptr;
    bool m_hasItems = false;
    std::atomic<uint32_t> m_processingCallback{ 0 };
    std::condition_variable_any m_event;
    std::condition_variable_any m_busy;
    spinlock m_lock;
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
        IApi* owner;
        void* context;
        AsyncQueueCallback* callback;
        uint64_t enqueueTime;
        uint32_t refs;
        bool busy;
        bool detached;
    };

    // Appends the given entry to the active queue.  The entry should already
    // be add-refd, and this API owns the lifetime of the entry.  If the
    // API fails, the entry will be released and deleted.
    HRESULT AppendEntry(
        _In_ QueueEntry* entry);

    // Releases the entry and optionally removes it from its owning list.
    // Also clears the entry busy flag and, if it was set, notifies the
    // m_busy condition variable.
    void ReleaseEntry(
        _In_ QueueEntry* entry,
        _In_ bool remove);

    bool RemoveItems(
        _In_ PLIST_ENTRY queueHead,
        _In_ AsyncQueueCallback* searchCallback,
        _In_opt_ void* predicateContext,
        _In_ AsyncQueueRemovePredicate* removePredicate);

    QueueEntry* EnumNextRemoveCandidate(
        _In_ AsyncQueueCallback* searchCallback,
        _In_ PLIST_ENTRY head,
        _In_ PLIST_ENTRY current,
        _In_ bool removeCurrent,
        _Out_ bool& restart);

    void EraseQueue(
        _In_ PLIST_ENTRY head);

    void ScheduleNextPendingCallback(
        _Out_opt_ QueueEntry** dueEntry);

    void SubmitPendingCallback();
};

class AsyncQueue : public Api<ApiId::AsyncQueue, IAsyncQueue>
{
public:

    AsyncQueue();
    virtual ~AsyncQueue();

    HRESULT Initialize(
        _In_ AsyncQueueDispatchMode workMode,
        _In_ AsyncQueueDispatchMode completionMode);
    
    async_queue_handle_t GetHandle()
    {
        return &m_header;
    }

    HRESULT __stdcall GetSection(
        _In_ AsyncQueueCallbackType type,
        _Out_ IAsyncQueueSection** section);
    
    HRESULT __stdcall RegisterSubmitCallback(
        _In_opt_ void* context,
        _In_ AsyncQueueCallbackSubmitted* callback,
        _Out_ registration_token_t* token);
    
    void __stdcall UnregisterSubmitCallback(
        _In_ registration_token_t token);

protected:

    referenced_ptr<IAsyncQueueSection> m_work;
    referenced_ptr<IAsyncQueueSection> m_completion;

private:

    async_queue_t m_header = { };
    SubmitCallback m_callbackSubmitted;
};

inline IAsyncQueue* GetQueue(async_queue_handle_t handle)
{
    if (handle->m_signature != ASYNC_QUEUE_SIGNATURE)
    {
        ASSERT("Invalid async_queue_handle_t");
        return nullptr;
    }
    
    return handle->m_queue;
}
