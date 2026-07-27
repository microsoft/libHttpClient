// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Shared, platform-neutral scenario for the composite-queue delayed-callback
// starvation regression. Exercised by:
//   - the Windows unit test VerifyCompositeQueueDelayedCallbackStarvation
//     (TaskQueueTests.cpp, Win32 WaitTimer backend), and
//   - the standalone Linux repro binary (CMake target, STL WaitTimer backend).
//
// The STL timer backend (Linux/non-Microsoft platforms) is where the regression
// manifests, so running this on Linux CI guards the platform the Windows lanes
// cannot reach.
//
// Scenario: many concurrent submitter threads each create a composite queue over
// one shared Work port and drive a self-resubmitting delayed-poll loop with tiny
// (1-2ms) delays. A canceller thread terminates a subset of the composites while
// they still have a delayed callback pending, while pump threads dispatch the
// shared Work port. This races new delayed submits against the timer worker
// promoting/arming due entries and against terminate removing the armed-earliest
// entry. If the shared timer fails to re-arm in any of those windows, the
// remaining delayed callbacks stop firing and the queue starves: some requests
// never complete and RunCompositeQueueStarvationScenario reports failure.
//
// Uses only the public XTaskQueue C API plus the C++ standard library so the
// same translation unit compiles on every platform.

#pragma once

// async.h pulls in pal.h (HRESULT, STDAPI, CALLBACK, FAILED, ...) and then
// XTaskQueue.h, so this header is self-contained on every platform.
#include <httpClient/async.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace hc_test
{

struct CompositeStarvationConfig
{
    int outerIterations = 20;
    int submitterThreads = 6;
    int requestsPerSubmitter = 24;
    int polls = 20;
    int pumpThreads = 2;
    // Per-iteration bound; starvation is detected when not every request
    // completes within this window.
    int completionTimeoutMs = 15000;
};

namespace detail
{

struct PollRequest
{
    XTaskQueueHandle composite{ nullptr };
    int target{ 0 };
    uint32_t delayMs{ 0 };
    std::atomic<int> polls{ 0 };
    std::atomic<int>* completed{ nullptr };
    std::atomic<int> terminated{ 0 };
    std::atomic<int> counted{ 0 };

    void MarkDone()
    {
        if (counted.exchange(1) == 0)
        {
            completed->fetch_add(1);
        }
    }

    void TerminateOnce()
    {
        if (terminated.exchange(1) == 0)
        {
            // Terminate only; the handle is closed in the final cleanup pass
            // after every worker/canceller/pump thread has joined.
            XTaskQueueTerminate(composite, false, nullptr, nullptr);
        }
    }
};

inline void CALLBACK PollCallback(void* context, bool cancelled)
{
    PollRequest* r = static_cast<PollRequest*>(context);

    if (cancelled)
    {
        // Dispatched after the composite was terminated: account for it once.
        r->MarkDone();
        return;
    }

    int n = r->polls.fetch_add(1) + 1;
    if (n < r->target)
    {
        HRESULT hr = XTaskQueueSubmitDelayedCallback(r->composite, XTaskQueuePort::Work, r->delayMs, r, PollCallback);
        if (FAILED(hr))
        {
            // Composite is terminating (E_ABORT); account for it once.
            r->MarkDone();
        }
    }
    else
    {
        r->TerminateOnce();
        r->MarkDone();
    }
}

} // namespace detail

// Runs the scenario. Returns true if every request completed in every iteration
// (healthy timer arming/teardown). Returns false if any iteration starved.
// If failedIteration / completedAtFailure are provided, they receive the first
// failing iteration index and how many of the per-iteration requests completed.
inline bool RunCompositeQueueStarvationScenario(
    const CompositeStarvationConfig& cfg = CompositeStarvationConfig{},
    int* failedIteration = nullptr,
    int* completedAtFailure = nullptr)
{
    using detail::PollRequest;
    using detail::PollCallback;

    const int total = cfg.submitterThreads * cfg.requestsPerSubmitter;

    for (int iter = 0; iter < cfg.outerIterations; iter++)
    {
        XTaskQueueHandle queue{ nullptr };
        if (FAILED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue)))
        {
            if (failedIteration) { *failedIteration = iter; }
            if (completedAtFailure) { *completedAtFailure = -1; }
            return false;
        }

        XTaskQueuePortHandle workPort{ nullptr };
        XTaskQueueGetPort(queue, XTaskQueuePort::Work, &workPort);

        // Pump threads for the shared Work port.
        std::atomic<bool> pumping{ true };
        std::vector<std::thread> pumps;
        pumps.reserve(cfg.pumpThreads);
        for (int p = 0; p < cfg.pumpThreads; p++)
        {
            pumps.emplace_back([queue, &pumping]()
            {
                while (pumping.load(std::memory_order_acquire))
                {
                    XTaskQueueDispatch(queue, XTaskQueuePort::Work, 50);
                }
            });
        }

        std::atomic<int> completed{ 0 };

        // Published request pointers. Composite handles are closed only in the
        // final cleanup loop after every thread has joined, so no thread can
        // touch a freed handle.
        std::vector<std::atomic<PollRequest*>> slots(total);
        for (auto& s : slots)
        {
            s.store(nullptr, std::memory_order_relaxed);
        }

        // Submitter threads: each creates composites + starts the poll loop.
        // Requests where (index % 3 == 0) are "long pollers" (effectively
        // unbounded) kept alive until the canceller terminates them mid-flight;
        // the rest self-complete after cfg.polls.
        std::vector<std::thread> submitters;
        submitters.reserve(cfg.submitterThreads);
        for (int t = 0; t < cfg.submitterThreads; t++)
        {
            int base = t * cfg.requestsPerSubmitter;
            submitters.emplace_back([base, &cfg, workPort, &completed, &slots]()
            {
                for (int i = 0; i < cfg.requestsPerSubmitter; i++)
                {
                    int globalIdx = base + i;
                    PollRequest* r = new PollRequest{};
                    r->target = (globalIdx % 3 == 0) ? 0x7fffffff : cfg.polls;
                    r->delayMs = (i & 1) ? 1u : 2u; // tiny delays thrash the "earliest due" pointer
                    r->completed = &completed;

                    if (FAILED(XTaskQueueCreateComposite(workPort, workPort, &r->composite)))
                    {
                        r->MarkDone();
                        delete r;
                        continue;
                    }

                    slots[globalIdx].store(r, std::memory_order_release);

                    if (FAILED(XTaskQueueSubmitCallback(r->composite, XTaskQueuePort::Work, r, PollCallback)))
                    {
                        r->MarkDone();
                    }
                }
            });
        }

        // Canceller thread: terminates the long-poller composites while they
        // still have a delayed poll pending. It only terminates (never closes);
        // the request's own canceled dispatch / aborted re-arm accounts for it.
        std::thread canceller([total, &slots]()
        {
            int handled = 0;
            while (handled < total)
            {
                handled = 0;
                for (int i = 0; i < total; i++)
                {
                    if (i % 3 != 0) { handled++; continue; } // only long pollers
                    PollRequest* r = slots[i].load(std::memory_order_acquire);
                    if (r == nullptr) { continue; } // not yet published
                    r->TerminateOnce();
                    handled++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        for (auto& s : submitters)
        {
            s.join();
        }
        canceller.join();

        // Bounded wait for completion. Starvation => timeout.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.completionTimeoutMs);
        while (completed.load() < total && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        int done = completed.load();

        // Stop pumps and join so nothing touches handles after close.
        pumping.store(false, std::memory_order_release);
        for (auto& p : pumps)
        {
            p.join();
        }

        // All threads joined: terminate (idempotent) + close + free.
        for (auto& slot : slots)
        {
            PollRequest* r = slot.load(std::memory_order_acquire);
            if (r != nullptr)
            {
                if (r->composite != nullptr)
                {
                    r->TerminateOnce();
                    XTaskQueueCloseHandle(r->composite);
                }
                delete r;
            }
        }

        XTaskQueueCloseHandle(queue);

        if (done != total)
        {
            if (failedIteration) { *failedIteration = iter; }
            if (completedAtFailure) { *completedAtFailure = done; }
            return false;
        }
    }

    return true;
}

} // namespace hc_test
