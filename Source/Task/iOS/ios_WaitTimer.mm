// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include "../WaitTimer.h"

#import <Foundation/Foundation.h>

#include "ios_WaitTimerImpl.h"

using Deadline = std::chrono::high_resolution_clock::time_point;
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

void WaitTimerImpl::Start(_In_ uint64_t absoluteTime)
{
    Cancel();

    auto duration = Deadline::duration(absoluteTime);
    auto timePoint = Deadline(duration) - std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint);

    m_timer = [NSTimer scheduledTimerWithTimeInterval:ms.count() / 1000.0
                                                target:m_target
                                                selector:@selector(timerFireMethod:)
                                                userInfo:[NSValue valueWithPointer:this]
                                                repeats:false];

}

void WaitTimer::Terminate() noexcept
{
    std::unique_ptr<WaitTimerImpl> timer(m_impl.exchange(nullptr));
    if (timer != nullptr)
    {
        timer->Cancel();
    }
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
    if (m_impl != nullptr)
    {
        delete m_impl;
    }
}

HRESULT WaitTimer::Initialize(void *context, WaitTimerCallback *callback) noexcept
{
    if (m_impl != nullptr || callback == nullptr)
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

void WaitTimer::Start(uint64_t absoluteTime) noexcept
{
    m_impl.load()->Start(absoluteTime);
}

void WaitTimer::Cancel() noexcept
{
    m_impl.load()->Cancel();
}

uint64_t WaitTimer::GetAbsoluteTime(uint32_t msFromNow) noexcept
{
    auto deadline = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(msFromNow);
    return deadline.time_since_epoch().count();
}

}