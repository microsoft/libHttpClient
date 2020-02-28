#include "pch.h"
#include "WaitTimer.h"

namespace OS
{
    class WaitTimerImpl
    {
    public:
        WaitTimerImpl()
            : m_timer(nullptr)
        {}

        ~WaitTimerImpl()
        {
            Terminate();
        }

        HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback)
        {
            m_context = context;
            m_callback = callback;

            m_timer = CreateThreadpoolTimer(WaitCallback, this, nullptr);
            RETURN_LAST_ERROR_IF_NULL(m_timer);

            return S_OK;
        }

        void Terminate()
        {
            if (m_timer != nullptr)
            {
                SetThreadpoolTimer(m_timer, nullptr, 0, 0);
                WaitForThreadpoolTimerCallbacks(m_timer, TRUE);
                CloseThreadpoolTimer(m_timer);
                m_timer = nullptr;
            }
        }

        void Start(_In_ uint64_t absoluteTime)
        {
            LARGE_INTEGER li;
            FILETIME ft;
            ASSERT((absoluteTime & 0x8000000000000000) == 0);
            li.QuadPart = static_cast<LONGLONG>(absoluteTime);
            ft.dwHighDateTime = li.HighPart;
            ft.dwLowDateTime = li.LowPart;

            SetThreadpoolTimer(m_timer, &ft, 0, 0);
        }

        void Cancel()
        {
            SetThreadpoolTimer(m_timer, nullptr, 0, 0);
        }

    private:

        PTP_TIMER m_timer;
        void* m_context;
        WaitTimerCallback* m_callback;

        static VOID CALLBACK WaitCallback(
            _Inout_ PTP_CALLBACK_INSTANCE,
            _Inout_opt_ void* context,
            _Inout_ PTP_TIMER)
        {
            if (context != nullptr)
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
        Terminate();
    }

    HRESULT WaitTimer::Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback) noexcept
    {
        if (m_impl.load() != nullptr || callback == nullptr)
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

    void WaitTimer::Terminate() noexcept
    {
        std::unique_ptr<WaitTimerImpl> timer(m_impl.exchange(nullptr));
        if (timer != nullptr)
        {
            timer->Terminate();
        }
    }

    void WaitTimer::Start(_In_ uint64_t absoluteTime) noexcept
    {
        m_impl.load()->Start(absoluteTime);
    }

    void WaitTimer::Cancel() noexcept
    {
        m_impl.load()->Cancel();
    }

    uint64_t WaitTimer::GetAbsoluteTime(_In_ uint32_t msFromNow) noexcept
    {
        FILETIME ft;
        ULARGE_INTEGER li;
        GetSystemTimeAsFileTime(&ft);
        ASSERT((ft.dwHighDateTime & 0x80000000) == 0);

        uint64_t hundredNanosFromNow = msFromNow;
        hundredNanosFromNow *= 10000ULL;

        li.HighPart = ft.dwHighDateTime;
        li.LowPart = ft.dwLowDateTime;
        li.QuadPart += hundredNanosFromNow;

        return li.QuadPart;
    }
} // Namespace