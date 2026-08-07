// Copyright(c) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "XTaskQueue.h"

#if defined(HC_ENABLE_UNNAMED_OBJECT_DIAGNOSTICS)
struct XTaskQueueSyncObjectCounts
{
    uint64_t constructed;
    uint64_t destroyed;
    uint64_t live;
};

struct XTaskQueueSyncObjectSnapshot
{
    XTaskQueueSyncObjectCounts mutex;
    XTaskQueueSyncObjectCounts recursiveMutex;
    XTaskQueueSyncObjectCounts conditionVariable;
    XTaskQueueSyncObjectCounts conditionVariableAny;
    bool usesUnnamedConstructors;
};

/// <summary>
/// Gets aggregate construction and destruction counts for the synchronization
/// wrappers used by this module. Available only in diagnostic test builds.
/// </summary>
STDAPI XTaskQueueGetSyncObjectDiagnosticSnapshot(
    _Out_ XTaskQueueSyncObjectSnapshot* snapshot
    ) noexcept;

/// <summary>
/// Constructs and destroys one of each synchronization wrapper, including a
/// recursive re-entry, and returns snapshots at each test boundary.
/// </summary>
STDAPI XTaskQueueRunSyncObjectDiagnosticProbe(
    _Out_ XTaskQueueSyncObjectSnapshot* before,
    _Out_ XTaskQueueSyncObjectSnapshot* during,
    _Out_ XTaskQueueSyncObjectSnapshot* after
    ) noexcept;

/// <summary>
/// Closes per-process queue handles and waits for outstanding TaskQueue
/// lifecycle references to drain. This is an existing private test API.
/// </summary>
STDAPI_(bool) XTaskQueueUninitialize(
    _In_ uint32_t timeoutMilliseconds
    ) noexcept;
#endif
