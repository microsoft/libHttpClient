#pragma once

#include <winhttp.h>

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XNetworking.h>
#include <appnotify.h>
#endif

#include "Platform/IHttpProvider.h"
#include "Platform/IWebSocketProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class WinHttpConnection;

using WinHttpWebSocketCompleteUpgradeExport = HINTERNET(WINAPI*)(HINTERNET, DWORD_PTR);
using WinHttpWebSocketSendExport = DWORD(WINAPI*)(HINTERNET, UINT, PVOID, DWORD);
using WinHttpWebSocketReceiveExport = DWORD(WINAPI*)(HINTERNET, PVOID, DWORD, DWORD*, UINT*);
using WinHttpWebSocketCloseExport = DWORD(WINAPI*)(HINTERNET, USHORT, PVOID, DWORD);
using WinHttpWebSocketQueryCloseStatusExport = DWORD(WINAPI*)(HINTERNET, USHORT*, PVOID, DWORD, DWORD*);
using WinHttpWebSocketShutdownExport = DWORD(WINAPI*)(HINTERNET, USHORT, PVOID, DWORD);

struct WinHttpWebSocketExports
{
    HMODULE winHttpModule{ nullptr };
    WinHttpWebSocketCompleteUpgradeExport completeUpgrade{ nullptr };
    WinHttpWebSocketSendExport send{ nullptr };
    WinHttpWebSocketReceiveExport receive{ nullptr };
    WinHttpWebSocketCloseExport close{ nullptr };
    WinHttpWebSocketQueryCloseStatusExport queryCloseStatus{ nullptr };
    WinHttpWebSocketShutdownExport shutdown{ nullptr };
};

struct XPlatSecurityInformation
{
    XPlatSecurityInformation() = default;
    XPlatSecurityInformation(const XPlatSecurityInformation&) = delete;
    XPlatSecurityInformation(XPlatSecurityInformation&&) = default;
    XPlatSecurityInformation& operator=(const XPlatSecurityInformation&) = delete;
    XPlatSecurityInformation& operator=(XPlatSecurityInformation&&) = default;
    ~XPlatSecurityInformation() = default;

#if HC_PLATFORM == HC_PLATFORM_GDK
    http_internal_vector<uint8_t> buffer;
    XNetworkingSecurityInformation* securityInformation{ nullptr };
#endif
    uint32_t enabledHttpSecurityProtocolFlags { 0 };
    XPlatSecurityInformation(uint32_t flags)
    {
        enabledHttpSecurityProtocolFlags = flags;
    }
};

class WinHttpProvider
{
public:
    static Result<HC_UNIQUE_PTR<WinHttpProvider>> Initialize();
    WinHttpProvider(const WinHttpProvider&) = delete;
    WinHttpProvider(WinHttpProvider&&) = delete;
    WinHttpProvider& operator=(const WinHttpProvider&) = delete;
    WinHttpProvider& operator=(WinHttpProvider&&) = delete;
    virtual ~WinHttpProvider();

    static WinHttpWebSocketExports GetWinHttpWebSocketExports();

public: // IHttpProvider
    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept;

    HRESULT SetGlobalProxy(
        _In_ String const& proxyUri
    ) noexcept;

#ifndef HC_NOWEBSOCKETS
public: // IWebSocketProvider
    HRESULT ConnectAsync(
        String const& uri,
        String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async
    ) noexcept;

    HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async
    ) noexcept;

    HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* asyncBlock
    ) noexcept;

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept;
#endif

private:
    WinHttpProvider() = default;

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

class WinHttp_HttpProvider : public IHttpProvider
{
public:
    WinHttp_HttpProvider(std::shared_ptr<WinHttpProvider> provider);

    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept override;

    SharedPtr<WinHttpProvider> const WinHttpProvider;
};

#ifndef HC_NOWEBSOCKETS
class WinHttp_WebSocketProvider : public IWebSocketProvider
{
public:
    WinHttp_WebSocketProvider(std::shared_ptr<WinHttpProvider> provider);

    HRESULT ConnectAsync(
        String const& uri,
        String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* asyncBlock
    ) noexcept override;

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept override;

    SharedPtr<WinHttpProvider> const WinHttpProvider;
};
#endif



NAMESPACE_XBOX_HTTP_CLIENT_END
