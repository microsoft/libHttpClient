// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include "timer.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using Deadline = std::chrono::high_resolution_clock::time_point;

struct TimerEntry
{
    Deadline When;
    PlatformTimer* Timer;

    TimerEntry(Deadline d, PlatformTimer* t) : When{ d }, Timer{ t } {}
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
    bool LazyInit() noexcept;

    void Add(PlatformTimer* timer, Deadline deadline) noexcept;
    void Remove(PlatformTimer const* timer) noexcept;

private:
    void Worker() noexcept;

    TimerEntry const& Peek() const noexcept;
    TimerEntry Pop() noexcept;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<TimerEntry> m_queue; // used as a heap
    bool m_initialized = false;
};

namespace
{

std::once_flag g_timerQueueLazyInit;
TimerQueue g_timerQueue;

}

bool TimerQueue::LazyInit() noexcept
{
    std::call_once(g_timerQueueLazyInit, [this]()
    {
        try
        {
            std::thread t([this]()
            {
                Worker();
            });
            t.detach();

            m_initialized = true;
        }
        catch (...)
        {
            m_initialized = false;
        }
    });

    return m_initialized;
}

void TimerQueue::Add(PlatformTimer* timer, Deadline deadline) noexcept
{
    {
        std::lock_guard<std::mutex> lock{ m_mutex };

        m_queue.emplace_back(deadline, timer);
        std::push_heap(m_queue.begin(), m_queue.end(), TimerEntryComparator{});
    }
    m_cv.notify_all();
}

void TimerQueue::Remove(PlatformTimer const* timer) noexcept
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
    while (true)
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
            // gets destroyed on this thread or readds itself in the callback
            lock.unlock();
            if (entry.Timer) // Timer is set to nullptr if the entry is removed
            {
                entry.Timer->OnDeadline();
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

PlatformTimer::PlatformTimer(void* context, PlatformTimerCallback* callback) noexcept :
    m_context{ context },
    m_callback{ callback },
    m_valid{ g_timerQueue.LazyInit() }
{}

PlatformTimer::~PlatformTimer() noexcept
{
    g_timerQueue.Remove(this);
}

bool PlatformTimer::Valid() const noexcept
{
    return m_valid;
}

void PlatformTimer::Start(uint32_t delayInMs) noexcept
{
    Deadline deadline = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds{ delayInMs };
    g_timerQueue.Add(this, deadline);
}

void PlatformTimer::Cancel() noexcept
{
    g_timerQueue.Remove(this);
}

void PlatformTimer::OnDeadline() noexcept
{
    m_callback(m_context);
}
