// Copyright(c) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "XTaskQueue.h"

//----------------------------------------------------------------//
//
// These APIs should be reserved for driving unit test harnesses.
//
//----------------------------------------------------------------//

/// <summary>
/// Returns TRUE if there is no outstanding work in this
/// queue for the given callback port.
/// </summary>
/// <param name='queue'>The queue to check.</param>
/// <param name='port'>The port to check.</param>
STDAPI_(bool) XTaskQueueIsEmpty(
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort port
    ) noexcept;

/// <summary>
/// Suspends terminations on the task queue.  May return an error if
/// the queue is already terminated.
/// </summary>
/// <param name='queue'>The queue to suspend terminations.</param>
STDAPI XTaskQueueSuspendTermination(
    _In_ XTaskQueueHandle queue
    ) noexcept;

/// <summary>
/// Resumes the ability to terminate the task queue. If a termination was
/// attempted it will be continued.
/// </summary>
/// <param name='queue'>The queue resume terminations.</param>
STDAPI_(void) XTaskQueueResumeTermination(
    _In_ XTaskQueueHandle queue
    ) noexcept;

//----------------------------------------------------------------//
//
// These APIs are internal to the runtime
//
//----------------------------------------------------------------//

/// <summary>
/// Suspends the activity of all task queues in the process. When
/// a task queue is suspended:
///
/// 1. It will not signal when new items are added.
/// 2. It will not return items from the dispatcher (it acts like it
///    is empty).
/// </summary>
STDAPI_(void) XTaskQueueGlobalSuspend();

/// <summary>
/// Resumes the activity of all task queues in the process. When
/// a task queue is resumed:
///
/// 1. Queues that are not empty will signal they have items.
/// 2. The dispatcher will start returing items again.
/// </summary>
STDAPI_(void) XTaskQueueGlobalResume();
