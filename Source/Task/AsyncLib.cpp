// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "Async.h"
#include "AsyncProvider.h"
#include "AsyncQueue.h"

#define ASYNC_STATE_SIG 0x41535445

// Used by unit tests to verify we cleanup memory correctly.
DWORD s_AsyncLibGlobalStateCount = 0;

struct AsyncState
{
    uint32_t signature = ASYNC_STATE_SIG;
    std::atomic<uint32_t> refs = 1;
    std::atomic_bool workScheduled = false;
    std::atomic_bool timerScheduled = false;
    bool canceled = false;
    AsyncProvider* provider = nullptr;
    AsyncProviderData providerData;
    HANDLE waitEvent = nullptr;
    PTP_TIMER timer = nullptr;
    const void* token = nullptr;
    const char* function = nullptr;

    AsyncState()
    {
        InterlockedIncrement(&s_AsyncLibGlobalStateCount);
    }

    ~AsyncState()
    {
        if (timer != nullptr)
        {
            SetThreadpoolTimer(timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(timer, TRUE);
            CloseThreadpoolTimer(timer);
        }

        if (providerData.queue != nullptr)
        {
            CloseAsyncQueue(providerData.queue);
        }

        if (waitEvent != nullptr)
        {
            CloseHandle(waitEvent);
        }

        InterlockedDecrement(&s_AsyncLibGlobalStateCount);
    }

    void AddRef()
    {
        refs++;
    }

    void Release()
    {
        if (refs.fetch_sub(1) == 1)
        {
            delete this;
        }
    }
};

class AsyncStateRef
{
public:
    AsyncStateRef(_In_ AsyncState* state)
        : m_state(state)
    {
        m_state->AddRef();
    }
    ~AsyncStateRef()
    {
        m_state->Release();
    }
private:
    AsyncState* m_state;
};

static void CALLBACK CompletionCallback(_In_ void* context);
static void CALLBACK WorkerCallback(_In_ void* context);
static void CALLBACK TimerCallback(_In_ PTP_CALLBACK_INSTANCE, _In_ void* context, _In_ PTP_TIMER);
static void SignalCompletion(_In_ AsyncBlock* asyncBlock);
static HRESULT AllocStateNoCompletion(_In_ AsyncBlock* asyncBlock);
static HRESULT AllocState(_In_ AsyncBlock* asyncBlock);
static void CleanupState(_In_ AsyncBlock* asyncBlock);
static AsyncState* GetState(_In_ AsyncBlock* asyncBlock);

static HRESULT AllocStateNoCompletion(_In_ AsyncBlock* asyncBlock)
{
    std::unique_ptr<AsyncState> state(new (std::nothrow) AsyncState);
    RETURN_IF_NULL_ALLOC(state);

    if (asyncBlock->waitEvent != nullptr)
    {
        RETURN_IF_WIN32_BOOL_FALSE(DuplicateHandle(
            GetCurrentProcess(), 
            asyncBlock->waitEvent, 
            GetCurrentProcess(), 
            &state->waitEvent, 0, 
            FALSE, 
            DUPLICATE_SAME_ACCESS));
    }
    else
    {
        state->waitEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        RETURN_LAST_ERROR_IF_NULL(state->waitEvent);
    }

    state->providerData.queue = asyncBlock->queue;
    state->providerData.async = asyncBlock;

    if (state->providerData.queue != nullptr)
    {
        ReferenceAsyncQueue(state->providerData.queue);
    }
    else
    {
        RETURN_IF_FAILED(
            CreateSharedAsyncQueue(
                (uint32_t)GetCurrentThreadId(),
                AsyncQueueDispatchMode_ThreadPool,
                AsyncQueueDispatchMode_FixedThread,
                &state->providerData.queue));
    }

    asyncBlock->internalPtr = state.release();
    asyncBlock->internalStatus = E_PENDING;

    return S_OK;
}

static HRESULT AllocState(_In_ AsyncBlock* asyncBlock)
{
    // If the async block is already associated with another
    // call, fail.

    RETURN_HR_IF(E_INVALIDARG, GetState(asyncBlock) != nullptr);
    
    HRESULT hr = AllocStateNoCompletion(asyncBlock);

    if (FAILED(hr))
    {
        // Attempt to complete the call as a failure, and only return
        // a failed HR here if we couldn't complete.

        if (asyncBlock->waitEvent != nullptr)
        {
            SetEvent(asyncBlock->waitEvent);
            hr = S_OK;
        }

        if (asyncBlock->callback != nullptr)
        {
            asyncBlock->callback(asyncBlock);
            hr = S_OK;
        }
    }

    RETURN_HR(hr);
}

static void CleanupState(_In_ AsyncBlock* block)
{
    AsyncState* state = (AsyncState*)InterlockedExchangePointer(&block->internalPtr, nullptr);

    if (state != nullptr)
    {
        (void)state->provider(AsyncOp_Cleanup, &state->providerData);

        auto removePredicate = [](void* pCxt, void* cCxt)
        {
            if (pCxt == cCxt)
            {
                AsyncState* state = static_cast<AsyncState*>(pCxt);
                state->Release();
                return true;
            }
            return false;
        };

        RemoveAsyncQueueCallbacks(state->providerData.queue, AsyncQueueCallbackType_Work, WorkerCallback, state, removePredicate);

        state->Release();
    }
}

static AsyncState* GetState(_In_ AsyncBlock* asyncBlock)
{
    AsyncState* state = (AsyncState*)InterlockedCompareExchangePointer(&asyncBlock->internalPtr, nullptr, nullptr);

    if (state != nullptr && state->signature != ASYNC_STATE_SIG)
    {
        DebugBreak();
        state = nullptr;
    }

    return state;
}

static void SignalCompletion(
    _In_ AsyncBlock* asyncBlock)
{
    AsyncState* state = GetState(asyncBlock);
    SetEvent(state->waitEvent);

    if (state->providerData.async->callback != nullptr)
    {
        (void)SubmitAsyncCallback(
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
    AsyncState* state = static_cast<AsyncState*>(context);
    AsyncBlock* async = state->providerData.async;
    state->workScheduled = false;

    if (!state->canceled)
    {
        HRESULT result = state->provider(AsyncOp_DoWork, &state->providerData);

        // Work routine can return E_PENDING if there is more work to do.  Otherwise
        // it either needs to be a failure or it should have called CompleteAsync, which
        // would have set a new value into the status.

        if (result != E_PENDING && !state->canceled)
        {
            if (SUCCEEDED(result))
            {
                result = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
            }

            HRESULT priorStatus = InterlockedCompareExchange(&async->internalStatus, result, E_PENDING);

            if (priorStatus == E_PENDING)
            {
                SignalCompletion(async);
            }
        }
    }

    state->Release();
}

static void CALLBACK TimerCallback(_In_ PTP_CALLBACK_INSTANCE, _In_ void* context, _In_ PTP_TIMER)
{
    AsyncState* state = static_cast<AsyncState*>(context);
    state->timerScheduled = false;

    if (!state->canceled)
    {
        HRESULT hr = SubmitAsyncCallback(
            state->providerData.queue,
            AsyncQueueCallbackType_Work,
            state,
            WorkerCallback);

        if (FAILED(hr))
        {
            state->Release();
            CompleteAsync(state->providerData.async, hr, 0);
        }
    }
    else
    {
        state->Release();
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
    HRESULT result = InterlockedCompareExchange(&asyncBlock->internalStatus, 0xFFFFFFFF, 0xFFFFFFFF);

    if (result == E_PENDING)
    {
        AsyncState* state = GetState(asyncBlock);
        RETURN_HR_IF(E_INVALIDARG, state == nullptr);

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
    HRESULT result = GetAsyncStatus(asyncBlock, false);

    if (SUCCEEDED(result))
    {
        const AsyncState* state = GetState(asyncBlock);

        if (state == nullptr)
        {
            result = E_INVALIDARG;
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
        state->canceled = true;

        if (state->timer != nullptr)
        {
            SetThreadpoolTimer(state->timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(state->timer, TRUE);

            if (state->timerScheduled)
            {
                // The timer callback was never invoked so release this reference
                state->Release();
            }
        }

        (void)state->provider(AsyncOp_Cancel, &state->providerData);
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
    RETURN_IF_FAILED(BeginAsync(
        asyncBlock, 
        work, 
        RunAsync, 
        __FUNCTION__, 
        [](AsyncOp op, const AsyncProviderData* data)
    {
        if (op == AsyncOp_DoWork)
        {
            AsyncWork* work = (AsyncWork*)data->context;
            HRESULT hr = work(data->async);
            CompleteAsync(data->async, hr, 0);
        }
        return S_OK;
    }));

    RETURN_HR(ScheduleAsync(asyncBlock, 0));
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
    _In_opt_ const void* token,
    _In_opt_ const char* function,
    _In_ AsyncProvider* provider)
{
    RETURN_IF_FAILED(AllocState(asyncBlock));

    AsyncState* state = GetState(asyncBlock);
    state->provider = provider;
    state->providerData.context = context;
    state->token = token;
    state->function = function;

    return S_OK;
}

/// <summary>
/// Schedules a callback to do async work.  Calling this is optional.  If the async work can be done
/// through a system - async mechanism like overlapped IO or async COM, there is no need to schedule
/// work.  If work should be scheduled after a delay, pass the number of ms ScheduleAsync should wait
/// before it schedules work.
/// </summary>
STDAPI ScheduleAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ uint32_t delayInMs)
{
    AsyncState* state = GetState(asyncBlock);
    RETURN_HR_IF(E_INVALIDARG, state == nullptr);

    AsyncStateRef ref(state);

    if (delayInMs != 0 && state->timer == nullptr)
    {
        state->timer = CreateThreadpoolTimer(TimerCallback, state, nullptr);
        RETURN_LAST_ERROR_IF_NULL(state->timer);
    }

    bool priorScheduled = false;
    
    state->workScheduled.compare_exchange_strong(priorScheduled, true);

    if (priorScheduled)
    {
        RETURN_HR(HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
    }

    if (delayInMs == 0)
    {
        RETURN_IF_FAILED(SubmitAsyncCallback(
            state->providerData.queue,
            AsyncQueueCallbackType_Work,
            state,
            WorkerCallback));
    }
    else
    {
        int64_t ft = (int64_t)delayInMs * (int64_t)(-10000);
        state->timerScheduled = true;
        SetThreadpoolTimer(state->timer, (PFILETIME)&ft, 0, delayInMs);
    }

    // State object is now referenced by the work or timer callback
    state->AddRef();
    return S_OK;
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
    // E_PENDING is special -- if you still have work to do don't
    // complete.

    if (result == E_PENDING)
    {
        return;
    }

    HRESULT priorStatus = InterlockedCompareExchange(&asyncBlock->internalStatus, result, E_PENDING);

    // If prior status was not pending, we either already completed or were canceled.
    if (priorStatus == E_PENDING)
    {
        AsyncState* state = GetState(asyncBlock);
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
    _In_opt_ const void* token,
    _In_ size_t bufferSize,
    _Out_writes_bytes_to_opt_(bufferSize, *bufferUsed) void* buffer,
    _Out_opt_ size_t* bufferUsed)
{
    HRESULT result = GetAsyncStatus(asyncBlock, false);
    AsyncState* state = GetState(asyncBlock);

    if (SUCCEEDED(result))
    {
        if (state == nullptr)
        {
            result = E_INVALIDARG;
        }
        else if (token != state->token)
        {
            char buf[100];
            if (state->function != nullptr)
            {
                sprintf_s(
                    buf,
                    "Call/Result mismatch.  This AsyncBlock was initiated by '%s'.\r\n",
                    state->function);
            }
            else
            {
                sprintf_s(buf, "Call/Result mismatch\r\n");
            }

            OutputDebugStringA(buf);
            DebugBreak();
            result = E_INVALIDARG;
        }
        else if (state->providerData.bufferSize == 0)
        {
            // Caller has not supplied a payload
            result = HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        else if (buffer == nullptr)
        {
            return E_INVALIDARG;
        }
        else if (bufferSize < state->providerData.bufferSize)
        {
            return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        }
        else
        {
            if (bufferUsed != nullptr)
            {
                *bufferUsed = state->providerData.bufferSize;
            }

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
