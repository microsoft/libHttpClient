// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

/// <summary>
/// Call this to setup a thread as "time critical".  APIs that should not be called from
/// time critical threads call VerifyNotTimeCriticalThread, which will fail if called
/// from a thread marked time critical.
/// </summary>
/// <param name='isTimeCriticalThread'>True if the current thread shoudl be marked time critical.</param>
STDAPI SetTimeCriticalThread(_In_ bool isTimeCriticalThread);

/// <summary>
/// Locks the time critical state of a thread.  This fixes the time critical
/// setting on the thread for the lifetime of the thread.  Any attempt to change
/// the state will return E_ACCESS_DENIED;
/// </summary>
STDAPI LockTimeCriticalThread();

/// <summary>
/// Returns E_TIME_CRITICAL_THREAD if called from a thread marked as time critical,
/// or S_OK otherwise.
/// </summary>
STDAPI VerifyNotTimeCriticalThread();
