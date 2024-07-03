#include "pch.h"
#include "WaitTimer.h"

using Deadline = std::chrono::high_resolution_clock::time_point;

namespace OS
{
    class TimerQueue;

    class WaitTimerImpl
    {
    public:
        ~WaitTimerImpl();
        HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback);
        void Start(_In_ uint64_t absoluteTime);
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

        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::vector<TimerEntry> m_queue; // used as a heap
        std::thread m_t;
        bool m_exitThread = false;
        bool m_initialized = false;
    };

    namespace
    {
        std::shared_ptr<TimerQueue> g_timerQueue;
        std::mutex g_timerQueueMutex;
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
                if (std::chrono::high_resolution_clock::now() < next)
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

    void WaitTimerImpl::Start(_In_ uint64_t absoluteTime)
    {
        m_timerQueue->Set(this, Deadline(Deadline::duration(absoluteTime)));
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
        Deadline d = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(msFromNow);
        return d.time_since_epoch().count();
    }
} // Namespace