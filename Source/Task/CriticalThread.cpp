// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "CriticalThread.h"

static INIT_ONCE s_tlsInit = INIT_ONCE_STATIC_INIT;
static DWORD s_tlsSlot;

#define CRITICAL_FALSE       0x00
#define CRITICAL_TRUE        0x01
#define CRITICAL_LOCKED      0x02

static HRESULT InitTls()
{
    BOOL pending;

    if (!InitOnceBeginInitialize(&s_tlsInit, 0, &pending, nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (pending)
    {
        DWORD flags = 0;
        DWORD error =  NOERROR;

        s_tlsSlot = TlsAlloc();
        if (s_tlsSlot == TLS_OUT_OF_INDEXES)
        {
            flags |= INIT_ONCE_INIT_FAILED;
            error = GetLastError();
        }

        if (!InitOnceComplete(&s_tlsInit, flags, nullptr))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        return HRESULT_FROM_WIN32(error);
    }

    return S_OK;
}

/// <summary>
/// Call this to setup a thread as "time critical".  APIs that should not be called from
/// time critical threads call VerifyNotTimeCriticalThread, which will fail if called
/// from a thread marked time critical.
/// </summary>
STDAPI SetTimeCriticalThread(_In_ bool isTimeCriticalThread)
{
    RETURN_IF_FAILED(InitTls());

    ULONG_PTR current = reinterpret_cast<ULONG_PTR>(TlsGetValue(s_tlsSlot));
    ULONG_PTR value = isTimeCriticalThread ? CRITICAL_TRUE : CRITICAL_FALSE;

    if (current & CRITICAL_LOCKED)
    {
        value |= CRITICAL_LOCKED;

        if (value != current)
        {
            RETURN_HR(E_ACCESSDENIED);
        }
    }

    TlsSetValue(s_tlsSlot, reinterpret_cast<PVOID>(value));
    
    return S_OK;
}

/// <summary>
/// Returns E_TIME_CRITICAL_THREAD if called from a thread marked as time critical,
/// or S_OK otherwise.
/// </summary>
STDAPI VerifyNotTimeCriticalThread()
{
    BOOL pending;
    if (!InitOnceBeginInitialize(&s_tlsInit, INIT_ONCE_CHECK_ONLY, &pending, nullptr) || pending)
    {
        // This thread is either still initializing or was never initialized.  It
        // can't be time critical.
        return S_OK;
    }

    ULONG_PTR timeCritical = reinterpret_cast<ULONG_PTR>(TlsGetValue(s_tlsSlot));

    if ((timeCritical & CRITICAL_TRUE) == 0)
    {
        DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error);
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
    RETURN_IF_FAILED(InitTls());

    ULONG_PTR current = reinterpret_cast<ULONG_PTR>(TlsGetValue(s_tlsSlot)) | CRITICAL_LOCKED;
    TlsSetValue(s_tlsSlot, reinterpret_cast<PVOID>(current));
   
    return S_OK;
}
