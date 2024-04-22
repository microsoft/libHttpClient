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

/// <summary>
/// Options when duplicating a task queue handle.
/// </summary>
enum class XTaskQueueDuplicateOptions
{
    /// <summary>
    /// Default behavior.
    /// </summary>
    None,

    /// <summary>
    /// The duplicated queue is a reference to the actual
    /// queue object, not a duplicated queue handle. References
    /// work just like fully duplicated handles but they are not
    /// tracked by the handle tracking infrastructure and do not
    /// cause an allocation for the handle.
    /// </summary>
    Reference
};

/// <summary>
/// Increments the refcount on the queue and allows supplying
/// options as to how the duplicate is performed.
/// </summary>
STDAPI XTaskQueueDuplicateHandleWithOptions(
    _In_ XTaskQueueHandle queueHandle,
    _In_ XTaskQueueDuplicateOptions options,
    _Out_ XTaskQueueHandle *duplicatedHandle
    ) noexcept;

/// <summary>
/// Returns a handle to the process task queue, or nullptr if there is no
/// process task queue.  By default, there is a default process task queue
/// that uses the thread pool for both work and completion ports. This is an
/// internal variant that takes duplicate options.
/// </summary>
STDAPI_(bool) XTaskQueueGetCurrentProcessTaskQueueWithOptions(
    _In_ XTaskQueueDuplicateOptions options,
    _Out_ XTaskQueueHandle *queue
    ) noexcept;
