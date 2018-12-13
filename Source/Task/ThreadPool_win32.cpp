#include "pch.h"
#include "ThreadPool.h"

class ThreadPoolImpl
{
public:

    ~ThreadPoolImpl() noexcept
    {
        Terminate();
    }

    HRESULT Initialize(
        _In_opt_ void* context,
        _In_ ThreadPoolCallback* callback) noexcept
    {
        m_context = context;
        m_callback = callback;
        m_work = CreateThreadpoolWork(TPCallback, this, nullptr);

        RETURN_LAST_ERROR_IF_NULL(m_work);
        return S_OK;
    }

    void Terminate() noexcept
    {
        if (m_work != nullptr)
        {
            WaitForThreadpoolWorkCallbacks(m_work, TRUE);
            CloseThreadpoolWork(m_work);
            m_work = nullptr;
        }
    }

    void Submit() noexcept
    {
        SubmitThreadpoolWork(m_work);
    }

private:

    static void CALLBACK TPCallback(
        _In_ PTP_CALLBACK_INSTANCE, 
        _In_ void* context, PTP_WORK) noexcept
    {
        ThreadPoolImpl* pthis = static_cast<ThreadPoolImpl*>(context);
        pthis->m_callback(pthis->m_context);
    }

    PTP_WORK m_work = nullptr;
    void* m_context = nullptr;
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
        delete m_impl;
        m_impl = nullptr;
    }
}

void ThreadPool::Submit()
{
    m_impl->Submit();
}
