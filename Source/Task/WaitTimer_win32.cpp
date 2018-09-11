#include "pch.h"
#include "WaitTimer.h"

class WaitTimerImpl
{
public:
    WaitTimerImpl()
        : m_timer(nullptr)
        , m_wait(nullptr)
    {}

    ~WaitTimerImpl()
    {
        if (m_timer != nullptr)
        {
            (void)CancelWaitableTimer(m_timer);
            CloseHandle(m_timer);
        }

        if (m_wait != nullptr)
        {
            UnregisterWaitEx(m_wait, nullptr);
        }
    }

    HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback)
    {
        m_context = context;
        m_callback = callback;

        m_timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        RETURN_LAST_ERROR_IF_NULL(m_timer);

        BOOL success = RegisterWaitForSingleObject(&m_wait, m_timer, WaitCallback, this, INFINITE, WT_EXECUTEINWAITTHREAD);
        RETURN_LAST_ERROR_IF(!success);

        return S_OK;
    }

    void Start(_In_ uint64_t absoluteTime)
    {
        LARGE_INTEGER li;
        ASSERT((absoluteTime & 0x8000000000000000) == 0);
        li.QuadPart = static_cast<LONGLONG>(absoluteTime);
        BOOL success = SetWaitableTimer(m_timer, &li, 0, nullptr, nullptr, FALSE);
        if (success == FALSE)
        {
            // Short of providing invalid arguments, the code path for this never fails.
            ASSERT(FALSE);
            ASYNC_LIB_TRACE(HRESULT_FROM_WIN32(GetLastError()), "Failed to set waitable timer");
        }
    }

    void Cancel()
    {
        // Canceling a timer does not reset its signaled state,  We've set this up as 
        // an auto reset timer so all that is needed is to wait on it
        WaitForSingleObject(m_timer, 0);
        BOOL success = CancelWaitableTimer(m_timer);
        if (success == FALSE)
        {
            // Short of providing invalid arguments, the code path for this never fails.
            ASSERT(FALSE);
            ASYNC_LIB_TRACE(HRESULT_FROM_WIN32(GetLastError()), "Failed to cancel waitable timer");
        }
    }

private:

    HANDLE m_timer;
    HANDLE m_wait;
    void* m_context;
    WaitTimerCallback* m_callback;

    static VOID CALLBACK WaitCallback(_In_ PVOID context, _In_ BOOLEAN timeout)
    {
        if (timeout == FALSE)
        {
            WaitTimerImpl* pthis = static_cast<WaitTimerImpl*>(context);
            pthis->m_callback(pthis->m_context);
        }
    }
};

WaitTimer::WaitTimer() noexcept
    : m_impl(nullptr)
{}

WaitTimer::~WaitTimer() noexcept
{
    if (m_impl != nullptr)
    {
        delete m_impl;
    }
}

HRESULT WaitTimer::Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback) noexcept
{
    if (m_impl != nullptr || callback == nullptr)
    {
        ASSERT(FALSE);
        return E_UNEXPECTED;
    }

    std::unique_ptr<WaitTimerImpl> timer(new (std::nothrow) WaitTimerImpl);
    RETURN_IF_NULL_ALLOC(timer.get());
    RETURN_IF_FAILED(timer->Initialize(context, callback));

    m_impl = timer.release();

    return S_OK;
}

void WaitTimer::Start(_In_ uint64_t absoluteTime) noexcept
{
    m_impl->Start(absoluteTime);
}

void WaitTimer::Cancel() noexcept
{
    m_impl->Cancel();
}

uint64_t WaitTimer::GetAbsoluteTime(_In_ uint32_t msFromNow) noexcept
{
    FILETIME ft;
    LARGE_INTEGER li;
    GetSystemTimeAsFileTime(&ft);
    ASSERT((ft.dwHighDateTime & 0x80000000) == 0);
    li.HighPart = ft.dwHighDateTime;
    li.LowPart = ft.dwLowDateTime;
    li.QuadPart += (msFromNow * 10000);
    return li.QuadPart;
}
