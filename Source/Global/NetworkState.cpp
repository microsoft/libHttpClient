#include "pch.h"
#include "NetworkState.h"
#include "Platform/ExternalHttpProvider.h"
#ifndef HC_NOWEBSOCKETS
#include "Platform/ExternalWebSocketProvider.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#ifndef HC_NOWEBSOCKETS
NetworkState::NetworkState(UniquePtr<IHttpProvider> httpProvider, UniquePtr<IWebSocketProvider> webSocketProvider) noexcept :
    m_httpProvider{ std::move(httpProvider) },
    m_webSocketProvider{ std::move(webSocketProvider) }
{
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
    XAsyncBlock internalAsyncBlock;
};

HRESULT NetworkState::HttpCallPerformAsync(HCCallHandle call, XAsyncBlock* async) noexcept
{
    auto performContext = http_allocate_unique<HttpPerformContext>(*this, call, async);
    RETURN_IF_FAILED(XAsyncBegin(async, performContext.get(), nullptr, __FUNCTION__, HttpCallPerformAsyncProvider));
    performContext.release();

    return S_OK;
}

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
        state.m_activeHttpRequests.insert(performContext->clientAsyncBlock);
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
        state.m_activeHttpRequests.erase(performContext->clientAsyncBlock);
        bool scheduleCleanup = state.ScheduleCleanup();
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
        XTaskQueuePortHandle workPort{};
        assert(data->async->queue); // Queue should never be null here
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &context->internalAsyncBlock.queue));

        std::unique_lock<std::mutex> lock{ state.m_mutex };
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

HRESULT NetworkState::CleanupAsync(UniquePtr<NetworkState> state, XAsyncBlock* async) noexcept
{
    RETURN_IF_FAILED(XAsyncBegin(async, state.get(), __FUNCTION__, __FUNCTION__, CleanupAsyncProvider));
    state.release();
    return S_OK;
}

HRESULT CALLBACK NetworkState::CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    assert(data->context);
    NetworkState* state{ static_cast<NetworkState*>(data->context) };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        std::unique_lock<std::mutex> lock{ state->m_mutex };
        state->m_cleanupAsyncBlock = data->async;
        bool scheduleCleanup = state->ScheduleCleanup();

#ifndef HC_NOWEBSOCKETS
        HC_TRACE_VERBOSE(HTTPCLIENT, "NetworkState::CleanupAsyncProvider::Begin: HTTP active=%llu, WebSocket Connecting=%llu, WebSocket Connected=%llu", state->m_activeHttpRequests.size(), state->m_connectingWebSockets.size(), state->m_connectedWebSockets.size());
#endif
        for (auto& activeRequest : state->m_activeHttpRequests)
        {
            XAsyncCancel(activeRequest);
        }
#ifndef HC_NOWEBSOCKETS
        for (auto& context : state->m_connectedWebSockets)
        {
            HRESULT hr = context->websocketObserver->websocket->Disconnect();
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WebSocket::Disconnect failed during HCCleanup");
            }
        }
#endif
        lock.unlock();

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

            UniquePtr<NetworkState> reclaim{ state };
            reclaim.reset();
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
    UniquePtr<NetworkState> state{ static_cast<NetworkState*>(providerCleanupAsyncBlock->context) };
    XAsyncBlock* stateCleanupAsyncBlock = state->m_cleanupAsyncBlock;

    HRESULT cleanupResult = XAsyncGetStatus(providerCleanupAsyncBlock.get(), false);
    providerCleanupAsyncBlock.reset();
    state.reset();

    // NetworkState fully cleaned up at this point
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
