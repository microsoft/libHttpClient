// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "Async.h"
#include "AsyncProvider.h"
#include "AsyncQueue.h"

#define ASYNC_STATE_SIG 0x41535445

#if !HC_PLATFORM_IS_MICROSOFT
using PTP_TIMER = void*;
#endif

// Used by unit tests to verify we cleanup memory correctly.
std::atomic<uint32_t> s_AsyncLibGlobalStateCount{ 0 };

struct AsyncState
{
    uint32_t signature = ASYNC_STATE_SIG;
    std::atomic<uint32_t> refs{ 1 };
    std::atomic<bool> workScheduled{ false };
    std::atomic<bool> timerScheduled{ false };
    bool canceled = false;
    AsyncProvider* provider = nullptr;
    AsyncProviderData providerData;
    HANDLE waitEvent = nullptr;
    PTP_TIMER timer = nullptr;
    const void* token = nullptr;
    const char* function = nullptr;

    AsyncState() noexcept
    {
        ++s_AsyncLibGlobalStateCount;
    }

    void AddRef() noexcept
    {
        ++refs;
    }

    void Release() noexcept
    {
        if (--refs == 0)
        {
            delete this;
        }
    }

private:
    ~AsyncState() noexcept
    {
        if (timer != nullptr)
        {
#ifdef _WIN32
            SetThreadpoolTimer(timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(timer, TRUE);
            CloseThreadpoolTimer(timer);
#endif
        }

        if (providerData.queue != nullptr)
        {
            CloseAsyncQueue(providerData.queue);
        }

        if (waitEvent != nullptr)
        {
#ifdef _WIN32
            CloseHandle(waitEvent);
#endif
        }

        --s_AsyncLibGlobalStateCount;
    }
};

struct AsyncBlockInternal
{
    std::atomic<AsyncState*> state;
    std::atomic<HRESULT> status;

    AsyncBlockInternal(AsyncState* s) :
        state(s), status(E_PENDING)
    {}
};
static_assert(sizeof(AsyncBlockInternal) <= 2 * sizeof(void*), "Unexpected size for AsyncBlockInternal");
static_assert(std::alignment_of<AsyncBlockInternal>::value == std::alignment_of<void*>::value,
    "Unexpected alignment for AsyncBlockInternal");
static_assert(std::is_trivially_destructible<AsyncBlockInternal>::value,
    "Unexpected nontrivial destructor for AsyncBlockInternal");

class AsyncStateRef
{
public:
    AsyncStateRef(_In_ AsyncState* state) noexcept
        : m_state(state)
    {
        if (m_state)
        {
            m_state->AddRef();
        }
    }
    ~AsyncStateRef() noexcept
    {
        if (m_state)
        {
            m_state->Release();
        }
    }
    AsyncState* operator->() const noexcept
    {
        return m_state;
    }
    bool operator==(nullptr_t) const noexcept
    {
        return m_state == nullptr;
    }
    AsyncState* Detach() noexcept
    {
        AsyncState* p = m_state;
        m_state = nullptr;
        return p;
    }
private:
    AsyncState * m_state;
};

static void CALLBACK CompletionCallback(_In_ void* context);
static void CALLBACK WorkerCallback(_In_ void* context);
#ifdef _WIN32
static void CALLBACK TimerCallback(_In_ PTP_CALLBACK_INSTANCE, _In_ void* context, _In_ PTP_TIMER);
#endif
static void SignalCompletion(_In_ AsyncBlock* asyncBlock);
static HRESULT AllocStateNoCompletion(_In_ AsyncBlock* asyncBlock);
static HRESULT AllocState(_In_ AsyncBlock* asyncBlock);
static void CleanupState(_In_ AsyncBlock* asyncBlock);
static AsyncState* GetState(_In_ AsyncBlock* asyncBlock);
static AsyncBlockInternal* GetInternalBlock(_In_ AsyncBlock* asyncBlock);

static HRESULT AllocStateNoCompletion(_In_ AsyncBlock* asyncBlock)
{
    AsyncStateRef state(new (std::nothrow) AsyncState);
    RETURN_IF_NULL_ALLOC(state);

    if (asyncBlock->waitEvent != nullptr)
    {
#if _WIN32
        RETURN_IF_WIN32_BOOL_FALSE(DuplicateHandle(
            GetCurrentProcess(),
            asyncBlock->waitEvent,
            GetCurrentProcess(),
            &state->waitEvent, 0,
            FALSE,
            DUPLICATE_SAME_ACCESS));
#else
        assert(false);
#endif
    }

    state->providerData.queue = asyncBlock->queue;
    state->providerData.async = asyncBlock;

    if (state->providerData.queue != nullptr)
    {
        ReferenceAsyncQueue(state->providerData.queue);
    }
    else
    {
#if _WIN32
        RETURN_IF_FAILED(
            CreateSharedAsyncQueue(
            (uint32_t)GetCurrentThreadId(),
                AsyncQueueDispatchMode_ThreadPool,
                AsyncQueueDispatchMode_FixedThread,
                &state->providerData.queue));
#else
        RETURN_HR(E_INVALIDARG);
#endif
    }

    new (asyncBlock->internal) AsyncBlockInternal{ state.Detach() };

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
#if _WIN32
            SetEvent(asyncBlock->waitEvent);
            hr = S_OK;
#else
            assert(false);
#endif
        }

        if (asyncBlock->callback != nullptr)
        {
            asyncBlock->callback(asyncBlock);
            hr = S_OK;
        }
    }

    RETURN_HR(hr);
}

static void CleanupState(_In_ AsyncBlock* asyncBlock)
{
    AsyncState* state = nullptr;
    GetInternalBlock(asyncBlock)->state.exchange(state);

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

    GetInternalBlock(asyncBlock)->~AsyncBlockInternal();
}

static AsyncState* GetState(_In_ AsyncBlock* asyncBlock)
{
    AsyncState* state = GetInternalBlock(asyncBlock)->state;

    if (state != nullptr && state->signature != ASYNC_STATE_SIG)
    {
        ASSERT(false);
        state = nullptr;
    }

    return state;
}

static AsyncBlockInternal* GetInternalBlock(_In_ AsyncBlock* asyncBlock)
{
    assert(asyncBlock);
    return reinterpret_cast<AsyncBlockInternal*>(asyncBlock->internal);
}

static void SignalCompletion(
    _In_ AsyncBlock* asyncBlock)
{
    AsyncState* state = GetState(asyncBlock);
    if (state->waitEvent)
    {
#if _WIN32
        SetEvent(state->waitEvent);
#else
        assert(false);
#endif
    }

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
    AsyncBlock* asyncBlock = state->providerData.async;
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
                result = E_UNEXPECTED;
            }

            HRESULT previous = E_PENDING;
            bool swapped = GetInternalBlock(asyncBlock)->status.compare_exchange_strong(previous, result);
            if (swapped)
            {
                SignalCompletion(asyncBlock);
            }
        }
    }

    state->Release();
}

#if _WIN32
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
#endif

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
    HRESULT result = GetInternalBlock(asyncBlock)->status;

    if (result == E_PENDING)
    {
        AsyncState* state = GetState(asyncBlock);
        RETURN_HR_IF(E_INVALIDARG, state == nullptr);

        if (result == E_PENDING && wait)
        {
#if _WIN32
            // Don't rely on this returning WAIT_OBJECT_0.  If a callback
            // was invoked that read the results before our wait could wake,
            // it's possible the event was closed.  We should still treat this
            // as a status change.
            (void)WaitForSingleObject(state->waitEvent, INFINITE);
            result = GetAsyncStatus(asyncBlock, false);
#else
            assert(false);
            RETURN_HR(E_INVALIDARG);
#endif
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
    HRESULT previous = E_PENDING;
    bool swapped = GetInternalBlock(asyncBlock)->status.compare_exchange_strong(previous, E_ABORT);

    if (swapped)
    {
        AsyncState* state = GetState(asyncBlock);
        state->canceled = true;

        if (state->timer != nullptr)
        {
#if _WIN32
            SetThreadpoolTimer(state->timer, nullptr, 0, 0);
            WaitForThreadpoolTimerCallbacks(state->timer, TRUE);

            if (state->timerScheduled)
            {
                // The timer callback was never invoked so release this reference
                state->Release();
            }
#else
            assert(false);
#endif
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
        reinterpret_cast<void*>(work),
        reinterpret_cast<void*>(RunAsync),
        __FUNCTION__,
        [](AsyncOp op, const AsyncProviderData* data)
    {
        if (op == AsyncOp_DoWork)
        {
            AsyncWork* work = reinterpret_cast<AsyncWork*>(data->context);
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
#if _WIN32
        state->timer = CreateThreadpoolTimer(TimerCallback, state, nullptr);
        RETURN_LAST_ERROR_IF_NULL(state->timer);
#else
        assert(false);
        RETURN_HR(E_INVALIDARG);
#endif
    }

    bool priorScheduled = false;
    state->workScheduled.compare_exchange_strong(priorScheduled, true);

    if (priorScheduled)
    {
        RETURN_HR(E_UNEXPECTED);
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
#ifdef _WIN32
        int64_t ft = (int64_t)delayInMs * (int64_t)(-10000);
        state->timerScheduled = true;
        SetThreadpoolTimer(state->timer, (PFILETIME)&ft, 0, delayInMs);
#else
        RETURN_HR(E_INVALIDARG);
#endif
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

    HRESULT priorStatus = E_PENDING;
    bool swapped = GetInternalBlock(asyncBlock)->status.compare_exchange_strong(priorStatus, result);

    // If prior status was not pending, we either already completed or were canceled.
    if (swapped)
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
            // Call/Result mismatch.  This AsyncBlock was initiated by state->function
            ASSERT(false);
            result = E_INVALIDARG;
        }
        else if (state->providerData.bufferSize == 0)
        {
            // Caller has not supplied a payload
            result = E_NOT_SUPPORTED;
        }
        else if (buffer == nullptr)
        {
            return E_INVALIDARG;
        }
        else if (bufferSize < state->providerData.bufferSize)
        {
            return E_INSUFFICIENT_BUFFER;
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