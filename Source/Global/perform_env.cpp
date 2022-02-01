#include "pch.h"
#include "perform_env.h"
#include "httpcall.h"

#if HC_PLATFORM == HC_PLATFORM_WIN32
#include "WebSocket/Websocketpp/websocketpp_websocket.h"
#elif HC_PLATFORM == HC_PLATFORM_UWP || HC_PLATFORM == HC_PLATFORM_XDK
#include "HTTP/XMLHttp/xmlhttp_http_task.h"
#include "WebSocket/WinRT/winrt_websocket.h"
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
#include "HTTP/Android/android_http_request.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"
#elif HC_PLATFORM_IS_APPLE
#include "HTTP/Apple/http_apple.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"
#endif

using namespace xbox::httpclient;

// Fallback Http/WebSocket handlers if no implementation exists for a platform
void CALLBACK HttpPerformAsyncDefault(
    _In_ HCCallHandle /*call*/,
    _Inout_ XAsyncBlock* /*asyncBlock*/,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv /*env*/
) noexcept
{
    // Register a custom Http handler
    assert(false);
}

HRESULT CALLBACK WebSocketConnectAsyncDefault(
    _In_z_ const char* /*uri*/,
    _In_z_ const char* /*subProtocol*/,
    _In_ HCWebsocketHandle /*websocket*/,
    _Inout_ XAsyncBlock* /*asyncBlock*/,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv /*env*/
)
{
    // Register custom websocket handlers
    assert(false);
    return E_UNEXPECTED;
}

HRESULT CALLBACK WebSocketSendMessageAsyncDefault(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_z_ const char* /*message*/,
    _Inout_ XAsyncBlock* /*asyncBlock*/,
    _In_opt_ void* /*context*/
)
{
    // Register custom websocket handlers
    assert(false);
    return E_UNEXPECTED;
}

HRESULT CALLBACK WebSocketSendBinaryMessageAsyncDefault(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_reads_bytes_(payloadSize) const uint8_t* /*payloadBytes*/,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* /*asyncBlock*/,
    _In_opt_ void* /*context*/
)
{
    UNREFERENCED_PARAMETER(payloadSize);

    // Register custom websocket handlers
    assert(false);
    return E_UNEXPECTED;
}

HRESULT CALLBACK WebSocketDisconnectDefault(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_ HCWebSocketCloseStatus /*closeStatus*/,
    _In_opt_ void* /*context*/
)
{
    // Register custom websocket handlers
    assert(false);
    return E_UNEXPECTED;
}

HttpPerformInfo HC_PERFORM_ENV::GetPlatformDefaultHttpHandlers()
{
#if HC_UNITTEST_API
    return HttpPerformInfo{ HttpPerformAsyncDefault, nullptr };
#elif HC_PLATFORM == HC_PLATFORM_WIN32
    return HttpPerformInfo{ WinHttpProvider::HttpCallPerformAsyncHandler, nullptr };
#elif HC_PLATFORM == HC_PLATFORM_GDK
    return HttpPerformInfo{ CurlProvider::PerformAsyncHandler, nullptr };
#elif HC_PLATFORM == HC_PLATFORM_UWP || HC_PLATFORM == HC_PLATFORM_XDK
    return HttpPerformInfo{ xmlhttp_http_task::PerformAsyncHandler, nullptr };
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
    return HttpPerformInfo{ AndroidHttpCallPerformAsync, nullptr };
#elif HC_PLATFORM_IS_APPLE
    return HttpPerformInfo{ AppleHttpCallPerformAsync, nullptr };
#else
    return HttpPerformInfo{ HttpPerformAsyncDefault, nullptr };
#endif
}

#if !HC_NOWEBSOCKETS
WebSocketPerformInfo HC_PERFORM_ENV::GetPlatformDefaultWebSocketHandlers()
{
#if HC_UNITTEST_API
    return WebSocketPerformInfo{
        WebSocketConnectAsyncDefault,
        WebSocketSendMessageAsyncDefault,
        WebSocketSendBinaryMessageAsyncDefault,
        WebSocketDisconnectDefault,
        nullptr
    };
#elif HC_PLATFORM == HC_PLATFORM_WIN32
    // Use WinHttp WebSockets if available (Win 8+) and WebSocketpp otherwise
    auto webSocketExports = WinHttpProvider::GetWinHttpWebSocketExports();
    if (webSocketExports.completeUpgrade && webSocketExports.send && webSocketExports.receive &&
        webSocketExports.close && webSocketExports.queryCloseStatus && webSocketExports.shutdown)
    {
        return WebSocketPerformInfo{
            WinHttpProvider::WebSocketConnectAsyncHandler,
            WinHttpProvider::WebSocketSendAsyncHandler,
            WinHttpProvider::WebSocketSendBinaryAsyncHandler,
            WinHttpProvider::WebSocketDisconnectHandler,
            nullptr
        };
    }
    else
    {
        return WebSocketPerformInfo{
            WebSocketppConnectAsync,
            WebSocketppSendMessageAsync,
            WebSocketppSendBinaryMessageAsync,
            WebSocketppDisconnect,
            nullptr
        };
    }
#elif HC_PLATFORM == HC_PLATFORM_GDK
    return WebSocketPerformInfo{
        WinHttpProvider::WebSocketConnectAsyncHandler,
        WinHttpProvider::WebSocketSendAsyncHandler,
        WinHttpProvider::WebSocketSendBinaryAsyncHandler,
        WinHttpProvider::WebSocketDisconnectHandler,
        nullptr
    };
#elif HC_PLATFORM == HC_PLATFORM_UWP || HC_PLATFORM == HC_PLATFORM_XDK
    return WebSocketPerformInfo{
        WinRTWebSocketConnectAsync,
        WinRTWebSocketSendMessageAsync,
        WinRTWebSocketSendBinaryMessageAsync,
        WinRTWebSocketDisconnect,
        nullptr
    };
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
    return WebSocketPerformInfo{
        WebSocketppConnectAsync,
        WebSocketppSendMessageAsync,
        WebSocketppSendBinaryMessageAsync,
        WebSocketppDisconnect,
        nullptr
    };
#elif HC_PLATFORM_IS_APPLE
    return WebSocketPerformInfo{
        WebSocketppConnectAsync,
        WebSocketppSendMessageAsync,
        WebSocketppSendBinaryMessageAsync,
        WebSocketppDisconnect,
        nullptr
    };
#else
    return WebSocketPerformInfo{
        WebSocketConnectAsyncDefault,
        WebSocketSendMessageAsyncDefault,
        WebSocketSendBinaryMessageAsyncDefault,
        WebSocketDisconnectDefault,
        nullptr
    };
#endif
}
#endif // !HC_NOWEBSOCKETS

Result<HC_UNIQUE_PTR<HC_PERFORM_ENV>> HC_PERFORM_ENV::Initialize(HCInitArgs* args) noexcept
{
    http_stl_allocator<HC_PERFORM_ENV> a{};
    HC_UNIQUE_PTR<HC_PERFORM_ENV> performEnv{ new (a.allocate(1)) HC_PERFORM_ENV };

#if HC_PLATFORM == HC_PLATFORM_WIN32 && !HC_UNITTEST_API
    RETURN_HR_IF(E_INVALIDARG, args);

    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);
 
    performEnv->winHttpProvider = initWinHttpResult.ExtractPayload();

#elif HC_PLATFORM == HC_PLATFORM_GDK
    RETURN_HR_IF(E_INVALIDARG, args);

    auto initCurlResult = CurlProvider::Initialize();
    RETURN_IF_FAILED(initCurlResult.hr);
    performEnv->curlProvider = initCurlResult.ExtractPayload();

#if !HC_NOWEBSOCKETS
    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);
    performEnv->winHttpProvider = initWinHttpResult.ExtractPayload();
#endif

#elif HC_PLATFORM == HC_PLATFORM_ANDROID
    auto initAndroidResult = AndroidPlatformContext::Initialize(args);
    RETURN_IF_FAILED(initAndroidResult.hr);
    performEnv->androidPlatformContext = initAndroidResult.ExtractPayload();

#else
    RETURN_HR_IF(E_INVALIDARG, args);
#endif

    return std::move(performEnv);
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

    HC_PERFORM_ENV* env{};
    HCCallHandle const callHandle{};
    XAsyncBlock* const clientAsyncBlock{};
    XAsyncBlock internalAsyncBlock{};
};

HRESULT HC_PERFORM_ENV::HttpCallPerformAsyncShim(HCCallHandle call, XAsyncBlock* async)
{
    auto performContext = http_allocate_shared<HttpPerformContext>(this, call, async);
    {
        std::unique_lock<std::mutex> lock{ m_mutex };
        m_activeHttpRequests[call->id] = performContext;
    }
    return XAsyncBegin(async, performContext.get(), nullptr, __FUNCTION__, HttpPerformAsyncShimProvider);
}

HRESULT CALLBACK HC_PERFORM_ENV::HttpPerformAsyncShimProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    HttpPerformContext* performContext{ static_cast<HttpPerformContext*>(data->context) };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        XTaskQueuePortHandle workPort{};
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &performContext->internalAsyncBlock.queue));

        return performContext->callHandle->PerformAsync(&performContext->internalAsyncBlock);
    }
    case XAsyncOp::Cancel:
    {
        XAsyncCancel(&performContext->internalAsyncBlock);
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
    HC_PERFORM_ENV* env{ performContext->env };

    XAsyncComplete(performContext->clientAsyncBlock, XAsyncGetStatus(async, false), 0);

    std::unique_lock<std::mutex> lock{ env->m_mutex };
    env->m_activeHttpRequests.erase(performContext->callHandle->id);
    bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();
    lock.unlock();

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

struct HC_PERFORM_ENV::WebSocketContext
{
    WebSocketContext(
        HC_PERFORM_ENV* _env,
        http_internal_string&& _uri,
        http_internal_string&& _subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* _clientAsyncBlock
    ) : env{ _env },
        uri{ std::move(_uri) },
        subprotocol{ std::move(_subprotocol) },
        websocketObserver{ HC_WEBSOCKET_OBSERVER::Initialize(websocketHandle->websocket, nullptr, nullptr, nullptr, HC_PERFORM_ENV::WebSocketClosed, this) },
        clientConnectAsyncBlock{ _clientAsyncBlock },
        internalConnectAsyncBlock{ nullptr, this, HC_PERFORM_ENV::WebSocketConnectComplete }
    {
    }

    ~WebSocketContext()
    {
        if (internalConnectAsyncBlock.queue)
        {
            XTaskQueueCloseHandle(internalConnectAsyncBlock.queue);
        }
    }

    HC_PERFORM_ENV* const env{};
    http_internal_string uri;
    http_internal_string subprotocol;  
    HC_UNIQUE_PTR<HC_WEBSOCKET_OBSERVER> const websocketObserver;
    XAsyncBlock* const clientConnectAsyncBlock;
    XAsyncBlock internalConnectAsyncBlock{};
    WebSocketCompletionResult connectResult{};
};

HRESULT HC_PERFORM_ENV::WebSocketConnectAsyncShim(
    _In_ http_internal_string&& uri,
    _In_ http_internal_string&& subprotocol,
    _In_ HCWebsocketHandle clientWebSocketHandle,
    _Inout_ XAsyncBlock* asyncBlock
)
{
    auto context = http_allocate_shared<WebSocketContext>(this, std::move(uri), std::move(subprotocol), clientWebSocketHandle, asyncBlock);
    {
        std::unique_lock<std::mutex> lock{ m_mutex };
        m_connectingWebSockets[clientWebSocketHandle->websocket->id] = context;
    }
    return XAsyncBegin(asyncBlock, context.get(), HCWebSocketConnectAsync, nullptr, WebSocketConnectAsyncShimProvider);
}

HRESULT CALLBACK HC_PERFORM_ENV::WebSocketConnectAsyncShimProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    WebSocketContext* context{ static_cast<WebSocketContext*>(data->context) };

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        XTaskQueuePortHandle workPort{};
        RETURN_IF_FAILED(XTaskQueueGetPort(data->async->queue, XTaskQueuePort::Work, &workPort));
        RETURN_IF_FAILED(XTaskQueueCreateComposite(workPort, workPort, &context->internalConnectAsyncBlock.queue));

        return context->websocketObserver->websocket->ConnectAsync(std::move(context->uri), std::move(context->subprotocol), &context->internalConnectAsyncBlock);
    }
    case XAsyncOp::GetResult:
    {
        WebSocketCompletionResult* result{ reinterpret_cast<WebSocketCompletionResult*>(data->buffer) };
        *result = context->connectResult;
        return S_OK;
    }
    case XAsyncOp::Cleanup:
    {
        HC_PERFORM_ENV* env{ context->env };
        uint64_t id = context->websocketObserver->websocket->id;
        env->m_connectingWebSockets.erase(id);
        env->m_pendingGetConnectResultWebSockets.erase(id);
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
    WebSocketContext* context{ static_cast<WebSocketContext*>(async->context) };
    HC_PERFORM_ENV* env{ context->env };
    uint64_t id = context->websocketObserver->websocket->id;

    std::unique_lock<std::mutex> lock{ env->m_mutex };

    std::shared_ptr<WebSocketContext> sharedContext = env->m_connectingWebSockets[id];
    assert(sharedContext);

    env->m_connectingWebSockets.erase(id);

    // If cleanup is pending, we may need to immediately disconnect or schedule ProviderCleanup depending on the result of the connect operation
    bool disconnect{ false };

    HRESULT hr = HCGetWebSocketConnectResult(&context->internalConnectAsyncBlock, &context->connectResult);
    if (SUCCEEDED(hr))
    {
        env->m_pendingGetConnectResultWebSockets[id] = sharedContext;
        if (SUCCEEDED(context->connectResult.errorCode))
        {
            env->m_connectedWebSockets[id] = sharedContext;
            if (env->m_cleanupAsyncBlock)
            {
                disconnect = true;
            }
        }
    }
    
    bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();
    assert(!disconnect || !scheduleProviderCleanup);

    lock.unlock();
    
    XAsyncComplete(context->clientConnectAsyncBlock, hr, sizeof(WebSocketCompletionResult));

    if (disconnect)
    {
        hr = context->websocketObserver->websocket->Disconnect();
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
}

void CALLBACK HC_PERFORM_ENV::WebSocketClosed(HCWebsocketHandle /*websocket*/, HCWebSocketCloseStatus /*closeStatus*/, void* c)
{
    WebSocketContext* context{ static_cast<WebSocketContext*>(c) };
    HC_PERFORM_ENV* env{ context->env };

    std::unique_lock<std::mutex> lock{ env->m_mutex };
    env->m_connectedWebSockets.erase(context->websocketObserver->websocket->id);
    bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();
    lock.unlock();

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

HRESULT HC_PERFORM_ENV::CleanupAsync(HC_UNIQUE_PTR<HC_PERFORM_ENV>&& env, XAsyncBlock* async) noexcept
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
        bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();

        for (auto& activeRequest : env->m_activeHttpRequests)
        {
            XAsyncCancel(&activeRequest.second->internalAsyncBlock);
        }
        for (auto& connection : env->m_connectedWebSockets)
        {
            HRESULT hr = connection.second->websocketObserver->websocket->Disconnect();
            if (FAILED(hr))
            {
                HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WebSocket::Disconnect failed during HCCleanup");
            }
        }
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

#if HC_PLATFORM == HC_PLATFORM_GDK
    auto curlCleanupAsyncBlock = http_allocate_unique<XAsyncBlock>();
    curlCleanupAsyncBlock->queue = data->async->queue;
    curlCleanupAsyncBlock->context = data->async;
    curlCleanupAsyncBlock->callback = [](XAsyncBlock* async)
    {
        HC_UNIQUE_PTR<XAsyncBlock> curlCleanupAsyncBlock{ async };
        XAsyncBlock* envCleanupAsyncBlock = static_cast<XAsyncBlock*>(curlCleanupAsyncBlock->context);

        HRESULT cleanupResult = XAsyncGetStatus(curlCleanupAsyncBlock.get(), false);
        curlCleanupAsyncBlock.reset();

        // HC_PERFORM_ENV fully cleaned up at this point
        XAsyncComplete(envCleanupAsyncBlock, cleanupResult, 0);
    };

    auto curlProvider = std::move(env->curlProvider);

    HC_UNIQUE_PTR<HC_PERFORM_ENV> reclaim{ env };
    reclaim.reset();

    RETURN_IF_FAILED(CurlProvider::CleanupAsync(std::move(curlProvider), curlCleanupAsyncBlock.get()));
    curlCleanupAsyncBlock.release();
    return E_PENDING;
#else
    // No additional provider cleanup needed
    env.reset();
    XAsyncComplete(cleanupAsyncBlock, S_OK, 0);
#endif
}

bool HC_PERFORM_ENV::CanScheduleProviderCleanup()
{
    // mutex should always be held when calling this method
    assert(!m_mutex.try_lock());
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
    return true;
}
