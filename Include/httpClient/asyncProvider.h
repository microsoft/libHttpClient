// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

typedef enum AsyncOp
{
    /// <summary>
    /// An async provider is invoked with this opcode when ScheduleAsync has been called to
    /// schedule work. Implementations should perform their asynchronous work and then call
    /// CompleteAsync with the data payload size. If additonal work needs to be done they
    /// can schedule it and return E_PENDING.
    /// </summary>
    AsyncOp_DoWork,

    /// <summary>
    /// An async provider is invoked with this opcode after an async call completes and the
    /// user needs to get the resulting data payload. The buffer and bufferSize have
    /// been arg checked.
    /// </summary>
    AsyncOp_GetResult,

    /// <summary>
    /// An async provider is invoked with this opcode when the async work should be canceled.
    /// </summary>
    AsyncOp_Cancel,

    /// <summary>
    /// An async provider is invoked with this opcode when the async call is over and
    /// data in the context can be cleaned up.
    /// </summary>
    AsyncOp_Cleanup
} AsyncOp;

typedef struct AsyncProviderData
{
    /// <summary>
    /// The async block for the call.
    /// </summary>
    AsyncBlock* async;

    /// <summary>
    /// The async queue for the call.  This will never be null -- either it will point
    /// to the queue inside the above async block or it will point to a temporary queue
    /// being used for the duration of the call.
    /// </summary>
    async_queue_t queue;

    /// <summary>
    /// Valid during a GetResult opcode and holds the size of the buffer.  This will
    /// be at least as large as the data size provided to CompleteAsync.
    /// </summary>
    size_t bufferSize;

    /// <summary>
    /// Valid during a GetResult opcode and holds the output data buffer.
    /// </summary>
    void* buffer;

    /// <summary>
    /// Valid during any opcode this is a user provided context pointer that was provided
    /// to BeginAsync.  It should be freed during the Cleanup opcode.
    /// </summary>
    void* context;
} AsyncProviderData;

typedef HRESULT CALLBACK AsyncProvider(_In_ AsyncOp op, _Inout_ AsyncProviderData* data);

/// <summary>
/// Initializes an async block for use.  Once begun calls such
/// as GetAsyncStatus will provide meaningful data. It is assumed the
/// async work will begin on some system defined thread after this call
/// returns.
/// </summary>
STDAPI BeginAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_opt_ void* token,
    _In_opt_ PCSTR function,
    _In_ AsyncProvider* provider);

/// <summary>
/// Schedules a callback to do async work.  Calling this is optional.  If the async work can be done
/// through a system - async mechanism like overlapped IO or async COM, there is no need to schedule
/// work.  If work should be scheduled after a delay, pass the number of ms ScheduleAsync should wait
/// before it schedules work.
/// </summary>
STDAPI ScheduleAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ uint32_t delay);

/// <summary>
/// Called when async work is completed and the results can be returned.
/// The caller should supply the resulting data payload size.  If the call
/// has no data payload, pass zero.
/// </summary
STDAPI_(void) CompleteAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ HRESULT result,
    _In_ size_t requiredBufferSize);

/// <summary>
/// Returns the result data for the asynchronous operation.  After this call
/// the async block is completed and no longer associated with the 
/// operation.
/// </summary>
STDAPI GetAsyncResult(
    _In_ AsyncBlock* asyncBlock,
    _In_opt_ void* token,
    _In_ size_t bufferSize,
    _Out_writes_bytes_opt_(bufferSize) void* buffer);

