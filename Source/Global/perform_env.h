#pragma once

#include "HTTP/httpcall.h"
#include "WebSocket/hcwebsocket.h"
#include "Platform/IHttpProvider.h"
#include "Platform/IWebSocketProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// Global state related to network operations.
// 
// Responsible for tracking ongoing network operations and awaiting them during cleanup. It also owns the IHttpProvider
// and IWebSocketProvider that will be used to perform HttpCalls and WebSockets operations.

class NetworkState
{
public: // Lifecycle management
    static Result<UniquePtr<NetworkState>> Initialize(
        UniquePtr<IHttpProvider> httpProvider,
        UniquePtr<IWebSocketProvider> webSocketProvider
    ) noexcept;

    static HRESULT CleanupAsync(
        UniquePtr<NetworkState> networkManager,
        XAsyncBlock* async
    ) noexcept;

public: // Providers
    IHttpProvider& HttpProvider() noexcept;
    IWebSocketProvider& WebSocketProvider() noexcept;

public: // Http
    Result<UniquePtr<HC_CALL>> HttpCallCreate() noexcept;

    HRESULT HttpCallPerformAsync(
        HCCallHandle httpCall,
        XAsyncBlock* async
    ) noexcept;

public: // WebSocket
    Result<SharedPtr<WebSocket>> WebSocketCreate() noexcept;

    HRESULT WebSocketConnectAsync(
        String&& uri,
        String&& subProtocol,
        HCWebsocketHandle handle,
        XAsyncBlock* asyncBlock
    ) noexcept;

    NetworkState(NetworkState const&) = delete;
    NetworkState& operator=(NetworkState const&) = delete;
    ~NetworkState();

private:
    NetworkState(
        UniquePtr<IHttpProvider> httpProvider,
        UniquePtr<IWebSocketProvider> webSocketProvider
    ) noexcept;

    static HRESULT CALLBACK HttpCallPerformAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK HttpCallPerformComplete(XAsyncBlock* async);

    static HRESULT CALLBACK WebSocketConnectAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK WebSocketConnectComplete(XAsyncBlock* async);
    static void CALLBACK WebSocketClosed(HCWebsocketHandle websocket, HCWebSocketCloseStatus closeStatus, void* context);

    static HRESULT CALLBACK CleanupAsyncProvider(XAsyncOp op, const XAsyncProviderData* data);
    static void CALLBACK HttpProviderCleanupComplete(XAsyncBlock* async);
    bool ScheduleCleanup();

    std::mutex m_mutex;

    UniquePtr<IHttpProvider> m_httpProvider;
    UniquePtr<IWebSocketProvider> m_webSocketProvider;

    // XAsync context objects
    struct HttpPerformContext;
    struct WebSocketConnectContext;
    struct ActiveWebSocketContext;

    Set<XAsyncBlock*> m_activeHttpRequests;
    Set<XAsyncBlock*> m_connectingWebSockets;
    Set<ActiveWebSocketContext*> m_connectedWebSockets;
    XAsyncBlock* m_cleanupAsyncBlock{ nullptr }; // non-owning
};

NAMESPACE_XBOX_HTTP_CLIENT_END
