// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Deterministic regression for last-owner retirement racing a new timer. This
// target compiles Source/Task/WaitTimer_stl.cpp directly so it can pause inside
// TimerQueue::RemoveTimer, exactly while the retiring owner holds
// g_timerQueueMutex, and prove a concurrent Initialize cannot resurrect a queue
// that is being retired.

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

    class RetirementHooks final : public OS::WaitTimerTestHooks
    {
    public:
        void BeforeTimerQueueRetirement() noexcept override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_retirementStarted = true;
            m_changed.notify_all();
            m_changed.wait(lock, [this]() noexcept { return m_releaseRetirement; });
        }

        bool WaitForRetirement()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_changed.wait_for(lock, 5s, [this]() noexcept { return m_retirementStarted; });
        }

        void ReleaseRetirement()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_releaseRetirement = true;
            m_changed.notify_all();
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        bool m_retirementStarted = false;
        bool m_releaseRetirement = false;
    };

    struct CallbackState
    {
        std::mutex Mutex;
        std::condition_variable Changed;
        bool Ran = false;
    };

    void CALLBACK TimerCallback(void* context)
    {
        auto callbackState = static_cast<CallbackState*>(context);
        {
            std::lock_guard<std::mutex> lock(callbackState->Mutex);
            callbackState->Ran = true;
        }
        callbackState->Changed.notify_all();
    }
}

int main()
{
    RetirementHooks hooks;
    OS::WaitTimerSetTestHooks(&hooks);

    OS::WaitTimer retiringTimer;
    HRESULT hr = retiringTimer.Initialize(nullptr, [](void*) {});
    if (FAILED(hr))
    {
        std::printf("[waittimer-queue-retirement] FAILED: first Initialize returned 0x%08x\n", static_cast<unsigned int>(hr));
        OS::WaitTimerSetTestHooks(nullptr);
        return 2;
    }

    std::thread retire([&retiringTimer]() { retiringTimer.Terminate(); });
    if (!hooks.WaitForRetirement())
    {
        std::printf("[waittimer-queue-retirement] FAILED: timer queue did not begin last-owner retirement\n");
        hooks.ReleaseRetirement();
        retire.join();
        OS::WaitTimerSetTestHooks(nullptr);
        return 2;
    }

    CallbackState callbackState;
    OS::WaitTimer newTimer;
    std::atomic<bool> initialized{ false };
    std::thread initialize([&newTimer, &callbackState, &initialized]()
    {
        initialized.store(SUCCEEDED(newTimer.Initialize(&callbackState, TimerCallback)), std::memory_order_release);
    });

    hooks.ReleaseRetirement();
    retire.join();
    initialize.join();
    OS::WaitTimerSetTestHooks(nullptr);

    if (!initialized.load(std::memory_order_acquire))
    {
        std::printf("[waittimer-queue-retirement] FAILED: concurrent Initialize did not succeed\n");
        return 2;
    }

    newTimer.Start(newTimer.GetDueTime(1));
    std::unique_lock<std::mutex> lock(callbackState.Mutex);
    if (!callbackState.Changed.wait_for(lock, 5s, [&callbackState]() noexcept { return callbackState.Ran; }))
    {
        std::printf("[waittimer-queue-retirement] FAILED: newly initialized timer did not dispatch\n");
        return 1;
    }

    newTimer.Terminate();
    std::printf("[waittimer-queue-retirement] PASSED: concurrent initialization received a live timer queue\n");
    return 0;
}
