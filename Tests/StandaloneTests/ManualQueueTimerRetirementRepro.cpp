// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Public-XTaskQueue integration regression for last-owner WaitTimer retirement.
// A private Manual/Manual queue is destroyed while its replacement is created,
// matching components that recreate a dedicated queue during lifecycle resets.
// The replacement queue must receive a live timer worker and dispatch delayed work.

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
            std::unique_lock<std::mutex> lock{ m_mutex };
            m_retirementStarted = true;
            m_changed.notify_all();
            m_changed.wait(lock, [this]() noexcept { return m_releaseRetirement; });
        }

        bool WaitForRetirement()
        {
            std::unique_lock<std::mutex> lock{ m_mutex };
            return m_changed.wait_for(lock, 5s, [this]() noexcept { return m_retirementStarted; });
        }

        void ReleaseRetirement()
        {
            std::lock_guard<std::mutex> lock{ m_mutex };
            m_releaseRetirement = true;
            m_changed.notify_all();
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        bool m_retirementStarted = false;
        bool m_releaseRetirement = false;
    };

    void CALLBACK DelayedCallback(void* context, bool canceled)
    {
        if (!canceled)
        {
            static_cast<std::atomic<bool>*>(context)->store(true, std::memory_order_release);
        }
    }

    void DrainAndClose(XTaskQueueHandle queue)
    {
        XTaskQueueTerminate(queue, false, nullptr, nullptr);
        while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) {}
        while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0)) {}
        XTaskQueueCloseHandle(queue);
    }
}

int main()
{
    RetirementHooks hooks;
    OS::WaitTimerSetTestHooks(&hooks);

    XTaskQueueHandle oldQueue{ nullptr };
    HRESULT hr = XTaskQueueCreate(
        XTaskQueueDispatchMode::Manual,
        XTaskQueueDispatchMode::Manual,
        &oldQueue);
    if (FAILED(hr))
    {
        std::printf("[manual-queue-timer-retirement] FAILED: old queue creation returned 0x%08x\n", static_cast<unsigned int>(hr));
        OS::WaitTimerSetTestHooks(nullptr);
        return 2;
    }

    std::thread closeOldQueue([oldQueue]() { XTaskQueueCloseHandle(oldQueue); });
    if (!hooks.WaitForRetirement())
    {
        std::printf("[manual-queue-timer-retirement] FAILED: closing the old queue did not begin timer retirement\n");
        hooks.ReleaseRetirement();
        closeOldQueue.join();
        OS::WaitTimerSetTestHooks(nullptr);
        return 2;
    }

    XTaskQueueHandle replacementQueue{ nullptr };
    std::atomic<bool> creationCompleted{ false };
    HRESULT creationHr = E_FAIL;
    std::thread createReplacement([&]()
    {
        creationHr = XTaskQueueCreate(
            XTaskQueueDispatchMode::Manual,
            XTaskQueueDispatchMode::Manual,
            &replacementQueue);
        creationCompleted.store(true, std::memory_order_release);
    });

    // Retirement owns the process-global timer-queue lock. Replacement queue
    // initialization must not complete by adopting that retiring queue.
    std::this_thread::sleep_for(100ms);
    const bool creationEscapedRetirement = creationCompleted.load(std::memory_order_acquire);

    hooks.ReleaseRetirement();
    closeOldQueue.join();
    createReplacement.join();
    OS::WaitTimerSetTestHooks(nullptr);

    if (creationEscapedRetirement)
    {
        std::printf("[manual-queue-timer-retirement] FAILED: replacement queue adopted a timer queue while it was retiring\n");
        if (replacementQueue != nullptr)
        {
            DrainAndClose(replacementQueue);
        }
        return 1;
    }

    if (FAILED(creationHr))
    {
        std::printf("[manual-queue-timer-retirement] FAILED: replacement queue creation returned 0x%08x\n", static_cast<unsigned int>(creationHr));
        return 2;
    }

    std::atomic<bool> callbackRan{ false };
    hr = XTaskQueueSubmitDelayedCallback(
        replacementQueue,
        XTaskQueuePort::Work,
        1,
        &callbackRan,
        DelayedCallback);
    if (FAILED(hr))
    {
        std::printf("[manual-queue-timer-retirement] FAILED: delayed callback submission returned 0x%08x\n", static_cast<unsigned int>(hr));
        DrainAndClose(replacementQueue);
        return 2;
    }

    const bool dispatched = XTaskQueueDispatch(replacementQueue, XTaskQueuePort::Work, 5000);
    const bool callbackObserved = callbackRan.load(std::memory_order_acquire);
    DrainAndClose(replacementQueue);

    if (!dispatched || !callbackObserved)
    {
        std::printf("[manual-queue-timer-retirement] FAILED: replacement queue delayed callback did not dispatch\n");
        return 1;
    }

    std::printf("[manual-queue-timer-retirement] PASSED: replacement manual queue received a live timer worker\n");
    return 0;
}