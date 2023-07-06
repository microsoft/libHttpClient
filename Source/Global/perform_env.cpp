#include "pch.h"
#include "perform_env.h"
#include "httpcall.h"

#if HC_PLATFORM == HC_PLATFORM_WIN32
// nothing
#elif HC_PLATFORM == HC_PLATFORM_UWP || HC_PLATFORM == HC_PLATFORM_XDK
#include "HTTP/XMLHttp/xmlhttp_http_task.h"
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
#include "HTTP/Android/android_http_request.h"
#elif HC_PLATFORM_IS_APPLE
#include "HTTP/Apple/http_apple.h"
#endif

#if !HC_NOWEBSOCKETS
#include "../WebSocket/hcwebsocket.h"

#if HC_PLATFORM == HC_PLATFORM_WIN32
#include "WebSocket/Websocketpp/websocketpp_websocket.h"
#elif HC_PLATFORM == HC_PLATFORM_UWP || HC_PLATFORM == HC_PLATFORM_XDK
#include "WebSocket/WinRT/winrt_websocket.h"
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
#include "WebSocket/Android/okhttp_websocket.h"
#elif HC_PLATFORM_IS_APPLE
#include "WebSocket/Websocketpp/websocketpp_websocket.h"
#endif

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
        OkHttpWebSocketConnectAsync,
        OkHttpWebSocketSendMessageAsync,
        OkHttpWebSocketSendBinaryMessageAsync,
        OkHttpWebSocketDisconnect,
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
        bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();
        lock.unlock();

        // Free performContext before scheduling ProviderCleanup to ensure it happens before returing to client
        HC_UNIQUE_PTR<HttpPerformContext> reclaim{ performContext };
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
    HC_UNIQUE_PTR<HC_WEBSOCKET_OBSERVER> websocketObserver;
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

    bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();
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
    bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();
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
        bool scheduleProviderCleanup = env->CanScheduleProviderCleanup();

#if !HC_NOWEBSOCKETS
        HC_TRACE_VERBOSE(HTTPCLIENT, "HC_PERFORM_ENV::CleanupAsyncProvider::Begin: HTTP active=%llu, WebSocket Connecting=%llu, WebSocket Connected=%llu", env->m_activeHttpRequests.size(), env->m_connectingWebSockets.size(), env->m_connectedWebSockets.size());
#endif
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
    HC_TRACE_VERBOSE(HTTPCLIENT, "HC_PERFORM_ENV::ProviderCleanup");

    HC_UNIQUE_PTR<HC_PERFORM_ENV> env{ static_cast<HC_PERFORM_ENV*>(context) };
    XAsyncBlock* cleanupAsyncBlock{ env->m_cleanupAsyncBlock };

#if HC_PLATFORM == HC_PLATFORM_GDK
    HC_UNIQUE_PTR<XAsyncBlock> curlCleanupAsyncBlock{ new (http_stl_allocator<XAsyncBlock>{}.allocate(1)) XAsyncBlock
    {
        cleanupAsyncBlock->queue,
        cleanupAsyncBlock,
        ProviderCleanupComplete
    }};

    auto curlProvider = std::move(env->curlProvider);
    env.reset();

    HRESULT hr = CurlProvider::CleanupAsync(std::move(curlProvider), curlCleanupAsyncBlock.get());
    if (FAILED(hr))
    {
        XAsyncComplete(cleanupAsyncBlock, hr, 0);
    }
    else
    {
        curlCleanupAsyncBlock.release();
    }
#else
    // No additional provider cleanup needed
    env.reset();
    XAsyncComplete(cleanupAsyncBlock, S_OK, 0);
#endif
}

void CALLBACK HC_PERFORM_ENV::ProviderCleanupComplete(XAsyncBlock* async)
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    HC_UNIQUE_PTR<XAsyncBlock> curlCleanupAsyncBlock{ async };
    XAsyncBlock* envCleanupAsyncBlock = static_cast<XAsyncBlock*>(curlCleanupAsyncBlock->context);

    HRESULT cleanupResult = XAsyncGetStatus(curlCleanupAsyncBlock.get(), false);
    curlCleanupAsyncBlock.reset();

    // HC_PERFORM_ENV fully cleaned up at this point
    XAsyncComplete(envCleanupAsyncBlock, cleanupResult, 0);
#else
    UNREFERENCED_PARAMETER(async);
    assert(false);
#endif
}

bool HC_PERFORM_ENV::CanScheduleProviderCleanup()
{
    if (!m_cleanupAsyncBlock)
    {
        // HC_PERFORM_ENV::CleanupAsync has not yet been called
        return false;
    }

#if !HC_NOWEBSOCKETS
    HC_TRACE_VERBOSE(HTTPCLIENT, "HC_PERFORM_ENV::Cleaning up, HTTP=%llu, WebSocket Connecting=%llu, WebSocket Connected=%llu", m_activeHttpRequests.size(), m_connectingWebSockets.size(), m_connectedWebSockets.size());
#endif
    if (!m_activeHttpRequests.empty())
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
