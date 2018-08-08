// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include "timer.h"

#import <Foundation/Foundation.h>

#include "ios_timer_target.h"

struct TimerWrapper
{
    NSTimer* timer;
};

struct TargetWrapper
{
    ios_timer_target* target;
};

PlatformTimer::PlatformTimer(void* context, PlatformTimerCallback* callback) noexcept :
    m_context{ context },
    m_callback{ callback },
    m_timerWrapper{ new TimerWrapper },
    m_targetWrapper{ new TargetWrapper }
{
    m_targetWrapper->target = [ios_timer_target new];
    m_timerWrapper->timer = nullptr;
}

PlatformTimer::~PlatformTimer() noexcept
{
    if (m_timerWrapper->timer != nullptr && m_timerWrapper->timer.valid)
    {
        [m_timerWrapper->timer invalidate];
    }
}

bool PlatformTimer::Valid() const noexcept
{
    return true;
}

void PlatformTimer::Start(uint32_t delayInMs) noexcept
{
    if (m_timerWrapper->timer != nullptr && m_timerWrapper->timer.valid)
    {
        // we are already running
        assert(false);
    }
    else
    {
        float interval = static_cast<float>(delayInMs) / 1000.0f;
        m_timerWrapper->timer = [NSTimer scheduledTimerWithTimeInterval: interval
                                                                 target:m_targetWrapper->target
                                                               selector:@selector(timerFireMethod:)
                                                               userInfo:[NSValue valueWithPointer:this]
                                                                repeats:false];

        auto curLoop = [NSRunLoop currentRunLoop];
        auto curMode = [curLoop currentMode];
        if (curMode == nil)
        {
            NSDate *now = [NSDate date];
            NSDate *until = [now dateByAddingTimeInterval:interval * 1.1f];
            [curLoop runUntilDate:until];
        }
    }
}

void PlatformTimer::Cancel() noexcept
{
    if (m_timerWrapper != nullptr && m_timerWrapper->timer.valid)
    {
        [m_timerWrapper->timer invalidate];
        m_timerWrapper->timer = nullptr;
    }
}
