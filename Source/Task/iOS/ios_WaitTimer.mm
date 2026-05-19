// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include "../WaitTimer.h"

#import <Foundation/Foundation.h>

#include "ios_WaitTimerImpl.h"

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
using TimerDuration = std::chrono::nanoseconds;

namespace
{
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

WaitTimerImpl::WaitTimerImpl()
: m_context(nullptr),
  m_callback(nullptr),
  m_target(nullptr),
  m_timer(nullptr)
{
}

WaitTimerImpl::~WaitTimerImpl()
{
    if (m_timer != nullptr && m_timer.valid)
    {
        [m_timer invalidate];
    }
}

HRESULT WaitTimerImpl::Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback){
    m_context = context;
    m_callback = callback;
    m_target = [ios_WaitTimer_target new];
    return S_OK;
}

void WaitTimerImpl::Start(_In_ uint64_t dueTime)
{
    Cancel();

    // NSTimer consumes a relative interval, so convert the stored steady-clock
    // deadline back into a relative delay right before arming it. Use ceiling
    // rounding so that a sub-millisecond remainder is rounded up rather than
    // truncated to zero, and clamp to zero so a past deadline never produces a
    // negative interval that would cause a tight re-arm loop.
    auto remaining = DeadlineFromDueTime(dueTime) - Clock::now();
    auto ms = std::max(std::chrono::milliseconds(0),
                       std::chrono::ceil<std::chrono::milliseconds>(remaining));

    m_timer = [NSTimer scheduledTimerWithTimeInterval:ms.count() / 1000.0
                                                target:m_target
                                                selector:@selector(timerFireMethod:)
                                                userInfo:[NSValue valueWithPointer:this]
                                                repeats:false];

}

void WaitTimerImpl::Cancel()
{
    if (m_timer != nullptr && m_timer.valid)
    {
        [m_timer invalidate];
        m_timer = nullptr;
    }
}

void WaitTimerImpl::TimerFired()
{
    m_callback(m_context);
}

WaitTimer::WaitTimer() noexcept
: m_impl(nullptr)
{
}

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
    RETURN_IF_NULL_ALLOC(timer);
    RETURN_IF_FAILED(timer->Initialize(context, callback));
    
    m_impl = timer.release();
    
    return S_OK;
}

void WaitTimer::Terminate() noexcept
{
    WaitTimerImpl* timer = m_impl.exchange(nullptr);
    if (timer != nullptr)
    {
        timer->Cancel();
        delete timer;
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
    auto deadline = Clock::now() + std::chrono::milliseconds(msFromNow);
    return DueTimeFromDeadline(deadline);
}

} // namespace OS
