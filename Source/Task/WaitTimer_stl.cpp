#include "pch.h"
#include "WaitTimer.h"

using Deadline = std::chrono::high_resolution_clock::time_point;

class WaitTimerImpl
{
public:

    ~WaitTimerImpl()
    {
        if (m_thread.joinable())
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_abortThread = true;
            m_cv.notify_all();
        }

        m_thread.join();
    }

    HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback)
    {
        m_context = context;
        m_callback = callback;

        try
        {
            std::thread t([this] { Worker(); });
            m_thread.swap(t);
        }
        catch (...)
        {
            return E_FAIL;
        }

        return S_OK;
    }

    void Start(_In_ uint64_t absoluteTime)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_duration = Deadline::duration(absoluteTime);
        m_deadlineSet = true;
        m_cv.notify_all();
    }

    void Cancel()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_deadlineSet = false;
        m_cv.notify_all();
    }

private:

    void* m_context;
    WaitTimerCallback* m_callback;
    Deadline::duration m_duration;
    bool m_deadlineSet = false;
    bool m_abortThread = false;
    std::condition_variable m_cv;
    std::mutex m_mutex;
    std::thread m_thread;

    void Worker() noexcept
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        while (!m_abortThread)
        {
            if (m_deadlineSet)
            {
                Deadline deadline(m_duration);
                m_cv.wait_until(lock, deadline);
            }
            else
            {
                m_cv.wait(lock);
            }

            if (std::chrono::high_resolution_clock::now() >= Deadline(m_duration))
            {
                m_deadlineSet = false;
                lock.unlock();
                m_callback(m_context);
                lock.lock();
            }
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
        ASSERT(false);
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
    Deadline d = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(msFromNow);
    return d.time_since_epoch().count();
}
