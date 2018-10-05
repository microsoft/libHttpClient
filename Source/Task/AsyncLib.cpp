// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#define ASYNC_STATE_SIG 0x41535445

// Used by unit tests to verify we cleanup memory correctly.
std::atomic<uint32_t> s_AsyncLibGlobalStateCount{ 0 };

// Note that there are two AsyncBlock structures in play.
// There is the pointer allocated and passed to us by the user
// and there is our own local copy.  We pass the local copy to
// async provider callbacks, and pass the user pointer to all
// user-visible callbacks.  The AsyncBlockInternalGuard class keeps
// the state of these two classes in sync under a lock.  We
// do this because it is possible for a user to cancel an async
// call while that call is still running.  We issue the completion
// callback for the cancel immediatly, which gives the user an
// opportunity to delete the user async block.  This would leave
// the async provider callback with a dangling pointer.

struct AsyncState
{
    uint32_t signature = ASYNC_STATE_SIG;
    std::atomic<uint32_t> refs{ 1 };
    std::atomic<bool> workScheduled{ false };
    bool canceled = false;
    AsyncProvider* provider = nullptr;
    AsyncProviderData providerData{ };
    AsyncBlock asyncBlock { };
    AsyncBlock* userAsyncBlock = nullptr;
    async_queue_handle_t queue = nullptr;

#ifdef _WIN32
    HANDLE waitEvent = nullptr;
#else
    std::mutex waitMutex;
    std::condition_variable waitCondition;
    bool waitSatisfied;
#endif

    const void* identity = nullptr;
    const char* identityName = nullptr;

    void* operator new(size_t size, size_t additional)
    {
        return ::operator new(size + additional);
    }

    void operator delete(void* ptr)
    {
        ::operator delete(ptr);
    }

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
        if (provider != nullptr)
        {
            (void)provider(AsyncOp_Cleanup, &providerData);
        }

        if (queue != nullptr)
        {
            CloseAsyncQueue(queue);
        }

#ifdef _WIN32
        if (waitEvent != nullptr)
        {
            CloseHandle(waitEvent);
        }
#endif

        --s_AsyncLibGlobalStateCount;
    }
};

struct AsyncBlockInternal
{
    AsyncState* state = nullptr;
    HRESULT status = E_PENDING;
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
};
static_assert(sizeof(AsyncBlockInternal) <= sizeof(AsyncBlock::internal),
    "Unexpected size for AsyncBlockInternal");
static_assert(std::alignment_of<AsyncBlockInternal>::value == std::alignment_of<void*>::value,
    "Unexpected alignment for AsyncBlockInternal");
static_assert(std::is_trivially_destructible<AsyncBlockInternal>::value,
    "Unexpected nontrivial destructor for AsyncBlockInternal");

class AsyncStateRef
{
public:
    AsyncStateRef() noexcept
        : m_state{ nullptr }
    {}
    explicit AsyncStateRef(AsyncState* state) noexcept
        : m_state{ state }
    {
        if (m_state)
        {
            m_state->AddRef();
        }
    }
    AsyncStateRef(const AsyncStateRef&) = delete;
    AsyncStateRef(AsyncStateRef&& other) noexcept
        : m_state{ other.m_state }
    {
        other.m_state = nullptr;
    }
    AsyncStateRef& operator=(const AsyncStateRef&) = delete;
    AsyncStateRef& operator=(AsyncStateRef&& other) noexcept
    {
        if (&other != this) { std::swap(m_state, other.m_state); }
        return *this;
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
    bool operator!=(nullptr_t) const noexcept
    {
        return m_state != nullptr;
    }
    void Attach(AsyncState* state) noexcept
    {
        ASSERT(!m_state);
        m_state = state;
    }
    AsyncState* Detach() noexcept
    {
        AsyncState* p = m_state;
        m_state = nullptr;
        return p;
    }

    AsyncState* Get() const { return m_state; }

private:
    AsyncState* m_state;
};

class AsyncBlockInternalGuard
{
public:
    AsyncBlockInternalGuard(_Inout_ AsyncBlock* asyncBlock) noexcept :
        m_internal1{ reinterpret_cast<AsyncBlockInternal*>(asyncBlock->internal) },
        m_internal2(nullptr)
    {
        ASSERT(m_internal1);
        while (m_internal1->lock.test_and_set()) {}
        
        if (m_internal1->state != nullptr)
        {
            // Which async block go we grab?  Either our copy of the user
            // pointer could have been passed in. We want to get the internal
            // state of both blocks.
            
            AsyncBlock* asyncBlock2 = (asyncBlock == m_internal1->state->userAsyncBlock) ?
                &m_internal1->state->asyncBlock :
                m_internal1->state->userAsyncBlock;

            ASSERT(asyncBlock2 != asyncBlock);
            
            m_internal2 = reinterpret_cast<AsyncBlockInternal*>(asyncBlock2->internal);
            ASSERT(m_internal2);
            while (m_internal2->lock.test_and_set()) {}
        }
    }

    ~AsyncBlockInternalGuard() noexcept
    {
        m_internal1->lock.clear();
        if (m_internal2 != nullptr)
        {
            m_internal2->lock.clear();
        }
    }
    
    AsyncStateRef GetState() const noexcept
    {
        AsyncStateRef state{ m_internal1->state };

        if (state != nullptr && state->signature != ASYNC_STATE_SIG)
        {
            ASSERT(false);
            return AsyncStateRef{};
        }

        return state;
    }

    AsyncStateRef ExtractState() const noexcept
    {
        AsyncStateRef state{ m_internal1->state };
        m_internal1->state = nullptr;
        
        if (m_internal2 != nullptr)
        {
            m_internal2->state = nullptr;
        }

        if (state != nullptr && state->signature != ASYNC_STATE_SIG)
        {
            ASSERT(false);
            return AsyncStateRef{};
        }

        return state;
    }

    HRESULT GetStatus() const noexcept
    {
        return m_internal1->status;
    }

    bool TrySetTerminalStatus(HRESULT status) noexcept
    {
        if (m_internal1->status == E_PENDING)
        {
            m_internal1->status = status;
            if (m_internal2 != nullptr)
            {
                ASSERT(m_internal2->status == E_PENDING);
                m_internal2->status = status;
            }
            return true;
        }
        else
        {
            return false;
        }
    }

private:
    AsyncBlockInternal * const m_internal1;
    AsyncBlockInternal * m_internal2;
};

static void CALLBACK CompletionCallbackForAsyncState(_In_ void* context);
static void CALLBACK CompletionCallbackForAsyncBlock(_In_ void* context);
static void CALLBACK WorkerCallback(_In_ void* context);
static void SignalCompletion(_In_ AsyncStateRef const& state);
static void SignalWait(_In_ AsyncStateRef const& state);
static HRESULT AllocStateNoCompletion(_Inout_ AsyncBlock* asyncBlock, _Inout_ AsyncBlockInternal* internal, _In_ size_t contextSize);
static HRESULT AllocState(_Inout_ AsyncBlock* asyncBlock, _In_ size_t contextSize);
static void CleanupState(_Inout_ AsyncStateRef&& state);

static HRESULT AllocStateNoCompletion(_Inout_ AsyncBlock* asyncBlock, _Inout_ AsyncBlockInternal* internal, _In_ size_t contextSize)
{
    AsyncStateRef state;
    state.Attach(new (contextSize) AsyncState);
    RETURN_IF_NULL_ALLOC(state);

    if (contextSize != 0)
    {
        // User allocated additional context data.  This was allocated as extra bytes at the end of 
        // async state.
        state->providerData.context = (state.Get() + 1);
    }
    
#if _WIN32
    state->waitEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    RETURN_LAST_ERROR_IF_NULL(state->waitEvent);
#endif

    RETURN_IF_FAILED(DuplicateAsyncQueueHandle(asyncBlock->queue, &state->queue));

    state->userAsyncBlock = asyncBlock;
    state->providerData.async = &state->asyncBlock;
    
    internal->state = state.Detach();

    // Duplicate the async block we've just configured
    internal->state->asyncBlock = *asyncBlock;

    return S_OK;
}

static HRESULT AllocState(_Inout_ AsyncBlock* asyncBlock, _In_ size_t contextSize)
{
    // If the async block is already associated with another
    // call, fail.

    // There is no great way to tell if the AsyncBlockInternal has already been
    // initialized, because uninitialized memory can look like anything.
    // Here we rely on the client zeoring out the entirety of the AsyncBlock
    // object so we can check that the state pointer of the internal data is zero.
    auto internal = reinterpret_cast<AsyncBlockInternal*>(asyncBlock->internal);
    if (internal->state != nullptr)
    {
        return E_INVALIDARG;
    }
    
    if (asyncBlock->queue == nullptr)
    {
        return E_INVALIDARG;
    }
    
    // This could be a reused async block from a prior
    // call, so zero everything.
    for (auto i = 0u; i < sizeof(asyncBlock->internal); ++i)
    {
        asyncBlock->internal[i] = 0;
    }

    // Construction is inherently single threaded
    // (there is nothing we can do if the client tries to use the same
    // AsyncBlock in 2 calls at the same time)
    internal = new (asyncBlock->internal) AsyncBlockInternal{};

    HRESULT hr = AllocStateNoCompletion(asyncBlock, internal, contextSize);

    if (FAILED(hr) && contextSize == 0)
    {
        // Attempt to complete the call as a failure, and only return
        // a failed HR here if we couldn't complete.  We don't do this
        // if the user asked to allocate additional context, because
        // it is supplied back to the user as an out parameter.

        internal->status = hr;

        if (asyncBlock->queue != nullptr && asyncBlock->callback != nullptr)
        {
            hr = SubmitAsyncCallback(
                asyncBlock->queue,
                AsyncQueueCallbackType_Completion,
                0,
                asyncBlock,
                CompletionCallbackForAsyncBlock);
        }
    }

    return hr;
}

static void CleanupState(_Inout_ AsyncStateRef&& state)
{

    if (state != nullptr)
    {
        // Should only cleanup state after calling ExtractState to clear it.
        ASSERT((reinterpret_cast<AsyncBlockInternal*>(state->providerData.async->internal))->state == nullptr);

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

        RemoveAsyncQueueCallbacks(state->queue, AsyncQueueCallbackType_Work, WorkerCallback, state.Get(), removePredicate);
        state->Release();
    }
}

static void SignalCompletion(_In_ AsyncStateRef const& state)
{
    if (state->providerData.async->callback != nullptr)
    {
        AsyncStateRef callbackState(state.Get());
        HRESULT hr = SubmitAsyncCallback(
            state->queue,
            AsyncQueueCallbackType_Completion,
            0,
            callbackState.Get(),
            CompletionCallbackForAsyncState);

        if (SUCCEEDED(hr))
        {
            callbackState.Detach();
        }
        else
        {
            FAIL_FAST_MSG("Failed to submit competion callback: 0x%08x", hr);
        }
    }
    else
    {
        SignalWait(state);
    }
}

static void SignalWait(_In_ AsyncStateRef const& state)
{
#if _WIN32
    if (state->waitEvent)
    {
        SetEvent(state->waitEvent);
    }
#else
    {
        std::lock_guard<std::mutex> lock(state->waitMutex);
        state->waitSatisfied = true;
    }
    state->waitCondition.notify_all();
#endif
}

static void CALLBACK CompletionCallbackForAsyncBlock(
    _In_ void* context)
{
    AsyncBlock* asyncBlock = static_cast<AsyncBlock*>(context);
    if (asyncBlock->callback != nullptr)
    {
        asyncBlock->callback(asyncBlock);
    }
}

static void CALLBACK CompletionCallbackForAsyncState(
    _In_ void* context)
{
    AsyncStateRef state;
    state.Attach(static_cast<AsyncState*>(context));

    AsyncBlock* asyncBlock = state->userAsyncBlock;
    if (asyncBlock->callback != nullptr)
    {
        asyncBlock->callback(asyncBlock);
    }

    SignalWait(state);
}

static void CALLBACK WorkerCallback(
    _In_ void* context)
{
    AsyncStateRef state;
    state.Attach(static_cast<AsyncState*>(context));
    state->workScheduled = false;

    if (state->canceled)
    {
        return;
    }

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

        bool completedNow = false;

        {
            AsyncBlockInternalGuard internal{ &state->asyncBlock };
            completedNow = internal.TrySetTerminalStatus(result);
        }

        if (completedNow)
        {
            SignalCompletion(state);
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
    _Inout_ AsyncBlock* asyncBlock,
    _In_ bool wait)
{
    HRESULT result = E_PENDING;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        result = internal.GetStatus();
        state = internal.GetState();
    }

    // If we are being asked to wait, always check the wait state before
    // looking at the hresult.  Our wait waits until the completion runs
    // so we may need to wait past when the status is set.

    if (wait)
    {
        if (state == nullptr)
        {
            ASSERT(result != E_PENDING);
            RETURN_HR_IF(E_INVALIDARG, result == E_PENDING);
        }
        else
        {
#if _WIN32
            DWORD waitResult;
            do
            {
                waitResult = WaitForSingleObjectEx(state->waitEvent, INFINITE, TRUE);
            } while (waitResult == WAIT_IO_COMPLETION);

            if (waitResult == WAIT_OBJECT_0)
            {
                result = GetAsyncStatus(asyncBlock, false);
            }
            else
            {
                result = HRESULT_FROM_WIN32(GetLastError());
            }
#else
            {
                std::unique_lock<std::mutex> lock(state->waitMutex);

                if (!state->waitSatisfied)
                {
                    AsyncState* s = state.Get();
                    state->waitCondition.wait(lock, [s] { return s->waitSatisfied; });
                }
            }

            result = GetAsyncStatus(asyncBlock, false);
#endif
        }
    }

    return result;
}

/// <summary>
/// Returns the required size of the buffer to pass to GetAsyncResult.
/// </summary>
STDAPI GetAsyncResultSize(
    _Inout_ AsyncBlock* asyncBlock,
    _Out_ size_t* bufferSize)
{
    HRESULT result = E_PENDING;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        result = internal.GetStatus();
        state = internal.GetState();
    }

    if (SUCCEEDED(result))
    {
        *bufferSize = state == nullptr ? 0 : state->providerData.bufferSize;
    }

    return result;
}

/// <summary>
/// Cancels an asynchronous operation. The status result will return E_ABORT,
/// the completion callback will be invoked and the event in the async block will be
/// signaled.
/// </summary>
STDAPI_(void) CancelAsync(
    _Inout_ AsyncBlock* asyncBlock)
{
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        if (!internal.TrySetTerminalStatus(E_ABORT))
        {
            return;
        }
        state = internal.ExtractState();
        state->canceled = true;
    }

    (void)state->provider(AsyncOp_Cancel, &state->providerData);
    SignalCompletion(state);

    // At this point asyncBlock may be unsafe to touch.

    CleanupState(std::move(state));
}

/// <summary>
/// Runs the given callback asynchronously.
/// </summary>
STDAPI RunAsync(
    _Inout_ AsyncBlock* asyncBlock,
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
    _Inout_ AsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_opt_ const void* identity,
    _In_opt_ const char* identityName,
    _In_ AsyncProvider* provider)
{
    RETURN_IF_FAILED(AllocState(asyncBlock, 0));

    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        state = internal.GetState();
    }

    // AllocState can fail, but if it can route
    // its failure to a completion callback it will
    // and will still return success.  
    if (state != nullptr)
    {
        state->provider = provider;
        state->providerData.context = context;
        state->identity = identity;
        state->identityName = identityName;
    }

    return S_OK;
}

/// <summary>
/// Initializes an async block for use.  Once begun calls such
/// as GetAsyncStatus will provide meaningful data. It is assumed the
/// async work will begin on some system defined thread after this call
/// returns. The token and function parameters can be used to help identify
/// mismatched Begin/GetResult calls.  The token is typically the function
/// pointer of the async API you are implementing, and the functionName parameter
/// is typically the __FUNCTION__ compiler macro.  
///
/// This variant of BeginAsync will allocate additional memory of size contextSize
/// and use this as the context pointer for async provider callbacks.  The memory
/// pointer is returned in 'context'.  The lifetime of this memory is managed
/// by the async library and will be freed automatically when the call 
/// completes.
/// </summary>
STDAPI BeginAsyncAlloc(
    _Inout_ AsyncBlock* asyncBlock,
    _In_opt_ const void* identity,
    _In_opt_ const char* identityName,
    _In_ AsyncProvider* provider,
    _In_ size_t contextSize,
    _Out_ void** context)
{
    RETURN_HR_IF(E_INVALIDARG, contextSize == 0);
    RETURN_IF_FAILED(AllocState(asyncBlock, contextSize));

    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        state = internal.GetState();
    }

    // Alloc using a context size should always fail and not
    // try to send a completion.
    ASSERT(state != nullptr);

    state->provider = provider;
    state->identity = identity;
    state->identityName = identityName;

    ASSERT(state->providerData.context != nullptr);
    memset(state->providerData.context, 0, contextSize);
    *context = state->providerData.context;

    return S_OK;
}

/// <summary>
/// Schedules a callback to do async work.  Calling this is optional.  If the async work can be done
/// through a system - async mechanism like overlapped IO or async COM, there is no need to schedule
/// work.  If work should be scheduled after a delay, pass the number of ms ScheduleAsync should wait
/// before it schedules work.
/// </summary>
STDAPI ScheduleAsync(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ uint32_t delayInMs)
{
    HRESULT existingStatus;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        state = internal.GetState();
        existingStatus = internal.GetStatus();
    }

    // If the call already failed, return that failure code.
    RETURN_HR_IF(existingStatus, FAILED(existingStatus) && existingStatus != E_PENDING);

    // If the call has completed, the state will be null.  
    RETURN_HR_IF(E_INVALIDARG, state == nullptr);

    bool priorScheduled = false;
    state->workScheduled.compare_exchange_strong(priorScheduled, true);

    if (priorScheduled)
    {
        RETURN_HR(E_UNEXPECTED);
    }

    RETURN_IF_FAILED(SubmitAsyncCallback(
        state->queue,
        AsyncQueueCallbackType_Work,
        delayInMs,
        state.Get(),
        WorkerCallback));

    state->Detach();
    return S_OK;
}

/// <summary>
/// Called when async work is completed and the results can be returned.
/// The caller should supply the resulting data payload size.  If the call
/// has no data payload, pass zero.
/// </summary
STDAPI_(void) CompleteAsync(
    _Inout_ AsyncBlock* asyncBlock,
    _In_ HRESULT result,
    _In_ size_t requiredBufferSize)
{
    // E_PENDING is special -- if you still have work to do don't complete.

    if (result == E_PENDING)
    {
        return;
    }

    bool completedNow = false;
    bool doCleanup = false;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        HRESULT priorStatus = internal.GetStatus();

        completedNow = internal.TrySetTerminalStatus(result);

        // If the required buffer is zero, there is no payload and we need to
        // clean up now. Also clean up if the status is abort, as that indicates
        // the user cancelled the call.
        if (requiredBufferSize == 0 || priorStatus == E_ABORT)
        {
            // If we are going to cleanup steal the reference from the block.
            doCleanup = true;
            state = internal.ExtractState();
        }
        else
        {
            state = internal.GetState();
        }
    }

    // If prior status was not pending, we either already completed or were canceled.
    if (completedNow)
    {
        state->providerData.bufferSize = requiredBufferSize;
        SignalCompletion(state);
    }

    // At this point asyncBlock may be unsafe to touch.

    if (doCleanup)
    {
        CleanupState(std::move(state));
    }
}

/// <summary>
/// Returns the result data for the asynchronous operation.  After this call
/// the async block is completed and no longer associated with the
/// operation.
/// </summary>
STDAPI GetAsyncResult(
    _Inout_ AsyncBlock* asyncBlock,
    _In_opt_ const void* identity,
    _In_ size_t bufferSize,
    _Out_writes_bytes_to_opt_(bufferSize, *bufferUsed) void* buffer,
    _Out_opt_ size_t* bufferUsed)
{
    HRESULT result = E_PENDING;
    AsyncStateRef state;
    {
        AsyncBlockInternalGuard internal{ asyncBlock };
        result = internal.GetStatus();
        state = internal.ExtractState();
    }

    if (SUCCEEDED(result))
    {
        if (state == nullptr)
        {
            if (bufferUsed != nullptr)
            {
                *bufferUsed = 0;
            }
        }
        else if (identity != state->identity)
        {
            // Call/Result mismatch.  This AsyncBlock was initiated by state->identityName
            char buf[100];
            int sprintfResult;
            if (state->identityName != nullptr)
            {
                sprintfResult = snprintf(
                    buf,
                    sizeof(buf),
                    "Call/Result mismatch.  This AsyncBlock was initiated by '%s'.\r\n",
                    state->identityName);
            }
            else
            {
                sprintfResult = snprintf(buf, sizeof(buf), "Call/Result mismatch\r\n");
            }

            result = E_INVALIDARG;
            ASYNC_LIB_TRACE(result, buf);
            ASSERT(false);
            ASSERT(sprintfResult > 0);
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
            return E_NOT_SUFFICIENT_BUFFER;
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

    // Cleanup state if needed.
    if (result != E_PENDING && state != nullptr)
    {
        CleanupState(std::move(state));
    }

    return result;
}
