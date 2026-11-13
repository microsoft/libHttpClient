// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#if !defined(__cplusplus)
   #error C++11 required
#endif

#pragma once
#include "XTaskQueue.h"

extern "C"
{

/// <summary>
/// An XAsyncBlock defines a piece of asynchronous work.  An async block can be used
/// to poll for an async call's status and retrieve call results.  An XAsyncBlock
/// needs to remain valid for the lifetime of an async call.
///
/// If a completion callback is set, the lifetime of the call extends until that
/// callback has returned, because the callback receives this XAsyncBlock pointer.
/// The call's status can stop being E_PENDING before the callback runs (for
/// example, when the completion port of the queue has not been dispatched yet).
/// Do not reuse, modify, or free the XAsyncBlock until the completion callback
/// has been invoked; reusing it from within the completion callback is allowed.
/// If no completion callback is set, the block may be reused or freed once
/// XAsyncGetStatus no longer returns E_PENDING and any result has been
/// retrieved.
/// </summary>
struct XAsyncBlock;

/// <summary>
/// Callback routine that is invoked when an async call completes. Use XAsyncGetStatus
/// or the async call's Get*Result method to obtain the results of the call.  If the
/// call was canceled these methods will return E_ABORT.
///
/// The completion callback marks the end of the async call. Until it has been
/// invoked, the XAsyncBlock must not be reused for another call or freed, even if
/// XAsyncGetStatus(asyncBlock, false) has already returned a completed status.
/// When a completion callback is set, use the callback (not polling
/// XAsyncGetStatus) to determine when the call has finished.
/// </summary>
/// <param name='asyncBlock'>A pointer to the XAsyncBlock that was passed to the async call.</param>
/// <seealso cref='XAsyncBlock' />
typedef void CALLBACK XAsyncCompletionRoutine(_Inout_ struct XAsyncBlock* asyncBlock);

/// <summary>
/// Callback routine that is invoked on a worker asynchronously when XAsyncRun is called.
/// The result of the callback becomes the result of the async call.
/// </summary>
/// <param name='asyncBlock'>A pointer to the XAsyncBlock that was passed to XAsyncRun.</param>
/// <seealso cref='XAsyncBlock' />
/// <seealso cref='XAsyncRun' />
typedef HRESULT CALLBACK XAsyncWork(_Inout_ struct XAsyncBlock* asyncBlock);

struct XAsyncBlock
{
    /// <summary>
    /// The queue to queue the call on
    /// </summary>
    XTaskQueueHandle queue;

    /// <summary>
    /// Optional context pointer to pass to the callback
    /// </summary>
    void* context;

    /// <summary>
    /// Optional callback that will be invoked when the call completes. If set,
    /// this XAsyncBlock must remain valid and unused by other calls until the
    /// callback has been invoked.
    /// </summary>
    XAsyncCompletionRoutine* callback;

    /// <summary>
    /// Internal use only
    /// </summary>
    unsigned char internal[sizeof(void*) * 4];
};

/// <summary>
/// Returns the status of the asynchronous operation, optionally waiting
/// for it to complete.  Once complete, you may call the api's get result
/// api if the async call has a resulting data payload. If it doesn't, calling
/// a get result method is unneeded.  If a completion callback was set into
/// the asyncBlock, a wait will wait until the completion callback has
/// returned.
///
/// Without a wait, the status reflects the result of the call and can stop
/// being E_PENDING before the completion callback has been invoked (the callback
/// is queued to the completion port of the task queue). A completed status from
/// a non-waiting call therefore does not mean the XAsyncBlock can be reused or
/// freed when a completion callback is set; wait for the callback to be invoked
/// first. Avoid mixing polling and a completion callback for the same call.
/// </summary>
/// <param name='asyncBlock'>A pointer to the XAsyncBlock that was passed to the async call.</param>
/// <param name='wait'>If true, XAsyncGetStatus waits until the async call either completes or is canceled.</param>
STDAPI XAsyncGetStatus(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ bool wait
    ) noexcept;

/// <summary>
/// Returns the required size of the buffer to pass to XAsyncGetResult.
/// </summary>
/// <param name='asyncBlock'>A pointer to the XAsyncBlock that was passed to the async call.</param>
/// <param name='bufferSize'>The required size of the buffer in bytes needed to hold the results.</param>
STDAPI XAsyncGetResultSize(
    _Inout_ XAsyncBlock* asyncBlock,
    _Out_ size_t* bufferSize
    ) noexcept;

/// <summary>
/// Cancels an asynchronous operation. The status result will return E_ABORT,
/// the completion callback will be invoked and the event in the async block will be
/// signaled.  This does nothing if the call has already completed.
/// </summary>
/// <param name='asyncBlock'>A pointer to the XAsyncBlock that was passed to the async call.</param>
STDAPI_(void) XAsyncCancel(
    _Inout_ XAsyncBlock* asyncBlock
    ) noexcept;

/// <summary>
/// Runs the given callback asynchronously.
/// </summary>
/// <param name='asyncBlock'>A pointer to an async block that is used to track the async call.</param>
/// <param name='work'>A pointer to a callback function to run asynchronously.</param>
STDAPI XAsyncRun(
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ XAsyncWork* work
    ) noexcept;

} // extern "C"
