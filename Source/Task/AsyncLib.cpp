// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "pch.h"
#include "httpClient/async.h"
#include "httpClient/asyncProvider.h"
#include "httpClient/asyncQueue.h"

#define ASYNC_STATE_SIG 0x41535445

struct AsyncState
{
    DWORD signature;
    AsyncProvider* provider;
    AsyncProviderData providerData;
    DWORD workScheduled;
    HANDLE waitEvent;
    PTP_TIMER timer;
    void* token;
    PCSTR function;
};

static void CALLBACK CompletionCallback(_In_ void* context);
static void CALLBACK WorkerCallback(_In_ void* context);
static void CALLBACK TimerCallback(_In_ PTP_CALLBACK_INSTANCE instance, _In_ PVOID context, _In_ PTP_TIMER timer);
static void SignalCompletion(_In_ AsyncBlock* asyncBlock);
static HRESULT AllocState(_In_ AsyncBlock* asyncBlock);
static void CleanupState(_In_ AsyncBlock* asyncBlock);
static AsyncState* GetState(_In_ AsyncBlock* asyncBlock);

static HRESULT AllocState(_In_ AsyncBlock* asyncBlock)
{
    if (GetState(asyncBlock) != nullptr)
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;
    HANDLE event = nullptr;
    AsyncState* state = nullptr;

    if (asyncBlock->waitEvent != nullptr)
    {
        if (!DuplicateHandle(
            GetCurrentProcess(), 
            asyncBlock->waitEvent, 
            GetCurrentProcess(), 
            &event, 0, 
            FALSE, 
            DUPLICATE_SAME_ACCESS))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }
    else
    {
        event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        if (event == nullptr)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
    }

    if (SUCCEEDED(hr))
    {
        state = new (std::nothrow) AsyncState;

        if (state == nullptr)
        {
            hr = E_OUTOFMEMORY;
        }
        else
        {
            ZeroMemory(state, sizeof(AsyncState));
            state->signature = ASYNC_STATE_SIG;
            state->waitEvent = event;
            state->providerData.queue = asyncBlock->queue;
            state->providerData.async = asyncBlock;
        }
    }

    if (SUCCEEDED(hr))
    {
        if (state->providerData.queue != nullptr)
        {
            hr = ReferenceAsyncQueue(state->providerData.queue);
        }
        else
        {
            hr = CreateSharedAsyncQueue(
                (uint32_t)GetCurrentThreadId(),
                AsyncQueueDispatchMode_ThreadPool,
                AsyncQueueDispatchMode_FixedThread,
                &state->providerData.queue);
        }
    }

    if (SUCCEEDED(hr))
    {
        asyncBlock->internalPtr = state;
        asyncBlock->internalStatus = E_PENDING;
    }
    else
    {
        // Attempt to complete the call as a failure, and only return
        // a failed HR here if we couldn't complete.

        asyncBlock->internalPtr = nullptr;
        asyncBlock->internalStatus = hr;

        if (event != nullptr)
        {
            SetEvent(event);
            CloseHandle(event);
        }

        if (asyncBlock->callback != nullptr)
        {
            asyncBlock->callback(asyncBlock);
            hr = S_OK;
        }
        
        if (state != nullptr)
        {
            delete state;
        }
    }

    return hr;
}

static void CleanupState(_In_ AsyncBlock* block)
{
    AsyncState* state = GetState(block);
    block->internalPtr = nullptr;

    if (state != nullptr)
    {
        state->provider(AsyncOp_Cleanup, &state->providerData);

        if (state->timer != nullptr)
        {
            SetThreadpoolTimer(state->timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(state->timer, TRUE);
            CloseThreadpoolTimer(state->timer);
        }

        RemoveAsyncQueueCallbacks(state->providerData.queue, AsyncQueueCallbackType_Work, WorkerCallback, block, [](void* pCxt, void* cCxt)
        {
            return pCxt == cCxt;
        });

        CloseAsyncQueue(state->providerData.queue);
        CloseHandle(state->waitEvent);
        delete state;
    }
}

static AsyncState* GetState(_In_ AsyncBlock* asyncBlock)
{
    AsyncState* state = (AsyncState*)asyncBlock->internalPtr;

    if (state != nullptr && state->signature != ASYNC_STATE_SIG)
    {
        //DebugBreak();
        state = nullptr;
    }

    return state;
}

static void SignalCompletion(
    _In_ AsyncBlock* asyncBlock)
{
    AsyncState* state = GetState(asyncBlock);

    SetEvent(state->waitEvent);

    if (asyncBlock->callback != nullptr)
    {
        SubmitAsyncCallback(
            state->providerData.queue,
            AsyncQueueCallbackType_Completion, 
            asyncBlock, 
            CompletionCallback);
    }
}

static void CALLBACK CompletionCallback(
    _In_ void* context)
{
    AsyncBlock* asyncBlock = (AsyncBlock*)context;

    if (asyncBlock->callback != nullptr)
    {
        asyncBlock->callback(asyncBlock);
    }
}

static void CALLBACK WorkerCallback(
    _In_ void* context)
{
    AsyncBlock* asyncBlock = (AsyncBlock*)context;
    AsyncState* state = GetState(asyncBlock);
    InterlockedExchange(&state->workScheduled, 0);
    HRESULT result = state->provider(AsyncOp_DoWork, &state->providerData);

    // Work routine can return E_PENDING if there is more work to do.  Otherwise
    // it either needs to be a failure or it should have called CompleteAsync, which
    // would have set a new value into the status.

    if (result != E_PENDING)
    {
        if (SUCCEEDED(result))
        {
            result = E_UNEXPECTED;
        }

        HRESULT priorStatus = InterlockedCompareExchange(&asyncBlock->internalStatus, result, E_PENDING);

        if (priorStatus == E_PENDING)
        {
            SignalCompletion(asyncBlock);
        }
    }
}

static void CALLBACK TimerCallback(_In_ PTP_CALLBACK_INSTANCE instance, _In_ PVOID context, _In_ PTP_TIMER timer)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(timer);

    AsyncBlock* async = (AsyncBlock*)context;
    AsyncState* state = GetState(async);
    if (state != nullptr)
    {
        HRESULT hr = SubmitAsyncCallback(
            state->providerData.queue,
            AsyncQueueCallbackType_Work,
            async,
            WorkerCallback);

        if (FAILED(hr))
        {
            CompleteAsync(async, hr, 0);
        }
    }
}

//
// Public APIs
//

/// <summary>
/// Returns the status of the asynchronous operation, optionally waiting
/// for it to complete.  Once complete, you may call GetAsyncResult if
/// the async call has a resulting data payload. If it doesn't, calling
/// GetAsyncResult is unneeded.
/// </summary>
STDAPI GetAsyncStatus(
    _In_ AsyncBlock* asyncBlock,
    _In_ bool wait)
{
    if (asyncBlock == nullptr) return E_POINTER;

    HRESULT result = InterlockedCompareExchange(&asyncBlock->internalStatus, 0xFFFFFFFF, 0xFFFFFFFF);

    if (result == E_PENDING)
    {
        AsyncState* state = GetState(asyncBlock);
        if (state == nullptr) return E_INVALIDARG;

        if (result == E_PENDING && wait)
        {
            // Don't rely on this returning WAIT_OBJECT_0.  If a callback
            // was invoked that read the results before our wait could wake,
            // it's possible the event was closed.  We should still treat this
            // as a status change.
            (void)WaitForSingleObject(state->waitEvent, INFINITE);
            result = GetAsyncStatus(asyncBlock, false);
        }
    }

    return result;
}

/// <summary>
/// Returns the required size of the buffer to pass to GetAsyncResult.
/// </summary>
STDAPI GetAsyncResultSize(
    _In_ AsyncBlock* asyncBlock,
    _Out_ size_t* bufferSize)
{
    if (asyncBlock == nullptr) return E_POINTER;

    HRESULT result = GetAsyncStatus(asyncBlock, false);

    if (SUCCEEDED(result))
    {
        AsyncState* state = GetState(asyncBlock);

        if (state == nullptr)
        {
            result = E_UNEXPECTED;
        }
        else
        {
            *bufferSize = state->providerData.bufferSize;
        }
    }

    return result;
}

/// <summary>
/// Cancels an asynchronous operation. The status result will return E_ABORT,
/// the completion callback will be invoked and the event in the async block will be
/// signaled.
/// </summary>
STDAPI_(void) CancelAsync(
    _In_ AsyncBlock* asyncBlock)
{
    HRESULT status = InterlockedCompareExchange(&asyncBlock->internalStatus, E_ABORT, E_PENDING);

    if (status == E_PENDING)
    {
        AsyncState* state = GetState(asyncBlock);

        if (state->timer != nullptr)
        {
            SetThreadpoolTimer(state->timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(state->timer, TRUE);
        }

        state->provider(AsyncOp_Cancel, &state->providerData);
        SignalCompletion(asyncBlock);
        CleanupState(asyncBlock);
    }
}

/// <summary>
/// Runs the given callback asynchronously.
/// </summary>
STDAPI RunAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ AsyncWork* work)
{
    HRESULT hr = BeginAsync(
        asyncBlock, 
        work, 
        nullptr, 
        nullptr, 
        [](AsyncOp op, AsyncProviderData* data)
    {
        if (op == AsyncOp_DoWork)
        {
            AsyncWork* work = (AsyncWork*)data->context;
            HRESULT hr = work(data->async);
            CompleteAsync(data->async, hr, 0);
        }
        return S_OK;
    });

    if (SUCCEEDED(hr))
    {
        hr = ScheduleAsync(asyncBlock, 0);
    }

    return hr;
}

//
// AsyncProvider APIs
//

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
    _In_ AsyncProvider* provider)
{
    HRESULT hr = AllocState(asyncBlock);

    if (SUCCEEDED(hr))
    {
        AsyncState* state = GetState(asyncBlock);
        state->provider = provider;
        state->providerData.context = context;
        state->token = token;
        state->function = function;
    }

    return hr;
}

/// <summary>
/// Schedules a callback to do async work.  Calling this is optional.  If the async work can be done
/// through a system - async mechanism like overlapped IO or async COM, there is no need to schedule
/// work.  If work should be scheduled after a delay, pass the number of ms ScheduleAsync should wait
/// before it schedules work.
/// </summary>
STDAPI ScheduleAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ uint32_t delay)
{
    if (asyncBlock == nullptr) return E_POINTER;
    AsyncState* state = GetState(asyncBlock);
    if (state == nullptr) return E_INVALIDARG;

    if (delay != 0 && state->timer == nullptr)
    {
        state->timer = CreateThreadpoolTimer(TimerCallback, asyncBlock, nullptr);
        if (state->timer == nullptr)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    DWORD priorScheduled = InterlockedCompareExchange(&state->workScheduled, 1, 0);

    if (priorScheduled == 1)
    {
        return E_NOT_VALID_STATE;
    }

    if (delay == 0)
    {
        return SubmitAsyncCallback(
            state->providerData.queue,
            AsyncQueueCallbackType_Work,
            asyncBlock,
            WorkerCallback);
    }
    else
    {
        INT64 ft = (INT64)delay * (INT64)(-10000);
        SetThreadpoolTimer(state->timer, (PFILETIME)&ft, 0, delay);
        return S_OK;
    }
}

/// <summary>
/// Called when async work is completed and the results can be returned.
/// The caller should supply the resulting data payload size.  If the call
/// has no data payload, pass zero.
/// </summary
STDAPI_(void) CompleteAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ HRESULT result,
    _In_ size_t requiredBufferSize)
{
    AsyncState* state = GetState(asyncBlock);

    // E_PENDING is special -- if you still have work to do don't
    // complete.

    if (result == E_PENDING)
    {
        //DebugBreak();
        return;
    }

    HRESULT priorStatus = InterlockedCompareExchange(&asyncBlock->internalStatus, result, E_PENDING);

    // If prior status was not pending, we either already completed or were canceled.
    if (priorStatus == E_PENDING)
    {
        state->providerData.bufferSize = requiredBufferSize;
        SignalCompletion(asyncBlock);
    }

    // If the required buffer is zero, there is no payload and
    // we can clean up.  Also clean up if the status is abort, as that
    // indicates the user cancelled the call.

    if (requiredBufferSize == 0 || priorStatus == E_ABORT)
    {
        CleanupState(asyncBlock);
    }
}

/// <summary>
/// Returns the result data for the asynchronous operation.  After this call
/// the async block is completed and no longer associated with the 
/// operation.
/// </summary>
STDAPI GetAsyncResult(
    _In_ AsyncBlock* asyncBlock,
    _In_opt_ void* token,
    _In_ size_t bufferSize,
    _Out_writes_bytes_opt_(bufferSize) void* buffer)
{
    if (asyncBlock == nullptr) return E_POINTER;

    HRESULT result = GetAsyncStatus(asyncBlock, false);
    AsyncState* state = GetState(asyncBlock);

    if (SUCCEEDED(result))
    {
        if (state == nullptr)
        {
            result = E_UNEXPECTED;
        }
        else if (token != state->token)
        {
            WCHAR buf[100];
            if (state->function != nullptr)
            {
                swprintf_s(
                    buf,
                    L"Call/Result mismatch.  This AsyncBlock was initiated by '%S'.\r\n",
                    state->function);
            }
            else
            {
                swprintf_s(buf, L"Call/Result mismatch\r\n");
            }

            OutputDebugString(buf);
            //DebugBreak();
            result = E_INVALIDARG;
        }
        else if (state->providerData.bufferSize == 0)
        {
            // Caller has not supplied a payload
            result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        else if (buffer == nullptr)
        {
            return E_POINTER;
        }
        else if (bufferSize < state->providerData.bufferSize)
        {
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            state->providerData.bufferSize = bufferSize;
            state->providerData.buffer = buffer;
            result = state->provider(AsyncOp_GetResult, &state->providerData);
        }
    }

    // Cleanup state if needed
    if (state != nullptr && result != E_PENDING)
    {
        CleanupState(asyncBlock);
    }

    return result;
}
