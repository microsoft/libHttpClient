#include "pch.h"
#include "perform_env.h"

#if HC_PLATFORM == HC_PLATFORM_WIN32
#include "WebSocket/Websocketpp/websocketpp_websocket.h"
#elif HC_PLATFORM == HC_PLATFORM_UWP || HC_PLATFORM == HC_PLATFORM_XDK
#include "HTTP/XMLHttp/xmlhttp_http_task.h"
#include "WebSocket/WinRT/winrt_websocket.h"
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
#include "HTTP/Android/android_http_request.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"
#elif HC_PLATFORM_IS_APPLE
#include "Source/HTTP/Apple/http_apple.h"
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
#if HC_PLATFORM != HC_PLATFORM_IOS
    http_stl_allocator<HC_PERFORM_ENV> a{};
#endif
    HC_UNIQUE_PTR<HC_PERFORM_ENV> performEnv{ nullptr };

#if HC_PLATFORM == HC_PLATFORM_WIN32 && !HC_UNITTEST_API
    RETURN_HR_IF(E_INVALIDARG, args);

    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);

    performEnv.reset(new (a.allocate(1)) HC_PERFORM_ENV);
    performEnv->winHttpProvider = initWinHttpResult.ExtractPayload();

#elif HC_PLATFORM == HC_PLATFORM_GDK
    RETURN_HR_IF(E_INVALIDARG, args);

    performEnv.reset(new (a.allocate(1)) HC_PERFORM_ENV);

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

    performEnv.reset(new (a.allocate(1)) HC_PERFORM_ENV);
    performEnv->androidPlatformContext = initAndroidResult.ExtractPayload();

#else
    RETURN_HR_IF(E_INVALIDARG, args);
#endif

    return std::move(performEnv);
}
