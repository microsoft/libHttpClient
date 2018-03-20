// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

/// <summary>
/// An async_queue_t contains async work. An async queue has two sides:  a worker side and
/// a completion side.  Each side can have different rules for how queued calls
/// are dispatched.
/// </summary>
typedef HANDLE async_queue_t;

typedef enum AsyncQueueDispatchMode
{
    /// <summary>
    /// Async callbacks are invoked manually by DispatchAsyncQueue
    /// </summary>
    AsyncQueueDispatchMode_Manual,

    /// <summary>
    /// Async callbacks are queued to the thread that created the queue
    /// and will be processed when the thread is alertable.
    /// </summary>
    AsyncQueueDispatchMode_FixedThread,

    /// <summary>
    /// Async callbacks are queued to the system thread pool and will
    /// be processed in order by the threadpool.
    /// </summary>
    AsyncQueueDispatchMode_ThreadPool
} AsyncQueueDispatchMode;

typedef enum AsyncQueueCallbackType
{
    /// <summary>
    /// Async work callbacks
    /// </summary>
    AsyncQueueCallbackType_Work,

    /// <summary>
    /// Completion callbacks after work is done
    /// </summary>
    AsyncQueueCallbackType_Completion
} AsyncQueueCallbackType;

typedef void CALLBACK AsyncQueueCallback(_In_ void* context);
typedef void CALLBACK AsyncQueueCallbackSubmitted(_In_ void* context, _In_ async_queue_t queue, _In_ AsyncQueueCallbackType type);
typedef bool CALLBACK AsyncQueueRemovePredicate(_In_ void* predicateContext, _In_ void* callbackContext);

/// <summary>
/// Creates an Async Queue, which can be used to queue
/// different async calls together.
/// </summary>
STDAPI CreateAsyncQueue(
    _In_ AsyncQueueDispatchMode workDispatchMode,
    _In_ AsyncQueueDispatchMode completionDispatchMode,
    _Out_ async_queue_t* queue);

/// <summary>
/// Creates an async queue with the given shared ID and dispatch
/// modes. If there is already a queue with this ID and dispatch
/// modes, it will be referenced.  Otherwise a new queue will be
/// created.  Call CloseAsyncQueue when done with the shared instance.
/// </summary>
STDAPI CreateSharedAsyncQueue(
    _In_ uint32_t id,
    _In_ AsyncQueueDispatchMode workerMode,
    _In_ AsyncQueueDispatchMode completionMode,
    _Out_ async_queue_t* queue);

/// <summary>
/// Creates an async queue suitable for invoking child tasks.
/// A nested queue dispatches its work through the parent
/// queue.  Both work and completions are dispatched through
/// the parent as "work" callback types.  A nested queue is useful
/// for performing intermediate work within a larger task.
/// </summary>
STDAPI CreateNestedAsyncQueue(
    _In_ async_queue_t parentQueue,
    _Out_ async_queue_t* queue);

/// <summary>
/// Increments the reference count on the async queue.  Call CloseAsyncQueue
/// to decrement.
/// </summary>
STDAPI ReferenceAsyncQueue(
    _In_ async_queue_t queue);

/// <summary>
/// Processes items in the async queue of the given type. If an item
/// is processed this will return TRUE. If there are no items to process
/// this returns FALSE.  You can pass a timeout, which will cause
/// DispatchAsyncQueue to wait for something to arrive in the queue.
/// </summary>
STDAPI_(bool) DispatchAsyncQueue(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ uint32_t timeout);

/// <summary>
/// Returns TRUE if there is no outstanding work in this
/// queue for the given callback type.
/// </summary>
STDAPI_(bool) IsAsyncQueueEmpty(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type);

/// <summary>
/// Closes the async queue.  A queue can only be closed if it
/// is not in use by an async api or is empty.  If not true, the queue
/// will be marked for closure and closed when it can. 
/// </summary>
STDAPI_(void) CloseAsyncQueue(
    _In_ async_queue_t queue);

/// <summary>
/// Submits a callback to the queue for the given callback
/// type.
/// </summary>
STDAPI SubmitAsyncCallback(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ void* callbackContext,
    _In_ AsyncQueueCallback* callback);

/// <summary>
/// Walks all callbacks in the queue of the given type searching
/// for callback pointers that match searchCallback. For each one,
/// the predicate is invoked with the callback's context.  If
/// the predicate wishes to remove the callback, it should return
/// true.  The predicate is invoked while holding a lock on the
/// async queue -- care should be taken to do no work on the
/// qsync queue within the predicate or you could deadlock.
/// This should be called before an object is deleted
/// to ensure there are no orphan callbacks in the async queue
/// that could later call back into the deleted object.
/// </summary>
STDAPI_(void) RemoveAsyncQueueCallbacks(
    _In_ async_queue_t queue,
    _In_ AsyncQueueCallbackType type,
    _In_ AsyncQueueCallback* searchCallback,
    _In_opt_ void* predicateContext,
    _In_ AsyncQueueRemovePredicate* removePredicate);

/// <summary>
/// Adds a callback that will be invoked whenever a callback
/// is submitted to this queue.
/// </summary>
STDAPI AddAsyncCallbackSubmitted(
    _In_ async_queue_t queue,
    _In_opt_ void* context,
    _In_ AsyncQueueCallbackSubmitted* callback,
    _Out_ uint32_t* token);

/// <summary>
/// Removes a previously added callback.
/// </summary>
STDAPI_(void) RemoveAsyncQueueCallbackSubmitted(
    _In_ async_queue_t queue,
    _In_ uint32_t token);
