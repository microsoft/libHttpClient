#pragma once

#include <winhttp.h>
#include "winhttp_connection.h"

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XNetworking.h>
#include <appnotify.h>
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class WinHttpProvider
{
public:
    static Result<std::shared_ptr<WinHttpProvider>> Initialize();
    WinHttpProvider(const WinHttpProvider&) = delete;
    WinHttpProvider(WinHttpProvider&&) = delete;
    WinHttpProvider& operator=(const WinHttpProvider&) = delete;
    WinHttpProvider& operator=(WinHttpProvider&&) = delete;
    virtual ~WinHttpProvider();

    // Http provider entry point
    static void CALLBACK HttpCallPerformAsyncHandler(
        HCCallHandle callHandle,
        XAsyncBlock* async,
        void* context,
        HCPerformEnv env
    ) noexcept;

    static WinHttpWebSocketExports GetWinHttpWebSocketExports();

    // WebSocket provider entry points
    static HRESULT CALLBACK WebSocketConnectAsyncHandler(
        const char* uri,
        const char* subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async,
        void* context,
        HCPerformEnv env
    ) noexcept;

    static HRESULT CALLBACK WebSocketSendAsyncHandler(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async,
        void* context
    ) noexcept;

    static HRESULT CALLBACK WebSocketSendBinaryAsyncHandler(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* asyncBlock,
        void* context
    ) noexcept;

    static HRESULT CALLBACK WebSocketDisconnectHandler(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus,
        void* context
    ) noexcept;

    // Sets Global proxy for all HttpConnections
    HRESULT SetGlobalProxy(_In_ const char* proxyUri) noexcept;

private:
    WinHttpProvider() = default;

    HRESULT HttpCallPerformAsync(HCCallHandle callHandle, XAsyncBlock* async) noexcept;
    HRESULT WebSocketConnectAsync(const char* uri, const char* subprotocol, HCWebsocketHandle websocketHandle, XAsyncBlock* async) noexcept;

    HRESULT CloseAllConnections();

    Result<XPlatSecurityInformation> GetSecurityInformation(const char* url);
    Result<HINTERNET> GetHSession(uint32_t securityProtolFlags);

    static HRESULT SetGlobalProxyForHSession(HINTERNET hSession, const char* proxyUri);
    static HRESULT GetProxyName(_In_ proxy_type proxyType, _In_ Uri proxyUri, _Out_ DWORD& pAccessType, _Out_ http_internal_wstring& pwProxyName);

    XTaskQueueHandle m_immediateQueue{ nullptr };
    xbox::httpclient::proxy_type m_proxyType = xbox::httpclient::proxy_type::automatic_proxy;
    http_internal_string m_globalProxy;
    std::mutex m_lock;

    // Maintain a WinHttpSession for each unique security protocol flags
    http_internal_map<uint32_t, HINTERNET> m_hSessions;

    // Track WinHttpConnections so that we can close them on shutdown/suspend
    http_internal_list<std::weak_ptr<WinHttpConnection>> m_connections;

#if HC_PLATFORM == HC_PLATFORM_GDK
public: // For testing purposes only
    void Suspend();
    void Resume();

private:
    static void CALLBACK NetworkConnectivityChangedCallback(void* context, const XNetworkingConnectivityHint* hint);
    static void CALLBACK AppStateChangedCallback(BOOLEAN isSuspended, void* context);

    bool m_networkInitialized{ false };
    bool m_isSuspended{ false };
    XTaskQueueRegistrationToken m_networkConnectivityChangedToken{ 0 };
    PAPPSTATE_REGISTRATION m_appStateChangedToken{ nullptr };
#endif
};

NAMESPACE_XBOX_HTTP_CLIENT_END
