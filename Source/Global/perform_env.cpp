#include "pch.h"
#include "perform_env.h"
#include "httpcall.h"
#include "Platform/ExternalHttpProvider.h"
#include "Platform/ExternalWebSocketProvider.h"
#if !HC_NOWEBSOCKETS
#include "../WebSocket/hcwebsocket.h"
#endif

using namespace xbox::httpclient;

HC_PERFORM_ENV::HC_PERFORM_ENV(HC_UNIQUE_PTR<IHttpProvider> httpProvider, HC_UNIQUE_PTR<IWebSocketProvider> webSocketProvider) :
    m_httpProvider{ std::move(httpProvider) },
    m_webSocketProvider{ std::move(webSocketProvider) }
{
}

Result<HC_UNIQUE_PTR<HC_PERFORM_ENV>> HC_PERFORM_ENV::Initialize(
    HC_UNIQUE_PTR<IHttpProvider> httpProvider,
    HC_UNIQUE_PTR<IWebSocketProvider> webSocketProvider
) noexcept
{
    http_stl_allocator<HC_PERFORM_ENV> a{};
    HC_UNIQUE_PTR<HC_PERFORM_ENV> env{ new (a.allocate(1)) HC_PERFORM_ENV(std::move(httpProvider), std::move(webSocketProvider)) };

    return env;
}

IHttpProvider& HC_PERFORM_ENV::HttpProvider()
{
    // If the client configured an external provider use that. Otherwise use the m_httpProvider
    ExternalHttpProvider& externalProvider = ExternalHttpProvider::Get();
    if (externalProvider.HasCallback())
    {
        return externalProvider;
    }
    assert(m_httpProvider);
    return *m_httpProvider;
}

IWebSocketProvider& HC_PERFORM_ENV::WebSocketProvider()
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

struct HC_PERFORM_ENV::HttpPerformContext
{
    HttpPerformContext(HC_PERFORM_ENV* _env, HCCallHandle _callHandle, XAsyncBlock* _clientAsyncBlock) :
        env{ _env },
        callHandle{ _callHandle },
        clientAsyncBlock{ _clientAsyncBlock },
        internalAsyncBlock{ nullptr, this, HC_PERFORM_ENV::HttpPerformComplete }
    {
    }

    ~HttpPerformContext()
    {
        if (internalAsyncBlock.queue)
        {
            XTaskQueueCloseHandle(internalAsyncBlock.queue);
        }
    }

    HC_PERFORM_ENV* const env;
    HCCallHandle const callHandle;
    XAsyncBlock* const clientAsyncBlock;
    XAsyncBlock internalAsyncBlock;
};

HRESULT HC_PERFORM_ENV::HttpCallPerformAsyncShim(HCCallHandle call, XAsyncBlock* async)
{
    auto performContext = http_allocate_unique<HttpPerformContext>(this, call, async);
    RETURN_IF_FAILED(XAsyncBegin(async, performContext.get(), nullptr, __FUNCTION__, HttpPerformAsyncShimProvider));
    performContext.release();

    return S_OK;
}

HRESULT CALLBACK HC_PERFORM_ENV::HttpPerformAsyncShimProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    HttpPerformContext* performContext{ static_cast<HttpPerformContext*>(data->context) };
    HC_PERFORM_ENV* env{ performContext->env };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        XTaskQueuePortHandle workPort{};
        assert(data->async->queue); // Queue should never be null here
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &performContext->internalAsyncBlock.queue));

        std::unique_lock<std::mutex> lock{ env->m_mutex };
        env->m_activeHttpRequests.insert(performContext->clientAsyncBlock);
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
        std::unique_lock<std::mutex> lock{ env->m_mutex };
        env->m_activeHttpRequests.erase(performContext->clientAsyncBlock);
        bool scheduleProviderCleanup = env->ShouldScheduleProviderCleanup();
        lock.unlock();

        // Free performContext before scheduling ProviderCleanup to ensure it happens before returing to client
        HC_UNIQUE_PTR<HttpPerformContext> reclaim{ performContext };
        reclaim.reset();

        if (scheduleProviderCleanup)
        {
            HRESULT hr = XTaskQueueSubmitCallback(data->async->queue, XTaskQueuePort::Work, env, ProviderCleanup);
            if (FAILED(hr))
            {
                // This should only fail due to client terminating the queue in which case there isn't anything we can do anyhow
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "Unable to schedule ProviderCleanup");
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

void CALLBACK HC_PERFORM_ENV::HttpPerformComplete(XAsyncBlock* async)
{
    HttpPerformContext* performContext{ static_cast<HttpPerformContext*>(async->context) };
    XAsyncComplete(performContext->clientAsyncBlock, XAsyncGetStatus(async, false), 0);
}

#if !HC_NOWEBSOCKETS
struct HC_PERFORM_ENV::WebSocketConnectContext
{
    WebSocketConnectContext(
        HC_PERFORM_ENV* _env,
        http_internal_string&& _uri,
        http_internal_string&& _subprotocol,
        HCWebsocketHandle _websocketHandle,
        XAsyncBlock* _clientAsyncBlock
    ) : env{ _env },
        uri{ std::move(_uri) },
        subprotocol{ std::move(_subprotocol) },
        websocketHandle{ _websocketHandle },
        websocket{ websocketHandle->websocket },
        clientAsyncBlock{ _clientAsyncBlock },
        internalAsyncBlock{ nullptr, this, HC_PERFORM_ENV::WebSocketConnectComplete }
    {
    }

    ~WebSocketConnectContext()
    {
        if (internalAsyncBlock.queue)
        {
            XTaskQueueCloseHandle(internalAsyncBlock.queue);
        }
    }

    HC_PERFORM_ENV* const env{};
    http_internal_string uri;
    http_internal_string subprotocol;
    HCWebsocketHandle websocketHandle;
    std::shared_ptr<WebSocket> websocket;
    XAsyncBlock* const clientAsyncBlock;
    XAsyncBlock internalAsyncBlock{};
    WebSocketCompletionResult connectResult{};
};

struct HC_PERFORM_ENV::ActiveWebSocketContext
{
    ActiveWebSocketContext(HC_PERFORM_ENV* _env, std::shared_ptr<WebSocket> websocket) :
        env{ _env },
        websocketObserver{ HC_WEBSOCKET_OBSERVER::Initialize(std::move(websocket), nullptr, nullptr, nullptr, HC_PERFORM_ENV::WebSocketClosed, this) }
    {
    }

    HC_PERFORM_ENV* const env{};
    xbox::httpclient::ObserverPtr websocketObserver;
};

HRESULT HC_PERFORM_ENV::WebSocketConnectAsyncShim(
    _In_ http_internal_string&& uri,
    _In_ http_internal_string&& subprotocol,
    _In_ HCWebsocketHandle clientWebSocketHandle,
    _Inout_ XAsyncBlock* asyncBlock
)
{
    auto context = http_allocate_unique<WebSocketConnectContext>(this, std::move(uri), std::move(subprotocol), clientWebSocketHandle, asyncBlock);
    RETURN_IF_FAILED(XAsyncBegin(asyncBlock, context.get(), (void*)HCWebSocketConnectAsync, nullptr, WebSocketConnectAsyncShimProvider));
    context.release();

    return S_OK;
}

HRESULT CALLBACK HC_PERFORM_ENV::WebSocketConnectAsyncShimProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    WebSocketConnectContext* context{ static_cast<WebSocketConnectContext*>(data->context) };
    HC_PERFORM_ENV* env{ context->env };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        XTaskQueuePortHandle workPort{};
        assert(data->async->queue); // Queue should never be null here
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &context->internalAsyncBlock.queue));

        std::unique_lock<std::mutex> lock{ env->m_mutex };
        env->m_connectingWebSockets.insert(context->clientAsyncBlock);
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
        HC_UNIQUE_PTR<WebSocketConnectContext> reclaim{ context };
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK HC_PERFORM_ENV::WebSocketConnectComplete(XAsyncBlock* async)
{
    WebSocketConnectContext* context{ static_cast<WebSocketConnectContext*>(async->context) };
    HC_PERFORM_ENV* env{ context->env };

    std::unique_lock<std::mutex> lock{ env->m_mutex };
    env->m_connectingWebSockets.erase(context->clientAsyncBlock);

    // If cleanup is pending and the connect succeeded, immediately disconnect
    bool disconnect{ false };

    HRESULT hr = HCGetWebSocketConnectResult(&context->internalAsyncBlock, &context->connectResult);
    if (SUCCEEDED(hr) && SUCCEEDED(context->connectResult.errorCode))
    {
        // Pass the clients handle back to them in the result
        context->connectResult.websocket = context->websocketHandle;

        env->m_connectedWebSockets.insert(new (http_stl_allocator<ActiveWebSocketContext>{}.allocate(1)) ActiveWebSocketContext{ env, context->websocket });
        if (env->m_cleanupAsyncBlock)
        {
            disconnect = true;
        }
    }

    bool scheduleProviderCleanup = env->ShouldScheduleProviderCleanup();
    lock.unlock();

    assert(!scheduleProviderCleanup || !disconnect);
    if (disconnect)
    {
        hr = context->websocket->Disconnect();
        if (FAILED(hr))
        {
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WebSocket::Disconnect failed during HCCleanup");
        }
    }
    else if (scheduleProviderCleanup)
    {
        hr = XTaskQueueSubmitCallback(env->m_cleanupAsyncBlock->queue, XTaskQueuePort::Work, env, ProviderCleanup);
        if (FAILED(hr))
        {
            // This should only fail due to client terminating the queue in which case there isn't anything we can do anyhow
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "Unable to schedule ProviderCleanup");
        }
    }

    XAsyncComplete(context->clientAsyncBlock, hr, sizeof(WebSocketCompletionResult));
}

void CALLBACK HC_PERFORM_ENV::WebSocketClosed(HCWebsocketHandle /*websocket*/, HCWebSocketCloseStatus /*closeStatus*/, void* c)
{
    ActiveWebSocketContext* context{ static_cast<ActiveWebSocketContext*>(c) };
    HC_PERFORM_ENV* env{ context->env };

    std::unique_lock<std::mutex> lock{ env->m_mutex };
    env->m_connectedWebSockets.erase(context);
    bool scheduleProviderCleanup = env->ShouldScheduleProviderCleanup();
    lock.unlock();

    // Free context before scheduling ProviderCleanup to ensure it happens before returing to client
    HC_UNIQUE_PTR<ActiveWebSocketContext> reclaim{ context };
    reclaim.reset();

    if (scheduleProviderCleanup)
    {
        HRESULT hr = XTaskQueueSubmitCallback(env->m_cleanupAsyncBlock->queue, XTaskQueuePort::Work, env, ProviderCleanup);
        if (FAILED(hr))
        {
            // This should only fail due to client terminating the queue in which case there isn't anything we can do anyhow
            HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "Unable to schedule ProviderCleanup");
        }
    }
}
#endif

HRESULT HC_PERFORM_ENV::CleanupAsync(HC_UNIQUE_PTR<HC_PERFORM_ENV> env, XAsyncBlock* async) noexcept
{
    RETURN_IF_FAILED(XAsyncBegin(async, env.get(), __FUNCTION__, __FUNCTION__, CleanupAsyncProvider));
    env.release();
    return S_OK;
}

HRESULT CALLBACK HC_PERFORM_ENV::CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    HC_PERFORM_ENV* env{ static_cast<HC_PERFORM_ENV*>(data->context) };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        std::unique_lock<std::mutex> lock{ env->m_mutex };
        env->m_cleanupAsyncBlock = data->async;
        bool scheduleProviderCleanup = env->ShouldScheduleProviderCleanup();

        for (auto& activeRequest : env->m_activeHttpRequests)
        {
            XAsyncCancel(activeRequest);
        }
#if !HC_NOWEBSOCKETS
        for (auto& context : env->m_connectedWebSockets)
        {
            HRESULT hr = context->websocketObserver->websocket->Disconnect();
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WebSocket::Disconnect failed during HCCleanup");
            }
        }
#endif
        lock.unlock();

        if (scheduleProviderCleanup)
        {
            return XTaskQueueSubmitCallback(data->async->queue, XTaskQueuePort::Work, env, ProviderCleanup);
        }
        return S_OK;
    }
    default:
    {
        return S_OK;
    }
    }
}

void CALLBACK HC_PERFORM_ENV::ProviderCleanup(void* context, bool /*canceled*/)
{
    HC_UNIQUE_PTR<HC_PERFORM_ENV> env{ static_cast<HC_PERFORM_ENV*>(context) };
    XAsyncBlock* cleanupAsyncBlock{ env->m_cleanupAsyncBlock };

    HC_UNIQUE_PTR<XAsyncBlock> providerCleanupAsyncBlock{ new (http_stl_allocator<XAsyncBlock>{}.allocate(1)) XAsyncBlock
    {
        cleanupAsyncBlock->queue,
        env.get(),
        ProviderCleanupComplete
    } };

    HRESULT hr = env->m_httpProvider->CleanupAsync(providerCleanupAsyncBlock.get());
    if (FAILED(hr))
    {
        env.reset();
        providerCleanupAsyncBlock.reset();

        XAsyncComplete(cleanupAsyncBlock, hr, 0);
    }
    else
    {
        env.release();
        providerCleanupAsyncBlock.release();
    }
}

void CALLBACK HC_PERFORM_ENV::ProviderCleanupComplete(XAsyncBlock* async)
{
    HC_UNIQUE_PTR<XAsyncBlock> providerCleanupAsyncBlock{ async };
    HC_UNIQUE_PTR<HC_PERFORM_ENV> env{ static_cast<HC_PERFORM_ENV*>(providerCleanupAsyncBlock->context) };
    XAsyncBlock* envCleanupAsyncBlock = env->m_cleanupAsyncBlock;

    HRESULT cleanupResult = XAsyncGetStatus(providerCleanupAsyncBlock.get(), false);
    providerCleanupAsyncBlock.reset();
    env.reset();

    // HC_PERFORM_ENV fully cleaned up at this point
    XAsyncComplete(envCleanupAsyncBlock, cleanupResult, 0);
}


bool HC_PERFORM_ENV::ShouldScheduleProviderCleanup()
{
    if (!m_cleanupAsyncBlock)
    {
        // HC_PERFORM_ENV::CleanupAsync has not yet been called
        return false;
    }
    else if (!m_activeHttpRequests.empty())
    {
        // Pending Http Requests
        return false;
    }
#if !HC_NOWEBSOCKETS
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
