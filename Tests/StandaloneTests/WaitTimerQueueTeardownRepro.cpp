// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Deterministic regression for the STL WaitTimer worker-pop teardown race.
// This target compiles Source/Task/WaitTimer_stl.cpp directly, rather than
// attempting to drive the race through public XTaskQueue APIs. The public API
// cannot reliably stop the worker after it has copied its internal timer
// pointer and released TimerQueue's lock.

#include <httpClient/async.h>
#include "../../Source/Task/WaitTimer.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    class QueueTeardownHooks final : public OS::WaitTimerTestHooks
    {
    public:
        bool BeforeTimerInvoke() noexcept override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_timerPopped = true;
            m_changed.notify_all();
            m_changed.wait(lock, [this]() noexcept { return m_releaseWorker; });
            return false;
        }

        void WaitTimerImplDestructionStarted() noexcept override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_destructionStarted = true;
            m_changed.notify_all();
        }

        bool WaitForTimerPop()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_changed.wait_for(lock, 5s, [this]() noexcept { return m_timerPopped; });
        }

        bool DestructionStartedWithin(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_changed.wait_for(lock, timeout, [this]() noexcept { return m_destructionStarted; });
        }

        bool WaitForDestruction()
        {
            return DestructionStartedWithin(5s);
        }

        void ReleaseWorker()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_releaseWorker = true;
            m_changed.notify_all();
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        bool m_timerPopped = false;
        bool m_destructionStarted = false;
        bool m_releaseWorker = false;
    };
}

int main()
{
    QueueTeardownHooks hooks;
    OS::WaitTimerSetTestHooks(&hooks);

    OS::WaitTimer timer;
    HRESULT hr = timer.Initialize(nullptr, [](void*) {});
    if (FAILED(hr))
    {
        std::printf("[waittimer-queue-teardown] FAILED: Initialize returned 0x%08x\n", static_cast<unsigned int>(hr));
        OS::WaitTimerSetTestHooks(nullptr);
        return 2;
    }

    // Keep the shared TimerQueue alive while the target timer is destroyed.
    // Otherwise the old implementation synchronously joins the paused worker
    // when retiring the final queue owner, obscuring the dispatch race.
    OS::WaitTimer queueOwner;
    hr = queueOwner.Initialize(nullptr, [](void*) {});
    if (FAILED(hr))
    {
        std::printf("[waittimer-queue-teardown] FAILED: keepalive Initialize returned 0x%08x\n", static_cast<unsigned int>(hr));
        timer.Terminate();
        OS::WaitTimerSetTestHooks(nullptr);
        return 2;
    }

    timer.Start(timer.GetDueTime(1));
    if (!hooks.WaitForTimerPop())
    {
        std::printf("[waittimer-queue-teardown] FAILED: timer worker did not pop the due entry\n");
        timer.Terminate();
        queueOwner.Terminate();
        OS::WaitTimerSetTestHooks(nullptr);
        return 2;
    }

    std::atomic<bool> terminateComplete{ false };
    std::thread terminate([&]()
    {
        timer.Terminate();
        terminateComplete.store(true, std::memory_order_release);
    });

    const bool destructionStartedWhileWorkerBlocked = hooks.DestructionStartedWithin(100ms);
    hooks.ReleaseWorker();
    terminate.join();
    const bool destructionStarted = hooks.WaitForDestruction();
    queueOwner.Terminate();
    OS::WaitTimerSetTestHooks(nullptr);

    if (!destructionStarted)
    {
        std::printf("[waittimer-queue-teardown] FAILED: WaitTimer implementation was not destroyed\n");
        return 2;
    }

    if (destructionStartedWhileWorkerBlocked)
    {
        std::printf("[waittimer-queue-teardown] UNSAFE: implementation destruction began after worker copied its raw timer pointer\n");
        return 1;
    }

    if (!terminateComplete.load(std::memory_order_acquire))
    {
        std::printf("[waittimer-queue-teardown] FAILED: Terminate did not complete\n");
        return 2;
    }

    std::printf("[waittimer-queue-teardown] PASSED: dispatch lifetime outlived worker teardown\n");
    return 0;
}