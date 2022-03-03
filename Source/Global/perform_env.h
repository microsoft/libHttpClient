#pragma once

#if HC_PLATFORM == HC_PLATFORM_WIN32 
#include "WinHttp/winhttp_provider.h"
#elif HC_PLATFORM == HC_PLATFORM_GDK
#include "Curl/CurlProvider.h"
#include "WinHttp/winhttp_provider.h"
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
#include "HTTP/Android/android_platform_context.h"
#endif

struct HttpPerformInfo
{
    HttpPerformInfo() = default;
    HttpPerformInfo(_In_ HCCallPerformFunction h, _In_opt_ void* ctx)
        : handler(h), context(ctx)
    { }
    HCCallPerformFunction handler = nullptr;
    void* context = nullptr; // non owning
};

#if !HC_NOWEBSOCKETS
struct WebSocketPerformInfo
{
    WebSocketPerformInfo(
        _In_ HCWebSocketConnectFunction conn,
        _In_ HCWebSocketSendMessageFunction st,
        _In_ HCWebSocketSendBinaryMessageFunction sb,
        _In_ HCWebSocketDisconnectFunction dc,
        _In_opt_ void* ctx
    ) :
        connect{ conn },
        sendText{ st },
        sendBinary{ sb },
        disconnect{ dc },
        context{ ctx }
    {}

    HCWebSocketConnectFunction connect = nullptr;
    HCWebSocketSendMessageFunction sendText = nullptr;
    HCWebSocketSendBinaryMessageFunction sendBinary = nullptr;
    HCWebSocketDisconnectFunction disconnect = nullptr;
    void* context = nullptr;
};
#endif

// Global context passed to HTTP/WebSocket hooks. Will be opaque to client providers, but contains needed context for default providers.
struct HC_PERFORM_ENV
{
public:
    HC_PERFORM_ENV(const HC_PERFORM_ENV&) = delete;
    HC_PERFORM_ENV(HC_PERFORM_ENV&&) = delete;
    HC_PERFORM_ENV& operator=(const HC_PERFORM_ENV&) = delete;
    virtual ~HC_PERFORM_ENV() = default;

    // Called during static initialization to get default hooks for the platform. These can be overridden by clients with
    // HCSetHttpCallPerformFunction and HCSetWebSocketFunctions
    static HttpPerformInfo GetPlatformDefaultHttpHandlers();
#if !HC_NOWEBSOCKETS
    static WebSocketPerformInfo GetPlatformDefaultWebSocketHandlers();
#endif

    // Called during HCInitialize. HC_PERFORM_ENV will be passed to Http/WebSocket hooks and is needed by default providers
    static Result<HC_UNIQUE_PTR<HC_PERFORM_ENV>> Initialize(HCInitArgs* args) noexcept;

    HRESULT HttpCallPerformAsyncShim(HCCallHandle call, XAsyncBlock* async);

#if !HC_NOWEBSOCKETS
    HRESULT WebSocketConnectAsyncShim(
        _In_ http_internal_string&& uri,
        _In_ http_internal_string&& subProtocol,
        _In_ HCWebsocketHandle handle,
        _Inout_ XAsyncBlock* asyncBlock
    );
#endif

    static HRESULT CleanupAsync(HC_UNIQUE_PTR<HC_PERFORM_ENV> env, XAsyncBlock* async) noexcept;

    // Default Provider State
#if HC_PLATFORM == HC_PLATFORM_WIN32
    std::shared_ptr<xbox::httpclient::WinHttpProvider> winHttpProvider;
#elif HC_PLATFORM == HC_PLATFORM_GDK
    HC_UNIQUE_PTR<xbox::httpclient::CurlProvider> curlProvider;
    std::shared_ptr<xbox::httpclient::WinHttpProvider> winHttpProvider;
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
    std::shared_ptr<AndroidPlatformContext> androidPlatformContext;
#endif
private:
    HC_PERFORM_ENV() = default;

    static HRESULT CALLBACK HttpPerformAsyncShimProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK HttpPerformComplete(XAsyncBlock* async);

#if !HC_NOWEBSOCKETS
    static HRESULT CALLBACK WebSocketConnectAsyncShimProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK WebSocketConnectComplete(XAsyncBlock* async);
    static void CALLBACK WebSocketClosed(HCWebsocketHandle websocket, HCWebSocketCloseStatus closeStatus, void* context);
#endif

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);  
    static void CALLBACK ProviderCleanup(void* context, bool canceled);
    static void CALLBACK ProviderCleanupComplete(XAsyncBlock* async);

    bool CanScheduleProviderCleanup();

    std::mutex m_mutex;

    struct HttpPerformContext;
    http_internal_set<XAsyncBlock*> m_activeHttpRequests;

#if !HC_NOWEBSOCKETS
    struct WebSocketConnectContext;
    struct ActiveWebSocketContext;
    http_internal_set<XAsyncBlock*> m_connectingWebSockets;
    http_internal_set<ActiveWebSocketContext*> m_connectedWebSockets;
#endif

    XAsyncBlock* m_cleanupAsyncBlock{ nullptr }; // non-owning
};

using PerformEnv = HC_UNIQUE_PTR<HC_PERFORM_ENV>;
