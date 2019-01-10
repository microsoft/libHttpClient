#include "pch.h"
#include "ThreadPool.h"

class ThreadPoolImpl
{
public:

    ~ThreadPoolImpl() noexcept
    {
        Terminate();
    }

    void AddRef()
    {
        m_refs++;
    }

    void Release()
    {
        if (--m_refs == 0)
        {
            delete this;
        }
    }

    HRESULT Initialize(
        _In_opt_ void* context,
        _In_ ThreadPoolCallback* callback) noexcept
    {
        m_context = context;
        m_callback = callback;
        m_work = CreateThreadpoolWork(TPCallback, this, nullptr);
        RETURN_LAST_ERROR_IF_NULL(m_work);

        InitializeCriticalSection(&m_cs);
        InitializeConditionVariable(&m_cv);

        return S_OK;
    }

    void Terminate() noexcept
    {
        if (m_work != nullptr)
        {
            // We cannot wait for work callbacks to complete because
            // the final release may be called from within the callback.
            // We know when our callbacks are invoking user code however,
            // and we block on that.
            EnterCriticalSection(&m_cs);

            while (m_activeCalls != 0)
            {
                SleepConditionVariableCS(&m_cv, &m_cs, INFINITE);
            }

            LeaveCriticalSection(&m_cs);

            CloseThreadpoolWork(m_work);
            m_work = nullptr;
            DeleteCriticalSection(&m_cs);
        }
    }

    void Submit() noexcept
    {
        m_activeCalls++;
        SubmitThreadpoolWork(m_work);
    }

private:

    static void CALLBACK TPCallback(
        _In_ PTP_CALLBACK_INSTANCE, 
        _In_ void* context, PTP_WORK) noexcept
    {
        ThreadPoolImpl* pthis = static_cast<ThreadPoolImpl*>(context);

        // ActionComplete is an optional call
        // the callback can make to indicate 
        // all portions of the call have finished
        // and it is safe to release the
        // thread pool, even if the callback has
        // not totally unwound.  This is neccessary
        // to allow users to close a task queue from
        // within a callback.  Task queue guards with an 
        // extra ref to ensure a safe point where 
        // member state is no longer accessed, but the
        // final release does need to wait on outstanding
        // calls.

        ActionCompleteImpl ac(pthis);
        pthis->AddRef();
        pthis->m_callback(pthis->m_context, ac);

        if (!ac.Invoked)
        {
            ac();
        }
        pthis->Release(); // May delete this
    }

    struct ActionCompleteImpl : ThreadPoolActionComplete
    {
        ActionCompleteImpl(ThreadPoolImpl* owner) :
            m_owner(owner)
        {
        }

        bool Invoked = false;

        void operator()() override
        {
            Invoked = true;
            m_owner->m_activeCalls--;
            WakeAllConditionVariable(&m_owner->m_cv);
        }

    private:
        ThreadPoolImpl* m_owner = nullptr;
    };

    std::atomic<uint32_t> m_refs { 1 };
    CONDITION_VARIABLE m_cv;
    CRITICAL_SECTION m_cs;
    PTP_WORK m_work = nullptr;
    void* m_context = nullptr;
    std::atomic<uint32_t> m_activeCalls { 0 };
    ThreadPoolCallback* m_callback = nullptr;
};

ThreadPool::ThreadPool() noexcept :
    m_impl(nullptr)
{
}

ThreadPool::~ThreadPool() noexcept
{
    Terminate();
}

HRESULT ThreadPool::Initialize(_In_opt_ void* context, _In_ ThreadPoolCallback* callback) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, m_impl != nullptr);
    
    std::unique_ptr<ThreadPoolImpl> impl(new (std::nothrow) ThreadPoolImpl);
    RETURN_IF_NULL_ALLOC(impl);

    RETURN_IF_FAILED(impl->Initialize(context, callback));

    m_impl = impl.release();
    return S_OK;
}

void ThreadPool::Terminate() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->Terminate();
        m_impl->Release();
        m_impl = nullptr;
    }
}

void ThreadPool::Submit()
{
    m_impl->Submit();
}
