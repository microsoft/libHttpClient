#pragma once

#include "Platform/IHttpProvider.h"
#include "Platform/IWebSocketProvider.h"

// Global manager of Network operations. Formerly passed as context to default WebSocket and Http providers.
// Likely will rename and refactor in near future.
struct HC_PERFORM_ENV
{
public:
    HC_PERFORM_ENV(const HC_PERFORM_ENV&) = delete;
    HC_PERFORM_ENV(HC_PERFORM_ENV&&) = delete;
    HC_PERFORM_ENV& operator=(const HC_PERFORM_ENV&) = delete;
    virtual ~HC_PERFORM_ENV() = default;

    // Called during HCInitialize
    static Result<HC_UNIQUE_PTR<HC_PERFORM_ENV>> Initialize(
        HC_UNIQUE_PTR<xbox::httpclient::IHttpProvider> httpProvider,
        HC_UNIQUE_PTR<xbox::httpclient::IWebSocketProvider> webSocketProvider
    ) noexcept;

    xbox::httpclient::IHttpProvider& HttpProvider();
    xbox::httpclient::IWebSocketProvider& WebSocketProvider();

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

private:
    HC_PERFORM_ENV(HC_UNIQUE_PTR<xbox::httpclient::IHttpProvider> httpProvider, HC_UNIQUE_PTR<xbox::httpclient::IWebSocketProvider> webSocketProvider);

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
    bool ShouldScheduleProviderCleanup();

    std::mutex m_mutex;

    HC_UNIQUE_PTR<xbox::httpclient::IHttpProvider> const m_httpProvider;
    HC_UNIQUE_PTR<xbox::httpclient::IWebSocketProvider> const m_webSocketProvider;

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
