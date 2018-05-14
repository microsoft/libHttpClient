// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "CriticalThread.h"

static uint64_t const CRITICAL_FALSE  = 0x00;
static uint64_t const CRITICAL_TRUE   = 0x01;
static uint64_t const CRITICAL_LOCKED = 0x02;

static thread_local uint64_t tls_threadState = CRITICAL_FALSE;

/// <summary>
/// Call this to setup a thread as "time critical".  APIs that should not be called from
/// time critical threads call VerifyNotTimeCriticalThread, which will fail if called
/// from a thread marked time critical.
/// </summary>
STDAPI SetTimeCriticalThread(_In_ bool isTimeCriticalThread)
{
    uint64_t current = tls_threadState;
    uint64_t value = isTimeCriticalThread ? CRITICAL_TRUE : CRITICAL_FALSE;

    if (current & CRITICAL_LOCKED)
    {
        value |= CRITICAL_LOCKED;

        if (value != current)
        {
            RETURN_HR(E_ACCESSDENIED);
        }
    }

    tls_threadState = value;

    return S_OK;
}

/// <summary>
/// Returns E_TIME_CRITICAL_THREAD if called from a thread marked as time critical,
/// or S_OK otherwise.
/// </summary>
STDAPI VerifyNotTimeCriticalThread()
{
    if ((tls_threadState & CRITICAL_TRUE) == 0)
    {
        return S_OK;
    }

    RETURN_HR(E_TIME_CRITICAL_THREAD);
}

/// <summary>
/// Locks the time critical state of a thread.  This fixes the time critical
/// setting on the thread for the lifetime of the thread.  Any attempt to change
/// the state will return E_ACCESS_DENIED;
/// </summary>
STDAPI LockTimeCriticalThread()
{
    tls_threadState |= CRITICAL_LOCKED;
    return S_OK;
}
