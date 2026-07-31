// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Deterministic WaitTimer wrapper lifetime regression. This target compiles
// Source/Task/WaitTimer_stl.cpp directly so it can pause exactly after Start
// or Cancel reads the private m_impl pointer. Public XTaskQueue APIs and their
// hooks cannot expose or reliably schedule this wrapper-only interleaving.

#include <httpClient/async.h>
#include "../../Source/Task/WaitTimer.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    class WrapperHooks final : public OS::WaitTimerTestHooks
    {
    public:
        bool WaitTimerOperationLoaded(bool isStart) noexcept override
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_operationLoaded = true;
            m_isStart = isStart;
            m_changed.notify_all();
            m_changed.wait(lock, [this]() noexcept { return m_releaseOperation; });
            m_operationResumedAfterDestruction = m_destructionStarted;
            m_changed.notify_all();

            // Avoid dereferencing the stale pointer after proving the race.
            return !m_operationResumedAfterDestruction;
        }

        void WaitTimerImplDestructionStarted() noexcept override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_destructionStarted = true;
            m_changed.notify_all();
        }

        bool WaitForOperation(bool isStart)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_changed.wait_for(lock, 5s, [this, isStart]() noexcept
            {
                return m_operationLoaded && m_isStart == isStart;
            });
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

        void ReleaseOperation()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_releaseOperation = true;
            m_changed.notify_all();
        }

        bool OperationResumedAfterDestruction()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_operationResumedAfterDestruction;
        }

    private:
        std::mutex m_mutex;
        std::condition_variable m_changed;
        bool m_operationLoaded = false;
        bool m_isStart = false;
        bool m_releaseOperation = false;
        bool m_destructionStarted = false;
        bool m_operationResumedAfterDestruction = false;
    };

    bool RunCase(bool isStart)
    {
        WrapperHooks hooks;
        OS::WaitTimerSetTestHooks(&hooks);

        OS::WaitTimer timer;
        HRESULT hr = timer.Initialize(nullptr, [](void*) {});
        if (FAILED(hr))
        {
            std::printf("[waittimer-wrapper-terminate] FAILED: Initialize returned 0x%08x\n", static_cast<unsigned int>(hr));
            OS::WaitTimerSetTestHooks(nullptr);
            return false;
        }

        std::thread operation([&timer, isStart]()
        {
            if (isStart)
            {
                timer.Start(timer.GetDueTime(60000));
            }
            else
            {
                timer.Cancel();
            }
        });

        if (!hooks.WaitForOperation(isStart))
        {
            std::printf("[waittimer-wrapper-terminate] FAILED: %s did not observe m_impl\n", isStart ? "Start" : "Cancel");
            hooks.ReleaseOperation();
            operation.join();
            timer.Terminate();
            OS::WaitTimerSetTestHooks(nullptr);
            return false;
        }

        std::thread terminate([&timer]() { timer.Terminate(); });
        const bool destructionStartedWhileOperationBlocked = hooks.DestructionStartedWithin(100ms);
        hooks.ReleaseOperation();
        operation.join();
        terminate.join();
        const bool unsafe = hooks.OperationResumedAfterDestruction();
        const bool destructionStarted = hooks.WaitForDestruction();
        OS::WaitTimerSetTestHooks(nullptr);

        if (destructionStartedWhileOperationBlocked || unsafe)
        {
            std::printf("[waittimer-wrapper-terminate] UNSAFE: implementation destruction began while %s held its raw implementation reference\n", isStart ? "Start" : "Cancel");
            return false;
        }

        if (!destructionStarted)
        {
            std::printf("[waittimer-wrapper-terminate] FAILED: implementation destruction did not run after %s completed\n", isStart ? "Start" : "Cancel");
            return false;
        }

        return true;
    }
}

int main()
{
    const bool startPassed = RunCase(true);
    const bool cancelPassed = RunCase(false);
    if (!startPassed || !cancelPassed)
    {
        return 1;
    }

    std::printf("[waittimer-wrapper-terminate] PASSED: Start and Cancel cannot race implementation teardown\n");
    return 0;
}