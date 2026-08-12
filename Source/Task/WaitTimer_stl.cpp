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

    class WaitTimerState
    {
    public:
        WaitTimerState(_In_opt_ void* context, _In_ WaitTimerCallback* callback) noexcept
            : m_context(context), m_callback(callback)
        {}

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

            std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
            if (m_terminating.load(std::memory_order_relaxed))
            {
                return false;
            }

            ++m_inFlightDispatch;
            return true;
        }

        void EndDispatch() noexcept
        {
            std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
            ASSERT(m_inFlightDispatch != 0);
            if (--m_inFlightDispatch == 0)
            {
                m_quiesced.notify_all();
            }
        }

        void WaitForQuiesce() noexcept
        {
            std::unique_lock<DefaultUnnamedMutex> lock{ m_mutex };
            m_quiesced.wait(lock, [this]() noexcept { return m_inFlightDispatch == 0; });
        }

        void InvokeCallback() noexcept
        {
            m_callback(m_context);
        }

    private:
        void* m_context;
        WaitTimerCallback* m_callback;
        std::atomic<bool> m_terminating{ false };
        DefaultUnnamedMutex m_mutex;
        DefaultUnnamedConditionVariableAny m_quiesced;
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
        WaitTimerImpl* Timer;
        std::shared_ptr<WaitTimerState> State;
        TimerEntry(Deadline d, WaitTimerImpl* timer, std::shared_ptr<WaitTimerState> state)
            : When{ d }, Timer{ timer }, State{ std::move(state) } {}
    };

    struct TimerEntryComparator
    {
        bool operator()(TimerEntry const& l, TimerEntry const& r) noexcept
        {
            return l.When > r.When;
        }
    };

    // A single process-wide TimerQueue owns the worker thread that fires every
    // WaitTimer callback. Ownership is shared: each live WaitTimerImpl holds a
    // shared_ptr, and the running worker holds one too (captured in Init), so
    // the queue is never destroyed while a callback is in flight. m_timerCount
    // tracks live timers under g_timerQueueMutex so the last owner can retire
    // the queue and stop the worker.
    class TimerQueue : public std::enable_shared_from_this<TimerQueue>
    {
    public:
        bool Init() noexcept;
        ~TimerQueue();

        void AddTimer() noexcept;
        void RemoveTimer() noexcept;
        void Set(WaitTimerImpl* timer, std::shared_ptr<WaitTimerState> const& state, Deadline deadline) noexcept;
        void Remove(WaitTimerImpl const* timer) noexcept;
        std::thread::id WorkerThreadId() const noexcept;

    private:
        void Worker() noexcept;

        TimerEntry const& Peek() const noexcept;
        TimerEntry Pop() noexcept;

        DefaultUnnamedMutex m_mutex;
        DefaultUnnamedConditionVariableAny m_cv;
        std::vector<TimerEntry> m_queue; // used as a heap
        std::thread m_t;
        uint32_t m_timerCount = 0; // live timers; guarded by g_timerQueueMutex
        bool m_exitThread = false;
        bool m_initialized = false;
    };

    namespace
    {
        std::shared_ptr<TimerQueue> g_timerQueue;
        DefaultUnnamedMutex g_timerQueueMutex;
        DefaultUnnamedMutex g_testHooksMutex;
        DefaultUnnamedConditionVariableAny g_testHooksChanged;
        WaitTimerTestHooks* g_testHooks = nullptr;
        std::atomic<bool> g_testHooksInstalled{ false };
        uint32_t g_testHooksActive = 0;
        bool g_testHooksReplacing = false;
    }

    class WaitTimerTestHookLease
    {
    public:
        WaitTimerTestHookLease() noexcept
        {
            // Production fast path: when no hooks are installed, avoid the
            // process-global hook mutex entirely on Start/Cancel/dispatch.
            if (!g_testHooksInstalled.load(std::memory_order_acquire))
            {
                return;
            }

            std::unique_lock<DefaultUnnamedMutex> lock{ g_testHooksMutex };
            g_testHooksChanged.wait(lock, []() noexcept { return !g_testHooksReplacing; });
            m_hooks = g_testHooks;
            if (m_hooks != nullptr)
            {
                ++g_testHooksActive;
            }
        }

        ~WaitTimerTestHookLease()
        {
            if (m_hooks != nullptr)
            {
                std::lock_guard<DefaultUnnamedMutex> lock{ g_testHooksMutex };
                ASSERT(g_testHooksActive != 0);
                if (--g_testHooksActive == 0)
                {
                    g_testHooksChanged.notify_all();
                }
            }
        }

        WaitTimerTestHooks* Get() const noexcept
        {
            return m_hooks;
        }

    private:
        WaitTimerTestHooks* m_hooks = nullptr;
    };

    void WaitTimerSetTestHooks(_In_opt_ WaitTimerTestHooks* hooks) noexcept
    {
        std::unique_lock<DefaultUnnamedMutex> lock{ g_testHooksMutex };
        g_testHooksReplacing = true;
        g_testHooksChanged.wait(lock, []() noexcept { return g_testHooksActive == 0; });
        g_testHooks = hooks;
        g_testHooksInstalled.store(hooks != nullptr, std::memory_order_release);
        g_testHooksReplacing = false;
        lock.unlock();
        g_testHooksChanged.notify_all();
    }

    TimerQueue::~TimerQueue()
    {
        {
            std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
            m_exitThread = true;
        }

        m_cv.notify_all();
        if (m_t.joinable())
        {
            // A timer callback can drop the last queue reference, so the queue
            // may be destroyed on its own worker thread. Detach in that case;
            // joining our own thread would deadlock.
            if (m_t.get_id() == std::this_thread::get_id())
            {
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
            // Capture a shared_ptr so the worker keeps the queue alive for its
            // whole lifetime. This lets a callback tear down its own timer (and
            // the last queue reference) without the worker touching freed state.
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
        // Caller holds g_timerQueueMutex, which serializes timer adoption in
        // Initialize with last-owner retirement in RemoveTimer.
        ++m_timerCount;
    }

    void TimerQueue::RemoveTimer() noexcept
    {
        {
            // The decrement and the last-owner decision run under the same lock
            // Initialize takes to adopt a timer, so a queue that is retiring can
            // never be resurrected by a concurrent Initialize.
            std::lock_guard<DefaultUnnamedMutex> globalLock{ g_timerQueueMutex };
            ASSERT(m_timerCount != 0);
            if (--m_timerCount != 0)
            {
                return;
            }

            WaitTimerTestHookLease testHooks;
            if (auto hooks = testHooks.Get())
            {
                hooks->BeforeTimerQueueRetirement();
            }

            if (g_timerQueue.get() == this)
            {
                g_timerQueue.reset();
            }
        }

        {
            std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
            m_exitThread = true;
        }
        m_cv.notify_all();
    }

    std::thread::id TimerQueue::WorkerThreadId() const noexcept
    {
        return m_t.get_id();
    }

    void TimerQueue::Set(WaitTimerImpl* timer, std::shared_ptr<WaitTimerState> const& state, Deadline deadline) noexcept
    {
        {
            std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };

            for (auto& entry : m_queue)
            {
                if (entry.Timer == timer)
                {
                    entry.Timer = nullptr;
                    entry.State.reset();
                }
            }

            m_queue.emplace_back(deadline, timer, state);
            std::push_heap(m_queue.begin(), m_queue.end(), TimerEntryComparator{});
        }
        m_cv.notify_all();
    }

    void TimerQueue::Remove(WaitTimerImpl const* timer) noexcept
    {
        std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };

        // since m_queue is a heap, removing elements is non trivial, instead we
        // just clean the timer pointer and the entry will be popped eventually

        for (auto& entry : m_queue)
        {
            if (entry.Timer == timer)
            {
                entry.Timer = nullptr;
                entry.State.reset();
            }
        }
    }

    void TimerQueue::Worker() noexcept
    {
        std::unique_lock<DefaultUnnamedMutex> lock{ m_mutex };
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

                // The raw timer pointer is only a cancellation key. Dispatch owns
                // the shared state so teardown cannot invalidate the callback.
                lock.unlock();
                if (entry.State && entry.State->TryBeginDispatch())
                {
                    WaitTimerTestHookLease testHooks;
                    if (auto hooks = testHooks.Get(); hooks == nullptr || hooks->BeforeTimerInvoke())
                    {
                        entry.State->InvokeCallback();
                    }
                    entry.State->EndDispatch();
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
        Terminate();
    }

    void WaitTimerImpl::Terminate() noexcept
    {
        // Take ownership of the shared state and queue. Terminate is idempotent:
        // the destructor always runs it, and a second call is a no-op once the
        // members have been moved out.
        std::shared_ptr<WaitTimerState> state = std::move(m_state);
        std::shared_ptr<TimerQueue> timerQueue = std::move(m_timerQueue);

        // Initialize may have failed before wiring up state/queue; there is
        // nothing to tear down in that case.
        if (state == nullptr || timerQueue == nullptr)
        {
            return;
        }

        // Stop new dispatches, drop any queued entry, then wait for an in-flight
        // dispatch to finish so the callback can never run against freed state.
        // Skip the wait on the worker thread (a callback is tearing down its own
        // timer): blocking on our own dispatch would deadlock, and the entry's
        // shared-state lease keeps the state alive until the callback returns.
        state->BeginTerminate();
        timerQueue->Remove(this);
        if (std::this_thread::get_id() != timerQueue->WorkerThreadId())
        {
            state->WaitForQuiesce();
        }

        WaitTimerTestHookLease testHooks;
        if (auto hooks = testHooks.Get())
        {
            hooks->WaitTimerImplDestructionStarted();
        }

        // Release this timer's queue ownership; the last owner retires the queue.
        timerQueue->RemoveTimer();
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

        std::lock_guard<DefaultUnnamedMutex> lock{ g_timerQueueMutex };

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
        m_timerQueue->Set(this, m_state, Deadline(Deadline::duration(dueTime)));
    }

    void WaitTimerImpl::Cancel()
    {
        m_timerQueue->Remove(this);
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
        std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
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
        std::unique_ptr<WaitTimerImpl> timer;
        {
            std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
            timer.reset(m_impl.exchange(nullptr));
        }
        if (timer != nullptr)
        {
            timer->Cancel();
        }
    }

    void WaitTimer::Start(_In_ uint64_t dueTime) noexcept
    {
        std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
        WaitTimerImpl* timer = m_impl.load();
        WaitTimerTestHookLease testHooks;
        if (auto hooks = testHooks.Get(); hooks != nullptr && !hooks->WaitTimerOperationLoaded(true))
        {
            return;
        }
        if (timer != nullptr)
        {
            timer->Start(dueTime);
        }
    }

    void WaitTimer::Cancel() noexcept
    {
        std::lock_guard<DefaultUnnamedMutex> lock{ m_mutex };
        WaitTimerImpl* timer = m_impl.load();
        WaitTimerTestHookLease testHooks;
        if (auto hooks = testHooks.Get(); hooks != nullptr && !hooks->WaitTimerOperationLoaded(false))
        {
            return;
        }
        if (timer != nullptr)
        {
            timer->Cancel();
        }
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
