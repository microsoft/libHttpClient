// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Standalone regression guard for issue #1072: concurrent XTaskQueueSubmitCallback
// calls raced on LocklessQueue node payloads (List::pop copied a node's payload
// before winning the head compare-exchange, while another thread could already be
// overwriting it). Delivery stayed correct, so the race only shows up under
// ThreadSanitizer: the Linux TSan CI leg (HC_ENABLE_TSAN) fails this test if any
// data race is reported. In other builds it still checks every callback runs once.
//
// Exit codes:
//   0  - every callback ran exactly once and the queue terminated cleanly
//   1  - a callback was rejected, cancelled, lost or ran more than once
//   (ThreadSanitizer exits with 66 when it reports a race)
//
// Built as the CTest target "taskqueue-concurrent-submit-linux" in
// Build/libHttpClient.Linux/CMakeLists.txt.

#include <httpClient/config.h>
#include <httpClient/pal.h>
#include <XTaskQueue.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

constexpr unsigned c_rounds = 4;
constexpr unsigned c_producers = 4;
constexpr unsigned c_callbacksPerProducer = 512;
constexpr unsigned c_callbacks = c_producers * c_callbacksPerProducer;

struct RoundState;

struct Slot
{
    RoundState* owner{ nullptr };
    std::atomic<unsigned> visits{ 0 };
};

struct RoundState
{
    std::mutex mutex;
    std::condition_variable cv;
    unsigned completed{ 0 };
    std::atomic<unsigned> rejected{ 0 };
    std::atomic<unsigned> cancelled{ 0 };
    std::unique_ptr<Slot[]> slots{ new Slot[c_callbacks] };
};

void CALLBACK OnCallback(void* context, bool cancelled)
{
    auto& slot = *static_cast<Slot*>(context);
    auto& owner = *slot.owner;
    slot.visits.fetch_add(1, std::memory_order_relaxed);
    if (cancelled)
    {
        owner.cancelled.fetch_add(1, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock{ owner.mutex };
        ++owner.completed;
    }
    owner.cv.notify_all();
}

bool RunRound(unsigned round)
{
    XTaskQueueHandle queue = nullptr;
    HRESULT hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue);
    if (FAILED(hr))
    {
        std::printf("[taskqueue-concurrent-submit] round %u: XTaskQueueCreate failed 0x%08x\n", round, static_cast<unsigned>(hr));
        return false;
    }

    RoundState state;
    for (unsigned i = 0; i < c_callbacks; ++i)
    {
        state.slots[i].owner = &state;
    }

    std::vector<std::thread> producers;
    for (unsigned producer = 0; producer < c_producers; ++producer)
    {
        producers.emplace_back([&, producer]
        {
            for (unsigned i = 0; i < c_callbacksPerProducer; ++i)
            {
                Slot* slot = &state.slots[producer * c_callbacksPerProducer + i];
                if (FAILED(XTaskQueueSubmitCallback(queue, XTaskQueuePort::Work, slot, OnCallback)))
                {
                    state.rejected.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& producer : producers)
    {
        producer.join();
    }

    unsigned const accepted = c_callbacks - state.rejected.load();
    bool complete;
    {
        std::unique_lock<std::mutex> lock{ state.mutex };
        complete = state.cv.wait_for(lock, std::chrono::seconds(60), [&] { return state.completed == accepted; });
    }

    HRESULT terminated = XTaskQueueTerminate(queue, true, nullptr, nullptr);
    XTaskQueueCloseHandle(queue);

    unsigned mismatches = 0;
    for (unsigned i = 0; i < c_callbacks; ++i)
    {
        mismatches += state.slots[i].visits.load() != 1;
    }

    bool const ok = complete && SUCCEEDED(terminated) && state.rejected.load() == 0 && state.cancelled.load() == 0 && mismatches == 0;
    if (!ok)
    {
        std::printf(
            "[taskqueue-concurrent-submit] round %u FAILED: accepted=%u completed=%u rejected=%u cancelled=%u mismatches=%u terminate=0x%08x\n",
            round, accepted, state.completed, state.rejected.load(), state.cancelled.load(), mismatches, static_cast<unsigned>(terminated));
    }
    return ok;
}

} // namespace

int main()
{
    std::printf("[taskqueue-concurrent-submit] %u rounds of %u producers x %u callbacks\n", c_rounds, c_producers, c_callbacksPerProducer);
    std::fflush(stdout);

    for (unsigned round = 0; round < c_rounds; ++round)
    {
        if (!RunRound(round))
        {
            std::fflush(stdout);
            return 1;
        }
    }

    std::printf("[taskqueue-concurrent-submit] PASSED\n");
    std::fflush(stdout);
    return 0;
}
