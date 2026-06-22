#include "pch.h"
#include "WaitTimer.h"

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
using TimerDuration = std::chrono::nanoseconds;

namespace
{
    // Keep the public WaitTimer surface on a plain integer so TaskQueue can use
    // atomics without dragging chrono types through its state. The integer still
    // represents steady-clock time, not wall-clock time.
    Deadline DeadlineFromDueTime(uint64_t dueTime) noexcept
    {
        return Deadline(std::chrono::duration_cast<Clock::duration>(TimerDuration(dueTime)));
    }

    uint64_t DueTimeFromDeadline(Deadline deadline) noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<TimerDuration>(deadline.time_since_epoch()).count());
    }
}

namespace OS
{
    class TimerQueue;

    // Keep callback payload and teardown coordination in shared state because the
    // Worker can still hold a due heap entry after the owning WaitTimerImpl has
    // been canceled or destroyed.
    class WaitTimerState
    {
    public:
        WaitTimerState(_In_opt_ void* context, _In_ WaitTimerCallback* callback) noexcept
            : m_context(context), m_callback(callback)
        {}

        uint64_t NextGeneration() noexcept
        {
            return ++m_generation;
        }

        uint64_t Generation() const noexcept
        {
            return m_generation.load(std::memory_order_acquire);
        }

        void BeginTerminate() noexcept
        {
            m_terminating.store(true, std::memory_order_release);
        }

        bool TryBeginDispatch() noexcept
        {
            if (m_terminating.load(std::memory_order_acquire))
            {
                return false;
            }

            std::lock_guard<std::mutex> lock{ m_lock };
            // Re-check under the lock so teardown cannot race with the in-flight
            // count increment and then wait forever for a dispatch we started.
            if (m_terminating.load(std::memory_order_relaxed))
            {
                return false;
            }

            ++m_inFlightDispatch;
            return true;
        }

        void EndDispatch() noexcept
        {
            std::lock_guard<std::mutex> lock{ m_lock };
            ASSERT(m_inFlightDispatch != 0);

            --m_inFlightDispatch;
            if (m_inFlightDispatch == 0)
            {
                m_quiesced.notify_all();
            }
        }

        void WaitForQuiesce() noexcept
        {
            std::unique_lock<std::mutex> lock{ m_lock };
            m_quiesced.wait(lock, [this]() noexcept
            {
                return m_inFlightDispatch == 0;
            });
        }

        void InvokeCallback() noexcept
        {
            m_callback(m_context);
        }

    private:
        void* m_context;
        WaitTimerCallback* m_callback;
        std::atomic<uint64_t> m_generation{ 0 };
        std::atomic<bool> m_terminating{ false };
        DefaultUnnamedMutex m_lock;
        DefaultUnnamedConditionVariable m_quiesced;
        uint32_t m_inFlightDispatch = 0;
    };

    class WaitTimerImpl
    {
    public:
        ~WaitTimerImpl();
        HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback);
        void Start(_In_ uint64_t dueTime);
        void Cancel();
        void Terminate() noexcept;

    private:
        std::shared_ptr<WaitTimerState> m_state;
        std::shared_ptr<TimerQueue> m_timerQueue;
    };

    struct TimerEntry
    {
        Deadline When;
        std::shared_ptr<WaitTimerState> State;
        // Each Start() pushes a new heap entry instead of searching/removing the
        // old one. Generation lets the Worker discard superseded entries cheaply.
        uint64_t Generation;
        TimerEntry(Deadline d, std::shared_ptr<WaitTimerState> state, uint64_t g)
            : When{ d }, State{ std::move(state) }, Generation{ g }
        {}
    };

    struct TimerEntryComparator
    {
        bool operator()(TimerEntry const& l, TimerEntry const& r) noexcept
        {
            return l.When > r.When;
        }
    };

    // The queue is shared across timers, but it should still retire once the
    // last timer goes away instead of leaking for process lifetime.
    class TimerQueue : public std::enable_shared_from_this<TimerQueue>
    {
    public:
        bool Init() noexcept;
        ~TimerQueue();

        void AddTimer() noexcept;
        void RemoveTimer() noexcept;
        void Set(std::shared_ptr<WaitTimerState> const& state, Deadline deadline) noexcept;
        void Remove(WaitTimerState const* state) noexcept;
        std::thread::id WorkerThreadId() const noexcept;

    private:
        void Worker() noexcept;

        TimerEntry const& Peek() const noexcept;
        TimerEntry Pop() noexcept;

        DefaultUnnamedMutex m_mutex;
        DefaultUnnamedConditionVariable m_cv;
        std::vector<TimerEntry> m_queue; // used as a heap
        std::thread m_t;
        std::atomic<uint32_t> m_timerCount{ 0 };
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
            if (m_t.get_id() == std::this_thread::get_id())
            {
                // Immediate-port callbacks can tear down their own timer from the
                // Worker thread. Joining from that path would deadlock.
                m_t.detach();
            }
            else
            {
                m_t.join();
            }
        }
    }

    bool TimerQueue::Init() noexcept
    {
        m_exitThread = false;

        try
        {
            // Capture a self-reference so the queue stays alive until the Worker
            // exits even if the last timer concurrently clears the global slot.
            m_t = std::thread([keepAlive = shared_from_this()]()
            {
                keepAlive->Worker();
            });
            m_initialized = true;
        }
        catch (...)
        {
            m_initialized = false;
        }

        return m_initialized;
    }

    void TimerQueue::AddTimer() noexcept
    {
        m_timerCount.fetch_add(1, std::memory_order_relaxed);
    }

    void TimerQueue::RemoveTimer() noexcept
    {
        if (m_timerCount.fetch_sub(1, std::memory_order_acq_rel) != 1)
        {
            return;
        }

        // Last timer out clears the global queue and wakes the Worker so Linux
        // teardown actually quiesces instead of depending on process exit.
        {
            std::lock_guard<std::mutex> globalLock{ g_timerQueueMutex };
            if (g_timerQueue.get() == this)
            {
                g_timerQueue.reset();
            }
        }

        {
            std::lock_guard<std::mutex> lock{ m_mutex };
            m_exitThread = true;
        }

        m_cv.notify_one();
    }

    void TimerQueue::Set(std::shared_ptr<WaitTimerState> const& state, Deadline deadline) noexcept
    {
        bool shouldNotify;
        {
            std::lock_guard<std::mutex> lock{ m_mutex };

            // Bump the generation so stale heap entries for this timer are
            // skipped by the Worker on pop.  This replaces the old O(N)
            // nullification scan with an O(log N) push.
            uint64_t gen = state->NextGeneration();

            // Only wake the Worker when the new deadline might be earlier
            // than the current heap top; otherwise the Worker is already
            // sleeping for a deadline that is at least as early.
            shouldNotify = m_queue.empty() || deadline < m_queue.front().When;

            m_queue.emplace_back(deadline, state, gen);
            std::push_heap(m_queue.begin(), m_queue.end(), TimerEntryComparator{});
        }
        if (shouldNotify)
        {
            m_cv.notify_one();
        }
    }

    void TimerQueue::Remove(WaitTimerState const* state) noexcept
    {
        std::lock_guard<std::mutex> lock{ m_mutex };

        // Remove is only called during cancellation/teardown.
        // Reset shared state entries so the Worker never dispatches them.
        for (auto& entry : m_queue)
        {
            if (entry.State.get() == state)
            {
                entry.State.reset();
            }
        }
    }

    std::thread::id TimerQueue::WorkerThreadId() const noexcept
    {
        return m_t.get_id();
    }

    void TimerQueue::Worker() noexcept
    {
        std::unique_lock<std::mutex> lock{ m_mutex };
        while (!m_exitThread)
        {
            while (!m_queue.empty())
            {
                auto& top = Peek();

                // Discard stale/nullified entries without releasing the lock.
                if (!top.State ||
                    top.Generation != top.State->Generation())
                {
                    Pop();
                    continue;
                }

                if (Clock::now() < top.When)
                {
                    break;
                }

                TimerEntry entry = Pop();
                if (!entry.State->TryBeginDispatch())
                {
                    continue;
                }

                // Release the lock while invoking the callback, just in case
                // the timer gets destroyed on this thread or re-adds itself
                // in the callback.
                lock.unlock();
                entry.State->InvokeCallback();
                entry.State->EndDispatch();
                lock.lock();
            }

            // Drain dead entries at the heap top so wait_until targets a
            // live deadline rather than sleeping until a stale entry's time.
            while (!m_queue.empty())
            {
                auto& top = Peek();
                if (top.State &&
                    top.Generation == top.State->Generation())
                {
                    break;
                }
                Pop();
            }

            if (!m_queue.empty())
            {
                m_cv.wait_until(lock, Peek().When);
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
        Terminate();
    }

    HRESULT WaitTimerImpl::Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback)
    {
        try
        {
            m_state = http_allocate_shared<WaitTimerState>(context, callback);
        }
        catch (const std::bad_alloc&)
        {
            return E_OUTOFMEMORY;
        }

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
        m_timerQueue->AddTimer();

        return S_OK;
    }

    void WaitTimerImpl::Start(_In_ uint64_t dueTime)
    {
        m_timerQueue->Set(m_state, DeadlineFromDueTime(dueTime));
    }

    void WaitTimerImpl::Cancel()
    {
        if (m_state != nullptr && m_timerQueue != nullptr)
        {
            m_timerQueue->Remove(m_state.get());
        }
    }

    void WaitTimerImpl::Terminate() noexcept
    {
        std::shared_ptr<WaitTimerState> state = std::move(m_state);
        std::shared_ptr<TimerQueue> timerQueue = std::move(m_timerQueue);
        if (state == nullptr || timerQueue == nullptr)
        {
            return;
        }

        // Block any new dispatch before removing queued entries so teardown has
        // a single publish point that both the Worker and waiter observe.
        state->BeginTerminate();
        timerQueue->Remove(state.get());

        if (std::this_thread::get_id() != timerQueue->WorkerThreadId())
        {
            // Delayed callbacks can run on Immediate queues and self-terminate on
            // the Worker thread. Waiting there would deadlock on our own dispatch.
            state->WaitForQuiesce();
        }

        timerQueue->RemoveTimer();
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
            timer->Terminate();
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
        return DueTimeFromDeadline(Clock::now());
    }

    uint64_t WaitTimer::GetDueTime(_In_ uint32_t msFromNow) noexcept
    {
        Deadline deadline = Clock::now() + std::chrono::milliseconds(msFromNow);
        return DueTimeFromDeadline(deadline);
    }
} // Namespace
