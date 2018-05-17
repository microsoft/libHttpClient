// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

/// <summary>
/// An async_queue_handle_t contains async work.  When you make an async call, that call is placed
/// on an async queue for execution.  An async queue has two sides:  a worker side and
/// a completion side.  Each side can have different rules for how queued calls
/// are dispatched.
///
/// You don't have to create an async queue to make async calls.  If no queue is passed
/// the call will use a default queue. Work on the default queue will be invoked on
/// the system thread pool. Completions will be invoked on the thread that initiated
/// the async call when that thread is alertable.
/// </summary>
typedef struct async_queue_t* async_queue_handle_t;

/// <summary>
/// An AsyncBlock defines a piece of asynchronous work.  An async block can be used
/// to poll for an async call's status and retrieve call results.  An AsyncBlock
/// needs to remain valid for the lifetime of an async call.
/// </summary>
struct AsyncBlock;

/// <summary>
/// Callback routine that is invoked when an async call completes. Use GetAsyncStatus
/// or the async call's Get*Result method to obtain the results of the call.  If the
/// call was canceled these methods will return E_ABORT.
/// </summary>
/// <param name='asyncBlock'>A pointer to the AsyncBlock that was passed to the async call.</param>
/// <seealso cref='AsyncBlock' />
typedef void CALLBACK AsyncCompletionRoutine(_Inout_ struct AsyncBlock* asyncBlock);

/// <summary>
/// Callback routine that is invoked on a worker asynchronously when RunAsync is called.
/// The result of the callback becomes the result of the async call.
/// </summary>
/// <param name='asyncBlock'>A pointer to the AsyncBlock that was passed to RunAsync.</param>
/// <seealso cref='AsyncBlock' />
/// <seealso cref='RunAsync' />
typedef HRESULT CALLBACK AsyncWork(_Inout_ struct AsyncBlock* asyncBlock);

typedef struct AsyncBlock
{
    /// <summary>
    /// Optional queue to queue the call on
    /// </summary>
    async_queue_handle_t queue;

    /// <summary>
    /// Optional event to wait on.  This will be signal
    /// when the async operation is complete and after
    /// any completion callback has run
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
    unsigned char internal[sizeof(void*) * 4];
} AsyncBlock;

/// <summary>
/// Returns the status of the asynchronous operation, optionally waiting
/// for it to complete.  Once complete, you may call GetAsyncResult if
/// the async call has a resulting data payload. If it doesn't, calling
/// GetAsyncResult is unneeded.  If a completion callback was set into
/// the asyncBlock, a wait will wait until the completion callback has
/// returned.
/// </summary>
/// <param name='asyncBlock'>A pointer to the AsyncBlock that was passed to the async call.</param>
/// <param name='wait'>If true, GetAsyncStatus waits until the async call either completes or is canceled.</param>
STDAPI GetAsyncStatus(
    _In_ AsyncBlock* asyncBlock,
    _In_ bool wait);

/// <summary>
/// Returns the required size of the buffer to pass to GetAsyncResult.
/// </summary>
/// <param name='asyncBlock'>A pointer to the AsyncBlock that was passed to the async call.</param>
/// <param name='bufferSize'>The required size of the buffer in bytes needed to hold the results.</param>
STDAPI GetAsyncResultSize(
    _In_ AsyncBlock* asyncBlock,
    _Out_ size_t* bufferSize);

/// <summary>
/// Cancels an asynchronous operation. The status result will return E_ABORT,
/// the completion callback will be invoked and the event in the async block will be
/// signaled.  This does nothing if the call has already completed.
/// </summary>
/// <param name='asyncBlock'>A pointer to the AsyncBlock that was passed to the async call.</param>
STDAPI_(void) CancelAsync(
    _Inout_ AsyncBlock* asyncBlock);

/// <summary>
/// Runs the given callback asynchronously.
/// </summary>
/// <param name='asyncBlock'>A pointer to an async block that is used to track the async call.</param>
/// <param name='work'>A pointer to a callback function to run asynchronously.</param>
STDAPI RunAsync(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ AsyncWork* work);


