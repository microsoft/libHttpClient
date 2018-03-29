// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

/// <summary>
/// An async_queue_t contains async work.  When you make an async call, that call is placed
/// on an async queue for execution.  An async queue has two sides:  a worker side and
/// a completion side.  Each side can have different rules for how queued calls
/// are dispatched.
///
/// You don't have to create an async queue to make async calls.  If no queue is passed
/// the call will use a default queue. Work on the default queue will be invoked on
/// the system thread pool. Completions will be invoked on the thread that initiated
/// the async call when that thread is alertable.
/// </summary>
typedef void* async_queue_t;

/// <summary>
/// An AsyncBlock defines a piece of asynchronous work.  An async block can be used
/// to poll for an async call's status and retrieve call results.  An AsyncBlock
/// needs to remain valid for the lifetime of an async call.
/// </summary>
struct AsyncBlock;

typedef void CALLBACK AsyncCompletionRoutine(_In_ struct AsyncBlock* asyncBlock);

typedef struct AsyncBlock
{
    /// <summary>
    /// Optional queue to queue the call on
    /// </summary>
    async_queue_t queue;

    /// <summary>
    /// Optional event to wait on
    /// </summary>
    HANDLE waitEvent;

    /// <summary>
    /// Optional context pointer to pass to the callback
    /// </summary>
    void* context;

    /// <summary>
    /// Optional callback that will be invoked when the call completes
    /// </summary>
    AsyncCompletionRoutine* callback;

    /// <summary>
    /// Internal use only
    /// </summary>
    void* internalPtr;

    /// <summary>
    /// Internal use only
    /// </summary>
    HRESULT internalStatus;
} AsyncBlock;

/// <summary>
/// Returns the status of the asynchronous operation, optionally waiting
/// for it to complete.  Once complete, you may call GetAsyncResult if
/// the async call has a resulting data payload. If it doesn't, calling
/// GetAsyncResult is unneeded.
/// </summary>
HCAPI GetAsyncStatus(
    _In_ AsyncBlock* asyncBlock,
    _In_ bool wait);

/// <summary>
/// Returns the required size of the buffer to pass to GetAsyncResult.
/// </summary>
HCAPI GetAsyncResultSize(
    _In_ AsyncBlock* asyncBlock,
    _Out_ size_t* bufferSize);

/// <summary>
/// Cancels an asynchronous operation. The status result will return E_ABORT,
/// the completion callback will be invoked and the event in the async block will be
/// signaled.
/// </summary>
HCAPI_(void) CancelAsync(
    _In_ AsyncBlock* asyncBlock);

typedef HRESULT CALLBACK AsyncWork(_In_ AsyncBlock* asyncBlock);

/// <summary>
/// Runs the given callback asynchronously.
/// </summary>
HCAPI RunAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ AsyncWork* work);

