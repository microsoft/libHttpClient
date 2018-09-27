// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
// This header defines advanced and rarely used APIS for the async queue.

#ifndef __asyncqueueex_h__
#define __asyncqueueex_h__

/// <summary>
/// Creates an async queue with the given shared ID and dispatch
/// modes. If there is already a queue with this ID and dispatch
/// modes, it will be referenced.  Otherwise a new queue will be
/// created with a reference count of 1.  Call CloseAsyncQueue 
/// when done with the shared instance.
/// </summary>
/// <param name='id'>An ID to identify the shared queue.  All calls with the same ID and work/completion dispatch moders will share the same queue.</param>
/// <param name='workDispatchMode'>The dispatch mode for the "work" side of the queue.</param>
/// <param name='completionDispatchMode'>The dispatch mode for the "completion" side of the queue.</param>
/// <param name='queue'>The shared queue.</param>
STDAPI CreateSharedAsyncQueue(
    _In_ uint32_t id,
    _In_ AsyncQueueDispatchMode workDispatchMode,
    _In_ AsyncQueueDispatchMode completionDispatchMode,
    _Out_ async_queue_handle_t* queue);

/// <summary>
/// Creates an async queue suitable for invoking child tasks.
/// A nested queue dispatches its work through the parent
/// queue.  Both work and completions are dispatched through
/// the parent as "work" callback types.  A nested queue is useful
/// for performing intermediate work within a larger task.
/// </summary>
/// <param name='parentQueue'>The parent queue to use when nesting.</param>
/// <param name='queue'>The newly created queue.</param>
STDAPI CreateNestedAsyncQueue(
    _In_ async_queue_handle_t parentQueue,
    _Out_ async_queue_handle_t* queue);

/// <summary>
/// Creates an async queue by composing elements of 2 other queues.
/// </summary>
/// <param name='workerSourceQueue'>"work" callbacks will be called on this queue.</param>
/// <param name='workerSourceCallbackType'>Determines if "work" callbacks will be called as "work" or completion callbacks on workerSourceQueue.</param>
/// <param name='completionSourceQueue'>"completion" callbacks will be called on this queue.</param>
/// <param name='completionSourceCallbackType'>Determines if "completion" callbacks will be called as "work" or completion callbacks on completionSourceQueue.</param>
/// <param name='queue'>The newly created queue.</param>
STDAPI CreateCompositeAsyncQueue(
    _In_ async_queue_handle_t workerSourceQueue,
    _In_ AsyncQueueCallbackType workerSourceCallbackType,
    _In_ async_queue_handle_t completionSourceQueue,
    _In_ AsyncQueueCallbackType completionSourceCallbackType,
    _Out_ async_queue_handle_t* queue);

#endif // #ifndef __asyncqueueex_h__