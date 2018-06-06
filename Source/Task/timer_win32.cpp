// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include "timer.h"

PlatformTimer::PlatformTimer(void* context, PlatformTimerCallback* callback) noexcept :
    m_context{ context },
    m_callback{ callback },
    m_timer{ CreateThreadpoolTimer(TimerCallback, this, nullptr) }
{}

PlatformTimer::~PlatformTimer() noexcept
{
    if (m_timer)
    {
        SetThreadpoolTimer(m_timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(m_timer, TRUE);
        CloseThreadpoolTimer(m_timer);
    }
}

bool PlatformTimer::Valid() const noexcept
{
    return m_timer != nullptr;
}

void PlatformTimer::Start(uint32_t delayInMs) noexcept
{
    int64_t delayHns = static_cast<int64_t>(delayInMs) * -10000;
    LARGE_INTEGER li{};
    li.QuadPart = delayHns;

    FILETIME ft{};
    ft.dwHighDateTime = li.HighPart;
    ft.dwLowDateTime = li.LowPart;

    SetThreadpoolTimer(m_timer, &ft, 0, delayInMs);
}

void PlatformTimer::Cancel() noexcept
{
    if (m_timer != nullptr)
    {
        SetThreadpoolTimer(m_timer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(m_timer, TRUE);
    }
}

void CALLBACK PlatformTimer::TimerCallback(
    _In_ PTP_CALLBACK_INSTANCE,
    _In_ void* context,
    _In_ PTP_TIMER timer
) noexcept
{
    auto pt = static_cast<PlatformTimer*>(context);

    assert(timer == pt->m_timer);
    UNREFERENCED_PARAMETER(timer);

    pt->m_callback(pt->m_context);
}
