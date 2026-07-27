// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Shared, platform-neutral scenario for the single-port delayed-callback strand
// regression. Exercised by:
//   - the Windows unit test VerifySingleQueueDelayedPollStrand
//     (TaskQueueTests.cpp, Win32 WaitTimer backend), and
//   - the standalone Linux repro binary (CMake target, STL WaitTimer backend).
//
// This complements CompositeQueueStarvationScenario.h. That scenario keeps many
// concurrent submitters and terminate races running for the whole measurement
// window, so there is always independent traffic whose timer fires sweep the
// pending list and rescue a momentarily-stranded entry. That continuous rescue
// traffic masks the strand this scenario targets.
//
// Here we reproduce the field failure shape directly: a single self-resubmitting
// delayed-poll loop on one manual Work port (mirroring a platform HTTP provider
// that re-arms its poll every few ms via XTaskQueueSubmitDelayedCallback). A
// short burst of unrelated one-shot delayed callbacks runs concurrently to
// create timer-arming contention, then STOPS. After the burst drains, the poll
// loop is the only remaining work: if a re-arm was dropped during the burst,
// nothing sweeps the pending list to rescue it, the poll loop stops advancing,
// and the queue is permanently stalled.
//
// The STL timer backend (Linux / non-Microsoft platforms) is where the
// regression manifests, so running this on Linux CI guards the platform the
// Windows lanes cannot reach.
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

struct SingleQueuePollStrandConfig
{
    int outerIterations = 50;
    // Victim poll loop: a single delayed callback that re-arms itself.
    uint32_t pollDelayMs = 2;
    // Contention burst: unrelated one-shot delayed callbacks submitted onto the
    // same Work port from several threads, then stopped. Tiny delays thrash the
    // "earliest due" arming the same way concurrent real work does.
    int burstThreads = 4;
    int burstSubmitsPerThread = 250;
    uint32_t burstDelayMs = 1;
    // After the burst drains, the poll loop must advance by at least this many
    // additional polls within the timeout. A stranded loop advances by zero.
    int progressAfterBurstPolls = 30;
    int progressTimeoutMs = 5000;
    // Bound on how long to wait for the burst callbacks to drain.
    int burstDrainTimeoutMs = 5000;
};

namespace detail
{

struct StrandPollRequest
{
    XTaskQueueHandle queue{ nullptr };
    uint32_t delayMs{ 0 };
    std::atomic<bool>* keepGoing{ nullptr };
    std::atomic<int> polls{ 0 };
};

inline void CALLBACK StrandPollCallback(void* context, bool cancelled)
{
    StrandPollRequest* r = static_cast<StrandPollRequest*>(context);

    if (cancelled)
    {
        // Dispatched as cancelled during teardown; do not re-arm.
        return;
    }

    r->polls.fetch_add(1, std::memory_order_release);

    if (r->keepGoing->load(std::memory_order_acquire))
    {
        // Re-arm the poll. This is the exact pattern a platform HTTP provider
        // uses to schedule its next poll, and the operation whose dropped
        // re-arm strands the loop.
        (void)XTaskQueueSubmitDelayedCallback(r->queue, XTaskQueuePort::Work, r->delayMs, r, StrandPollCallback);
    }
}

struct BurstRequest
{
    std::atomic<int>* outstanding{ nullptr };
};

inline void CALLBACK BurstCallback(void* context, bool /*cancelled*/)
{
    BurstRequest* r = static_cast<BurstRequest*>(context);
    r->outstanding->fetch_sub(1, std::memory_order_acq_rel);
    delete r;
}

} // namespace detail

// Runs the scenario. Returns true if the poll loop kept advancing after the
// contention burst ended in every iteration (healthy timer arming). Returns
// false if the loop stalled in any iteration (a dropped re-arm was never
// rescued). If failedIteration / pollsAfterStallStart are provided, they
// receive the first failing iteration index and how many polls the loop had
// reached when it stalled.
inline bool RunSingleQueuePollStrandScenario(
    const SingleQueuePollStrandConfig& cfg = SingleQueuePollStrandConfig{},
    int* failedIteration = nullptr,
    int* pollsAtStall = nullptr)
{
    using detail::StrandPollRequest;
    using detail::StrandPollCallback;
    using detail::BurstRequest;
    using detail::BurstCallback;

    for (int iter = 0; iter < cfg.outerIterations; iter++)
    {
        XTaskQueueHandle queue{ nullptr };
        if (FAILED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue)))
        {
            if (failedIteration) { *failedIteration = iter; }
            if (pollsAtStall) { *pollsAtStall = -1; }
            return false;
        }

        // Single pump thread for the shared Work port.
        std::atomic<bool> pumping{ true };
        std::thread pump([queue, &pumping]()
        {
            while (pumping.load(std::memory_order_acquire))
            {
                XTaskQueueDispatch(queue, XTaskQueuePort::Work, 50);
            }
        });

        std::atomic<bool> keepGoing{ true };

        StrandPollRequest poller{};
        poller.queue = queue;
        poller.delayMs = cfg.pollDelayMs;
        poller.keepGoing = &keepGoing;

        // Start the self-resubmitting poll loop and let it establish itself.
        if (FAILED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, cfg.pollDelayMs, &poller, StrandPollCallback)))
        {
            keepGoing.store(false, std::memory_order_release);
            pumping.store(false, std::memory_order_release);
            pump.join();
            XTaskQueueCloseHandle(queue);
            if (failedIteration) { *failedIteration = iter; }
            if (pollsAtStall) { *pollsAtStall = -1; }
            return false;
        }

        {
            auto established = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
            while (poller.polls.load(std::memory_order_acquire) == 0 &&
                   std::chrono::steady_clock::now() < established)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Contention burst: several threads each submit a run of unrelated
        // one-shot delayed callbacks onto the same Work port, racing the poll
        // loop's re-arms against the timer worker's arming, then stop.
        std::atomic<int> outstanding{ 0 };
        std::vector<std::thread> burst;
        burst.reserve(cfg.burstThreads);
        for (int t = 0; t < cfg.burstThreads; t++)
        {
            burst.emplace_back([queue, &cfg, &outstanding]()
            {
                for (int i = 0; i < cfg.burstSubmitsPerThread; i++)
                {
                    BurstRequest* b = new BurstRequest{};
                    b->outstanding = &outstanding;
                    outstanding.fetch_add(1, std::memory_order_acq_rel);
                    if (FAILED(XTaskQueueSubmitDelayedCallback(queue, XTaskQueuePort::Work, cfg.burstDelayMs, b, BurstCallback)))
                    {
                        outstanding.fetch_sub(1, std::memory_order_acq_rel);
                        delete b;
                    }
                }
            });
        }

        for (auto& b : burst)
        {
            b.join();
        }

        // Let the burst callbacks drain so the poll loop is the only remaining
        // work on the queue.
        {
            auto drainDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.burstDrainTimeoutMs);
            while (outstanding.load(std::memory_order_acquire) > 0 &&
                   std::chrono::steady_clock::now() < drainDeadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // With no remaining rescue traffic, the poll loop must keep advancing.
        // A strand introduced during the burst shows up here as zero progress.
        int pollsAtBurstEnd = poller.polls.load(std::memory_order_acquire);
        int target = pollsAtBurstEnd + cfg.progressAfterBurstPolls;

        bool progressed = false;
        {
            auto progressDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.progressTimeoutMs);
            while (std::chrono::steady_clock::now() < progressDeadline)
            {
                if (poller.polls.load(std::memory_order_acquire) >= target)
                {
                    progressed = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }

        // Teardown: stop the loop, stop the pump, then terminate + drain so any
        // pending (including a stranded) poll callback is dispatched as
        // cancelled before the queue and the stack-allocated poller go away.
        keepGoing.store(false, std::memory_order_release);
        pumping.store(false, std::memory_order_release);
        pump.join();

        XTaskQueueTerminate(queue, false, nullptr, nullptr);
        while (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) {}
        while (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0)) {}
        XTaskQueueCloseHandle(queue);

        if (!progressed)
        {
            if (failedIteration) { *failedIteration = iter; }
            if (pollsAtStall) { *pollsAtStall = pollsAtBurstEnd; }
            return false;
        }
    }

    return true;
}

} // namespace hc_test
