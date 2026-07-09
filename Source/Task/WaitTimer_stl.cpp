#include "pch.h"
#include "WaitTimer.h"

// NOTE: This is the proven pre-#975 STL wait-timer backend (libHttpClient
// 2.3.1 / commit 0fa5f24), restored to fix a delayed-callback strand that
// #975's rewrite introduced. The #975 backend added a per-timer "generation"
// scheme where every Start() invalidated the previously pushed heap entry; a
// Start() racing the worker as it was about to dispatch the due entry caused
// the worker to discard that entry as stale and never fire it, permanently
// stranding the callback (observed on-device as an infinite sign-in / inventory
// hang on a single-port manual queue with no independent traffic to rescue it).
//
// This backend uses the original pointer-keyed cancellation and an
// unconditional notify on every Set, which cannot drop a due callback. The
// public WaitTimer API matches the post-#975 surface (GetCurrentTime /
// GetDueTime / Start(dueTime)); both time helpers and the worker share a
// single monotonic clock so the values TaskQueue compares stay consistent and
// are immune to wall-clock adjustments.
//
// The pre-#975 backend keyed this clock off std::high_resolution_clock. That
// alias is steady_clock on some standard libraries (libc++) but system_clock
// on others (libstdc++), which is wall-clock and not monotonic. We pin it to
// steady_clock explicitly so the ordering and "now < dueTime" comparisons in
// TaskQueue are monotonic on every platform that compiles this backend.

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

namespace OS
{
    class TimerQueue;

    class WaitTimerImpl
    {
    public:
        ~WaitTimerImpl();
        HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback);
        void Start(_In_ uint64_t dueTime);
        void Cancel();
        void InvokeCallback();

    private:

        void* m_context;
        WaitTimerCallback* m_callback;
        std::shared_ptr<TimerQueue> m_timerQueue;
    };

    struct TimerEntry
    {
        Deadline When;
        WaitTimerImpl* Timer;
        TimerEntry(Deadline d, WaitTimerImpl* t) : When{ d }, Timer{ t } {}
    };

    struct TimerEntryComparator
    {
        bool operator()(TimerEntry const& l, TimerEntry const& r) noexcept
        {
            return l.When > r.When;
        }
    };

    class TimerQueue
    {
    public:
        bool Init() noexcept;
        ~TimerQueue();

        void Set(WaitTimerImpl* timer, Deadline deadline) noexcept;
        void Remove(WaitTimerImpl const* timer) noexcept;

    private:
        void Worker() noexcept;

        TimerEntry const& Peek() const noexcept;
        TimerEntry Pop() noexcept;

        DefaultUnnamedMutex m_mutex;
        DefaultUnnamedConditionVariable m_cv;
        std::vector<TimerEntry> m_queue; // used as a heap
        std::thread m_t;
        bool m_exitThread = false;
        bool m_initialized = false;
    };

    namespace
    {
        std::shared_ptr<TimerQueue> g_timerQueue;
        DefaultUnnamedMutex g_timerQueueMutex;
    }

    TimerQueue::~TimerQueue()
    {
        {
            std::lock_guard<std::mutex> lock{ m_mutex };
            m_exitThread = true;
        }

        m_cv.notify_all();
        if (m_t.joinable())
        {
            m_t.join();
        }
    }

    bool TimerQueue::Init() noexcept
    {
        m_exitThread = false;

        try
        {
            m_t = std::thread([this]()
            {
                Worker();
            });
            m_initialized = true;
        }
        catch (...)
        {
            m_initialized = false;
        }

        return m_initialized;
    }

    void TimerQueue::Set(WaitTimerImpl* timer, Deadline deadline) noexcept
    {
        {
            std::lock_guard<std::mutex> lock{ m_mutex };

            for (auto& entry : m_queue)
            {
                if (entry.Timer == timer)
                {
                    entry.Timer = nullptr;
                }
            }

            m_queue.emplace_back(deadline, timer);
            std::push_heap(m_queue.begin(), m_queue.end(), TimerEntryComparator{});
        }
        m_cv.notify_all();
    }

    void TimerQueue::Remove(WaitTimerImpl const* timer) noexcept
    {
        std::lock_guard<std::mutex> lock{ m_mutex };

        // since m_queue is a heap, removing elements is non trivial, instead we
        // just clean the timer pointer and the entry will be popped eventually

        for (auto& entry : m_queue)
        {
            if (entry.Timer == timer)
            {
                entry.Timer = nullptr;
            }
        }
    }

    void TimerQueue::Worker() noexcept
    {
        std::unique_lock<std::mutex> lock{ m_mutex };
        while (!m_exitThread)
        {
            while (!m_queue.empty())
            {
                Deadline next = Peek().When;
                if (Clock::now() < next)
                {
                    break;
                }

                TimerEntry entry = Pop();

                // release the lock while invoking the callback, just in case timer
                // gets destroyed on this thread or re-adds itself in the callback
                lock.unlock();
                if (entry.Timer) // Timer is set to nullptr if the entry is removed
                {
                    entry.Timer->InvokeCallback();
                }
                lock.lock();
            }

            if (!m_queue.empty())
            {
                Deadline next = Peek().When;
                m_cv.wait_until(lock, next);
            }
            else
            {
                m_cv.wait(lock);
            }
        }
    }

    TimerEntry const& TimerQueue::Peek() const noexcept
    {
        // assume lock is held
        return m_queue.front();
    }

    TimerEntry TimerQueue::Pop() noexcept
    {
        // assume lock is held
        TimerEntry e = m_queue.front();
        std::pop_heap(m_queue.begin(), m_queue.end(), TimerEntryComparator{});
        m_queue.pop_back();
        return e;
    }

    WaitTimerImpl::~WaitTimerImpl()
    {
        std::lock_guard<std::mutex> lock{ g_timerQueueMutex };

        // If we are the last one referencing the global timer the
        // shared use count will be two (us + the global). If it is,
        // clear out the global. We let our own reference reset
        // as the class destructs. This puts it outside the mutex
        // lock, which we want since there is some shutdown cost
        // associated with shutting the timer down.

        if (g_timerQueue.use_count() == 2)
        {
            g_timerQueue.reset();
        }
    }

    HRESULT WaitTimerImpl::Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback)
    {
        m_context = context;
        m_callback = callback;

        std::lock_guard<std::mutex> lock{ g_timerQueueMutex };

        if (g_timerQueue == nullptr)
        {
            try
            {
                auto queue = http_allocate_shared<TimerQueue>();
                if (!queue->Init())
                {
                    return E_FAIL;
                }

                g_timerQueue = std::move(queue);
            }
            catch (const std::bad_alloc&)
            {
                return E_OUTOFMEMORY;
            }
        }

        m_timerQueue = g_timerQueue;

        return S_OK;
    }

    void WaitTimerImpl::Start(_In_ uint64_t dueTime)
    {
        m_timerQueue->Set(this, Deadline(Deadline::duration(dueTime)));
    }

    void WaitTimerImpl::Cancel()
    {
        m_timerQueue->Remove(this);
    }

    void WaitTimerImpl::InvokeCallback()
    {
        m_callback(m_context);
    }

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
            ASSERT(false);
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
            timer->Cancel();
        }
    }

    void WaitTimer::Start(_In_ uint64_t dueTime) noexcept
    {
        m_impl.load()->Start(dueTime);
    }

    void WaitTimer::Cancel() noexcept
    {
        m_impl.load()->Cancel();
    }

    uint64_t WaitTimer::GetCurrentTime() noexcept
    {
        Deadline now = Clock::now();
        return now.time_since_epoch().count();
    }

    uint64_t WaitTimer::GetDueTime(_In_ uint32_t msFromNow) noexcept
    {
        Deadline d = Clock::now() + std::chrono::milliseconds(msFromNow);
        return d.time_since_epoch().count();
    }
} // Namespace
