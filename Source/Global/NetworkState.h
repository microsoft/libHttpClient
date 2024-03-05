#pragma once

#include "HTTP/httpcall.h"
#include "Platform/IHttpProvider.h"
#if !HC_NOWEBSOCKETS
#include "WebSocket/hcwebsocket.h"
#include "Platform/IWebSocketProvider.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// Global state related to network operations.
//
// Responsible for tracking ongoing network operations and awaiting them during cleanup. It also owns the IHttpProvider
// and IWebSocketProvider that will be used to perform HttpCalls and WebSockets operations.

class NetworkState
{
public:
    NetworkState(NetworkState const&) = delete;
    NetworkState& operator=(NetworkState const&) = delete;
    ~NetworkState() = default;

    // Lifecycle management
#if !HC_NOWEBSOCKETS
    static Result<UniquePtr<NetworkState>> Initialize(
        UniquePtr<IHttpProvider> httpProvider,
        UniquePtr<IWebSocketProvider> webSocketProvider
    ) noexcept;
#else
    static Result<UniquePtr<NetworkState>> Initialize(
        UniquePtr<IHttpProvider> httpProvider
    ) noexcept;
#endif

    static HRESULT CleanupAsync(
        UniquePtr<NetworkState> networkManager,
        XAsyncBlock* async
    ) noexcept;

public: // Http
    IHttpProvider& HttpProvider() noexcept;

    Result<UniquePtr<HC_CALL>> HttpCallCreate() noexcept;

    HRESULT HttpCallPerformAsync(
        HCCallHandle httpCall,
        XAsyncBlock* async
    ) noexcept;

#if !HC_NOWEBSOCKETS
public: // WebSocket
    IWebSocketProvider& WebSocketProvider() noexcept;

    Result<SharedPtr<WebSocket>> WebSocketCreate() noexcept;

    HRESULT WebSocketConnectAsync(
        String&& uri,
        String&& subProtocol,
        HCWebsocketHandle handle,
        XAsyncBlock* asyncBlock
    ) noexcept;
#endif

private:
#if !HC_NOWEBSOCKETS
    NetworkState(UniquePtr<IHttpProvider> httpProvider, UniquePtr<IWebSocketProvider> webSocketProvider) noexcept;
#else
    NetworkState(UniquePtr<IHttpProvider> httpProvider) noexcept;
#endif

    static HRESULT CALLBACK HttpCallPerformAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK HttpCallPerformComplete(XAsyncBlock* async);

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK HttpProviderCleanupComplete(XAsyncBlock* async);
    bool ScheduleCleanup();

    std::mutex m_mutex;

    UniquePtr<IHttpProvider> m_httpProvider;

    struct HttpPerformContext;

    Set<XAsyncBlock*> m_activeHttpRequests;
    XAsyncBlock* m_cleanupAsyncBlock{ nullptr }; // non-owning

#if !HC_NOWEBSOCKETS
    static HRESULT CALLBACK WebSocketConnectAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK WebSocketConnectComplete(XAsyncBlock* async);
    static void CALLBACK WebSocketClosed(HCWebsocketHandle websocket, HCWebSocketCloseStatus closeStatus, void* context);

    UniquePtr<IWebSocketProvider> m_webSocketProvider;

    struct WebSocketConnectContext;
    struct ActiveWebSocketContext;

    Set<XAsyncBlock*> m_connectingWebSockets;
    Set<ActiveWebSocketContext*> m_connectedWebSockets;
#endif

};

NAMESPACE_XBOX_HTTP_CLIENT_END
