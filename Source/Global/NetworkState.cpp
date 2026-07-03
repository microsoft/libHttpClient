#include "pch.h"
#include "NetworkState.h"
#include "Platform/ExternalHttpProvider.h"
#ifndef HC_NOWEBSOCKETS
#include "Platform/ExternalWebSocketProvider.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#ifndef HC_NOWEBSOCKETS
namespace
{

void NotifyProviderSuspending(IWebSocketProvider* provider) noexcept
{
    if (auto lifecycle = GetProviderLifecycle(provider))
    {
        lifecycle->OnSuspending();
    }
}

void NotifyProviderResuming(IWebSocketProvider* provider) noexcept
{
    if (auto lifecycle = GetProviderLifecycle(provider))
    {
        lifecycle->OnResuming();
    }
}

}

NetworkState::NetworkState(
    UniquePtr<IHttpProvider> httpProvider,
    UniquePtr<IWebSocketProvider> webSocketProvider
) noexcept :
    m_httpProvider{ std::move(httpProvider) },
    m_webSocketProvider{ std::move(webSocketProvider) }
{
    assert(m_webSocketProvider);
}

Result<UniquePtr<NetworkState>> NetworkState::Initialize(
    UniquePtr<IHttpProvider> httpProvider,
    UniquePtr<IWebSocketProvider> webSocketProvider
) noexcept
{
    http_stl_allocator<NetworkState> a{};
    UniquePtr<NetworkState> state{ new (a.allocate(1)) NetworkState(std::move(httpProvider), std::move(webSocketProvider)) };

    return state;
}

#else
NetworkState::NetworkState(UniquePtr<IHttpProvider> httpProvider) noexcept :
    m_httpProvider{ std::move(httpProvider) }
{
}

Result<UniquePtr<NetworkState>> NetworkState::Initialize(
    UniquePtr<IHttpProvider> httpProvider
) noexcept
{
    http_stl_allocator<NetworkState> a{};
    UniquePtr<NetworkState> state{ new (a.allocate(1)) NetworkState(std::move(httpProvider)) };

    return state;
}
#endif

IHttpProvider & NetworkState::HttpProvider() noexcept
{
    // If the client configured an external provider use that. Otherwise use the m_httpProvider
    ExternalHttpProvider & externalProvider = ExternalHttpProvider::Get();
    if (externalProvider.HasCallback())
    {
        return externalProvider;
    }
    assert(m_httpProvider);
    return *m_httpProvider;
}

Result<UniquePtr<HC_CALL>> NetworkState::HttpCallCreate() noexcept
{
    auto httpSingleton = get_http_singleton();
    RETURN_HR_IF(E_HC_NOT_INITIALISED, !httpSingleton);

    auto call = http_allocate_unique<HC_CALL>(++httpSingleton->m_lastId, HttpProvider());
    call->retryAllowed = httpSingleton->m_retryAllowed;
    call->timeoutInSeconds = httpSingleton->m_timeoutInSeconds;
    call->timeoutWindowInSeconds = httpSingleton->m_timeoutWindowInSeconds;
    call->retryDelayInSeconds = httpSingleton->m_retryDelayInSeconds;

    return call;
}

// Coordinates handoff of the client-owned XAsyncBlock between cleanup and completion.
// This prevents CleanupAsyncProvider from canceling a request while HttpCallPerformComplete is
// handing that same XAsyncBlock to XAsyncComplete, after which the client callback may delete it.
// Once CallbackStarted is published, cleanup must never touch clientAsyncBlock again.
enum class HttpPerformClientBlockState : uint8_t
{
    CleanupMayCancel,
    CleanupCancelInFlight,
    CallbackStarted
};

struct NetworkState::HttpPerformContext
{
    HttpPerformContext(NetworkState& _state, HCCallHandle _callHandle, XAsyncBlock* _clientAsyncBlock) :
        state{ _state },
        callHandle{ _callHandle },
        clientAsyncBlock{ _clientAsyncBlock },
        internalAsyncBlock{ nullptr, this, NetworkState::HttpCallPerformComplete }
    {
    }

    ~HttpPerformContext()
    {
        if (internalAsyncBlock.queue)
        {
            XTaskQueueCloseHandle(internalAsyncBlock.queue);
        }
    }

    NetworkState& state;
    HCCallHandle const callHandle;
    XAsyncBlock* const clientAsyncBlock;
    std::atomic<HttpPerformClientBlockState> clientBlockState{ HttpPerformClientBlockState::CleanupMayCancel };
    XAsyncBlock internalAsyncBlock;
};

HRESULT NetworkState::HttpCallPerformAsync(HCCallHandle call, XAsyncBlock* async) noexcept
{
    auto performContext = http_allocate_unique<HttpPerformContext>(*this, call, async);
    RETURN_IF_FAILED(XAsyncBegin(async, performContext.get(), nullptr, __FUNCTION__, HttpCallPerformAsyncProvider));
    performContext.release();

    return S_OK;
}

#ifdef HC_UNITTEST_API
bool NetworkState::CanCleanupCancelHttpRequest(XAsyncBlock* async) noexcept
{
    std::unique_lock<std::mutex> lock{ m_mutex };
    for (auto context : m_activeHttpRequests)
    {
        if (context->clientAsyncBlock == async && context->clientBlockState.load(std::memory_order_acquire) != HttpPerformClientBlockState::CallbackStarted)
        {
            return true;
        }
    }
    return false;
}

void NetworkState::TestSetCleanupStarted(bool started, XAsyncBlock* cleanupAsyncBlock) noexcept
{
    std::unique_lock<std::mutex> lock{ m_mutex };
    m_cleanupStarted = started;
    m_cleanupAsyncBlock = cleanupAsyncBlock;
}
#endif

HRESULT CALLBACK NetworkState::HttpCallPerformAsyncProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    HttpPerformContext* performContext{ static_cast<HttpPerformContext*>(data->context) };
    NetworkState& state{ performContext->state };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        XTaskQueuePortHandle workPort{};
        assert(data->async->queue); // Queue should never be null here
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &performContext->internalAsyncBlock.queue));

        std::unique_lock<std::mutex> lock{ state.m_mutex };
        if (state.m_cleanupStarted)
        {
            // Cleanup has already begun and taken (or is taking) its snapshot of
            // m_activeHttpRequests under this same mutex. Refuse the request instead of inserting
            // it after the snapshot, which would orphan it (never canceled or awaited) and could
            // run it against a torn-down provider.
            lock.unlock();
            return E_HC_NOT_INITIALISED;
        }
        state.m_activeHttpRequests.insert(performContext);
        lock.unlock();

        return performContext->callHandle->PerformAsync(&performContext->internalAsyncBlock);
    }
    case XAsyncOp::Cancel:
    {
        XAsyncCancel(&performContext->internalAsyncBlock);
        return S_OK;
    }
    case XAsyncOp::Cleanup:
    {
        std::unique_lock<std::mutex> lock{ state.m_mutex };
        // Only a perform that was actually admitted (present in m_activeHttpRequests) may drive the
        // cleanup wakeup. A perform rejected by the m_cleanupStarted guard was never inserted, so
        // erase() returns 0 and we must not call ScheduleCleanup()/XAsyncSchedule for it -- doing so
        // would spuriously (re)schedule cleanup's async block for a request that was never tracked.
        bool scheduleCleanup = state.m_activeHttpRequests.erase(performContext) != 0 && state.ScheduleCleanup();
        lock.unlock();

        // Free performContext before scheduling cleanup to ensure it happens before returing to client
        UniquePtr<HttpPerformContext> reclaim{ performContext };
        reclaim.reset();

        if (scheduleCleanup)
        {
            HRESULT hr = XAsyncSchedule(state.m_cleanupAsyncBlock, 0);
            if (FAILED(hr))
            {
                // This should only fail due to client terminating the queue in which case there isn't anything we can do anyhow
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "Unable to schedule NetworkState::CleanupAsyncProvider");
            }
        }
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK NetworkState::HttpCallPerformComplete(XAsyncBlock* async)
{
    HttpPerformContext* performContext{ static_cast<HttpPerformContext*>(async->context) };

    // Cleanup snapshots requests under m_mutex and then issues XAsyncCancel outside the lock.
    // A snapshotted request publishes CleanupCancelInFlight before that lock is released, so
    // the completion path waits here until cancel propagation finishes or until it wins the race
    // and publishes CallbackStarted itself.
    bool clientCallbackMayRun{ false };
    while (!clientCallbackMayRun)
    {
        switch (performContext->clientBlockState.load(std::memory_order_acquire))
        {
        case HttpPerformClientBlockState::CallbackStarted:
        {
            clientCallbackMayRun = true;
            break;
        }

        case HttpPerformClientBlockState::CleanupMayCancel:
        {
            auto expectedState = HttpPerformClientBlockState::CleanupMayCancel;
            if (performContext->clientBlockState.compare_exchange_weak(
                expectedState,
                HttpPerformClientBlockState::CallbackStarted,
                std::memory_order_acq_rel,
                std::memory_order_acquire))
            {
                clientCallbackMayRun = true;
            }
            break;
        }

        case HttpPerformClientBlockState::CleanupCancelInFlight:
        {
            // Expected transient state while CleanupAsyncProvider is synchronously issuing
            // XAsyncCancel for this snapshotted request outside m_mutex. That path restores
            // CleanupMayCancel before it leaves, at which point this loop can retry the handoff.
            std::this_thread::yield();
            break;
        }
        }
    }

    XAsyncComplete(performContext->clientAsyncBlock, XAsyncGetStatus(async, false), 0);
}

#ifndef HC_NOWEBSOCKETS
IWebSocketProvider& NetworkState::WebSocketProvider() noexcept
{
    // If the client configured an external provider use that. Otherwise use the m_webSocketProvider
    ExternalWebSocketProvider& externalProvider = ExternalWebSocketProvider::Get();
    if (externalProvider.HasCallbacks())
    {
        return externalProvider;
    }
    assert(m_webSocketProvider);
    return *m_webSocketProvider;
}

void NetworkState::NotifyWebSocketSuspending() noexcept
{
    // Lifecycle notifications are scoped to the built-in provider path.
    // External websocket callback overrides are app-owned and are not treated as lifecycle-capable providers.
    assert(m_webSocketProvider);
    NotifyProviderSuspending(m_webSocketProvider.get());
}

void NetworkState::NotifyWebSocketResuming() noexcept
{
    assert(m_webSocketProvider);
    NotifyProviderResuming(m_webSocketProvider.get());
}

Result<SharedPtr<WebSocket>> NetworkState::WebSocketCreate() noexcept
{
    auto httpSingleton = get_http_singleton();
    RETURN_HR_IF(E_HC_NOT_INITIALISED, !httpSingleton);

    return http_allocate_shared<WebSocket>(++httpSingleton->m_lastId, WebSocketProvider());
}

struct NetworkState::WebSocketConnectContext
{
    WebSocketConnectContext(
        NetworkState& _state,
        http_internal_string&& _uri,
        http_internal_string&& _subprotocol,
        HCWebsocketHandle _websocketHandle,
        XAsyncBlock* _clientAsyncBlock
    ) : state{ _state },
        uri{ std::move(_uri) },
        subprotocol{ std::move(_subprotocol) },
        websocketHandle{ _websocketHandle },
        websocket{ websocketHandle->websocket },
        clientAsyncBlock{ _clientAsyncBlock },
        internalAsyncBlock{ nullptr, this, NetworkState::WebSocketConnectComplete }
    {
    }

    ~WebSocketConnectContext()
    {
        if (internalAsyncBlock.queue)
        {
            XTaskQueueCloseHandle(internalAsyncBlock.queue);
        }
    }

    NetworkState& state;
    String uri;
    String subprotocol;
    HCWebsocketHandle websocketHandle;
    std::shared_ptr<WebSocket> websocket;
    XAsyncBlock* const clientAsyncBlock;
    XAsyncBlock internalAsyncBlock{};
    WebSocketCompletionResult connectResult{};
};

struct NetworkState::ActiveWebSocketContext
{
    ActiveWebSocketContext(NetworkState& _state, std::shared_ptr<WebSocket> websocket) :
        state{ _state },
        websocketObserver{ HC_WEBSOCKET_OBSERVER::Initialize(std::move(websocket), nullptr, nullptr, nullptr, NetworkState::WebSocketClosed, this) }
    {
    }

    NetworkState& state;
    xbox::httpclient::ObserverPtr websocketObserver;
};

HRESULT NetworkState::WebSocketConnectAsync(
    String&& uri,
    String&& subprotocol,
    HCWebsocketHandle clientWebSocketHandle,
    XAsyncBlock* asyncBlock
) noexcept
{
    auto context = http_allocate_unique<WebSocketConnectContext>(*this, std::move(uri), std::move(subprotocol), clientWebSocketHandle, asyncBlock);
    RETURN_IF_FAILED(XAsyncBegin(asyncBlock, context.get(), (void*)HCWebSocketConnectAsync, nullptr, WebSocketConnectAsyncProvider));
    context.release();

    return S_OK;
}

HRESULT CALLBACK NetworkState::WebSocketConnectAsyncProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    WebSocketConnectContext* context{ static_cast<WebSocketConnectContext*>(data->context) };
    NetworkState& state{ context->state };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        assert(data->async->queue); // Queue should never be null here

        // Run the internal connect completion on the caller's Work port so it is not stranded when the
        // Completion port is dispatched asymmetrically. The client completion still fires on the
        // Completion port via the outer async block.
        XTaskQueuePortHandle workPort{ nullptr };
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &context->internalAsyncBlock.queue));

        std::unique_lock<std::mutex> lock{ state.m_mutex };
        if (state.m_cleanupStarted)
        {
            // See the equivalent guard in HttpCallPerformAsyncProvider: reject connects that arrive
            // after cleanup has begun rather than orphaning them past the cleanup snapshot.
            lock.unlock();
            return E_HC_NOT_INITIALISED;
        }
        state.m_connectingWebSockets.insert(context->clientAsyncBlock);
        lock.unlock();

        return context->websocket->ConnectAsync(std::move(context->uri), std::move(context->subprotocol), &context->internalAsyncBlock);
    }
    case XAsyncOp::GetResult:
    {
        WebSocketCompletionResult* result{ reinterpret_cast<WebSocketCompletionResult*>(data->buffer) };
        *result = context->connectResult;
        return S_OK;
    }
    case XAsyncOp::Cleanup:
    {
        UniquePtr<WebSocketConnectContext> reclaim{ context };
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK NetworkState::WebSocketConnectComplete(XAsyncBlock* async)
{
    WebSocketConnectContext* context{ static_cast<WebSocketConnectContext*>(async->context) };
    NetworkState& state{ context->state };

    std::unique_lock<std::mutex> lock{ state.m_mutex };
    state.m_connectingWebSockets.erase(context->clientAsyncBlock);

    // If cleanup is pending and the connect succeeded, immediately disconnect
    bool disconnect{ false };

    HRESULT hr = HCGetWebSocketConnectResult(&context->internalAsyncBlock, &context->connectResult);
    if (SUCCEEDED(hr))
    {
        // Pass the clients handle back to them in the result
        context->connectResult.websocket = context->websocketHandle;

        if (SUCCEEDED(context->connectResult.errorCode))
        {
            state.m_connectedWebSockets.insert(new (http_stl_allocator<ActiveWebSocketContext>{}.allocate(1)) ActiveWebSocketContext{ state, context->websocket });
            if (state.m_cleanupAsyncBlock)
            {
                disconnect = true;
            }
        }
    }

    bool scheduleCleanup = state.ScheduleCleanup();
    lock.unlock();

    assert(!scheduleCleanup || !disconnect);
    if (disconnect)
    {
        hr = context->websocket->Disconnect();
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WebSocket::Disconnect failed during HCCleanup");
        }
    }
    else if (scheduleCleanup)
    {
        hr = XAsyncSchedule(state.m_cleanupAsyncBlock, 0);
        if (FAILED(hr))
        {
            // This should only fail due to client terminating the queue in which case there isn't anything we can do anyhow
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "Unable to schedule NetworkState::CleanupAsyncProvider");
        }
    }

    XAsyncComplete(context->clientAsyncBlock, hr, sizeof(WebSocketCompletionResult));
}

void CALLBACK NetworkState::WebSocketClosed(HCWebsocketHandle /*websocket*/, HCWebSocketCloseStatus /*closeStatus*/, void* c)
{
    ActiveWebSocketContext* context{ static_cast<ActiveWebSocketContext*>(c) };
    NetworkState& state{ context->state };

    std::unique_lock<std::mutex> lock{ state.m_mutex };
    state.m_connectedWebSockets.erase(context);
    bool scheduleCleanup = state.ScheduleCleanup();
    lock.unlock();

    // Free context before scheduling ProviderCleanup to ensure it happens before returing to client
    UniquePtr<ActiveWebSocketContext> reclaim{ context };
    reclaim.reset();

    if (scheduleCleanup)
    {
        HRESULT hr = XAsyncSchedule(state.m_cleanupAsyncBlock, 0);
        if (FAILED(hr))
        {
            // This should only fail due to client terminating the queue in which case there isn't anything we can do anyhow
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "Unable to schedule NetworkState::CleanupAsyncProvider");
        }
    }
}
#endif // !HC_NOWEBSOCKETS

HRESULT NetworkState::CleanupAsync(NetworkState* state, XAsyncBlock* async) noexcept
{
    // NetworkState is not taken by owning pointer here: it remains owned by the http_singleton for
    // its whole lifetime. Cleanup runs against the still-owned instance and never destroys it, so an
    // in-flight API caller holding a singleton reference can never observe a moved-from or destroyed
    // NetworkState (Race B). The instance is destroyed together with the singleton, once the
    // singleton's use_count gate confirms no other references remain.
    return XAsyncBegin(async, state, __FUNCTION__, __FUNCTION__, CleanupAsyncProvider);
}

HRESULT CALLBACK NetworkState::CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    assert(data->context);
    NetworkState* state{ static_cast<NetworkState*>(data->context) };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        xbox::httpclient::Vector<HttpPerformContext*> activeRequestsToCancel;
#ifndef HC_NOWEBSOCKETS
        xbox::httpclient::Vector<ActiveWebSocketContext*> connectedWebSockets;
#endif
        bool scheduleCleanup = false;
        {
            std::unique_lock<std::mutex> lock{ state->m_mutex };
            state->m_cleanupAsyncBlock = data->async;
            state->m_cleanupStarted = true;
            scheduleCleanup = state->ScheduleCleanup();

#ifndef HC_NOWEBSOCKETS 
            HC_TRACE_VERBOSE(HTTPCLIENT, "NetworkState::CleanupAsyncProvider::Begin: HTTP active=%llu, WebSocket Connecting=%llu, WebSocket Connected=%llu", state->m_activeHttpRequests.size(), state->m_connectingWebSockets.size(), state->m_connectedWebSockets.size());
#endif
            // Setting m_cleanupStarted above (under m_mutex) closes the admission window: any HTTP
            // perform or WebSocket connect whose Begin op acquires m_mutex after this point is
            // refused, so nothing can be inserted into the tracking sets after the snapshot below.
            // Requests that acquired m_mutex before us are already in m_activeHttpRequests and are
            // captured by the snapshot here. Snapshot them under the lock and cancel them after
            // releasing m_mutex; this prevents holding the lock across XAsyncCancel while still
            // ensuring completion cannot advance a request cleanup has already decided to cancel.
            for (auto activeRequest : state->m_activeHttpRequests)
            {
                auto expectedState = HttpPerformClientBlockState::CleanupMayCancel;
                if (activeRequest->clientBlockState.compare_exchange_strong(
                    expectedState,
                    HttpPerformClientBlockState::CleanupCancelInFlight,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
                {
                    activeRequestsToCancel.push_back(activeRequest);
                }
            }
#ifndef HC_NOWEBSOCKETS
            connectedWebSockets.assign(state->m_connectedWebSockets.begin(), state->m_connectedWebSockets.end());
#endif
        }

        // The snapshot remains valid outside m_mutex because a request in CleanupCancelInFlight
        // cannot publish CallbackStarted, and the active-set entry is only removed during the
        // client async cleanup that follows that callback.
        for (auto activeRequest : activeRequestsToCancel)
        {
            XAsyncCancel(activeRequest->clientAsyncBlock);

            // XAsyncCancel synchronously propagated the cancel request to the provider. If the
            // request is still alive after that, the completion path may resume and enter the
            // client callback.
            activeRequest->clientBlockState.store(HttpPerformClientBlockState::CleanupMayCancel, std::memory_order_release);
        }
#ifndef HC_NOWEBSOCKETS
        for (auto& context : connectedWebSockets)
        {
            HRESULT hr = context->websocketObserver->websocket->Disconnect();
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WebSocket::Disconnect failed during HCCleanup");
            }
        }
#endif

        if (scheduleCleanup)
        {
            return XAsyncSchedule(data->async, 0);
        }
        return S_OK;
    }
    case XAsyncOp::DoWork:
    {
        UniquePtr<XAsyncBlock> providerCleanupAsyncBlock{ new (http_stl_allocator<XAsyncBlock>{}.allocate(1)) XAsyncBlock
        {
            data->async->queue,
            state,
            HttpProviderCleanupComplete
        } };

        HRESULT hr = state->m_httpProvider->CleanupAsync(providerCleanupAsyncBlock.get());
        if (FAILED(hr))
        {
            XAsyncBlock* cleanupAsyncBlock{ state->m_cleanupAsyncBlock };

            // NetworkState stays owned by the http_singleton; do not destroy it here.
            providerCleanupAsyncBlock.reset();

            XAsyncComplete(cleanupAsyncBlock, hr, 0);
            return S_OK;
        }
        else
        {
            providerCleanupAsyncBlock.release();
        }
        return E_PENDING;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK NetworkState::HttpProviderCleanupComplete(XAsyncBlock* async)
{
    UniquePtr<XAsyncBlock> providerCleanupAsyncBlock{ async };
    NetworkState* state{ static_cast<NetworkState*>(providerCleanupAsyncBlock->context) };
    XAsyncBlock* stateCleanupAsyncBlock = state->m_cleanupAsyncBlock;

    HRESULT cleanupResult = XAsyncGetStatus(providerCleanupAsyncBlock.get(), false);
    providerCleanupAsyncBlock.reset();

    // NetworkState's providers are cleaned up at this point. The NetworkState instance itself stays
    // owned by the http_singleton and is destroyed when the singleton is (after its use_count gate),
    // so an in-flight caller holding a singleton reference never observes it freed.
    XAsyncComplete(stateCleanupAsyncBlock, cleanupResult, 0);
}

bool NetworkState::ScheduleCleanup()
{
    if (!m_cleanupAsyncBlock)
    {
        // HC_PERFORM_ENV::CleanupAsync has not yet been called
        return false;
    }

#ifndef HC_NOWEBSOCKETS
    HC_TRACE_VERBOSE(HTTPCLIENT, "HC_PERFORM_ENV::Cleaning up, HTTP=%llu, WebSocket Connecting=%llu, WebSocket Connected=%llu", m_activeHttpRequests.size(), m_connectingWebSockets.size(), m_connectedWebSockets.size());
#endif
    if (!m_activeHttpRequests.empty())
    {
        // Pending Http Requests
        return false;
    }
#ifndef HC_NOWEBSOCKETS
    else if (!m_connectingWebSockets.empty())
    {
        // Pending WebSocket Connect operations
        return false;
    }
    else if (!m_connectedWebSockets.empty())
    {
        // Pending WebSocket CloseFunc callbacks
        return false;
    }
#endif
    return true;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
