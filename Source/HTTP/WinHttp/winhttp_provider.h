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

    // Global cap on the number of HTTP requests allowed to be in flight against WinHTTP at once.
    // Requests beyond the cap are queued and started as earlier requests complete, so callers may
    // enqueue as many as they like. The limit itself is process-wide state owned by global.h
    // (xbox::httpclient::GetGlobalRequestLimit), since it may be set before the provider exists.

    HRESULT SetGlobalProxy(
        _In_ String const& proxyUri
    ) noexcept;

    // Public helper for building a proxy name (host[:port]) used by tests and implementation.
    static http_internal_wstring BuildNamedProxyString(_In_ const xbox::httpclient::Uri& proxyUri);

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

    // timeoutMs bounds the wait for connections to report closed. Suspend passes a short budget so
    // a stuck connection cannot trigger the PLM watchdog; shutdown passes INFINITE because the wait
    // is what prevents callbacks from outliving the provider.
    HRESULT CloseAllConnections(DWORD timeoutMs);

    // Starts a request against WinHTTP immediately, bypassing the admission check. Callers must
    // already hold a reserved slot in m_activeRequestCount, reserved during epoch `epoch`.
    HRESULT StartRequest(HCCallHandle callHandle, XAsyncBlock* async, uint64_t epoch) noexcept;

    // Called when an admitted request finishes. Releases its slot and promotes queued requests.
    // `epoch` is the value of m_requestEpoch when the slot was reserved; a release from an earlier
    // epoch is ignored because CloseAllConnections already reclaimed those slots in bulk.
    void OnRequestCompleted(uint64_t epoch) noexcept;

    Result<XPlatSecurityInformation> GetSecurityInformation(const char* url);
    Result<HINTERNET> GetHSession(uint32_t securityProtocolFlags, const char* url);

    static HRESULT SetGlobalProxyForHSession(HINTERNET hSession, const char* proxyUri);
    static HRESULT GetProxyName(_In_ proxy_type proxyType, _In_ Uri proxyUri, _Out_ DWORD& pAccessType, _Out_ http_internal_wstring& pwProxyName);

    XTaskQueueHandle m_immediateQueue{ nullptr };
    xbox::httpclient::proxy_type m_proxyType = xbox::httpclient::proxy_type::automatic_proxy;
    http_internal_string m_globalProxy;
    std::mutex m_lock;

    // Maintain a WinHttpSession for each unique (security protocol flags, secure scheme) pair.
    //
    // `isSecure` is part of the key because WinHTTP sessions cannot be reused across TLS and
    // non-TLS connections. TLS-enabled sessions are restricted with WINHTTP_FLAG_SECURE_DEFAULTS
    // which then prevents those sessions from being used with insecure WS and HTTP schemes.
    // Keying on the protocol flags alone let whichever ran first win the cache slot, so an http://
    // or ws:// request that followed an https:// request reused the secure-defaults session and
    // failed in WinHttpOpenRequest with ERROR_ACCESS_DENIED.
    struct SessionKey
    {
        uint32_t securityProtocolFlags;
        bool isSecure;

        bool operator<(SessionKey const& other) const
        {
            if (securityProtocolFlags != other.securityProtocolFlags)
            {
                return securityProtocolFlags < other.securityProtocolFlags;
            }
            return isSecure < other.isSecure;
        }
    };
    http_internal_map<SessionKey, HINTERNET> m_hSessions;

    // Shared with the connection-closed callbacks so a connection that reports closed after a
    // bounded wait has already returned still has a valid context to signal. Defined in the .cpp.
    struct CloseContext;

    // Contexts from earlier CloseAllConnections calls whose bounded wait expired with connections
    // still open. Those connections were dropped from m_connections and cannot be closed a second
    // time (WinHttpConnection::Close is once-only and returns E_UNEXPECTED without invoking the
    // callback), so waiting on the context they were originally given is the only way to observe
    // them finishing. Only the shutdown path drains these, and only for a bounded time - not
    // INFINITE - so a connection that already missed one deadline cannot hang HCCleanup forever.
    // Best-effort: it narrows the window where a straggler outlives the provider and the WinHTTP
    // session handles it is still using, rather than closing it entirely.
    http_internal_vector<std::shared_ptr<CloseContext>> m_pendingCloseContexts;

    // Track WinHttpConnections so that we can close them on shutdown/suspend
    http_internal_list<std::weak_ptr<WinHttpConnection>> m_connections;

    // Requests admitted to WinHTTP but not yet completed. Bounded by GetGlobalRequestLimit().
    uint32_t m_activeRequestCount{ 0 };

    // Incremented whenever CloseAllConnections reclaims every slot at once. A slot reserved before
    // that reset must not be released again afterwards: the count is unsigned, so the stray
    // decrement would underflow to UINT32_MAX and make m_activeRequestCount >= the limit
    // permanently true, queueing every subsequent request forever. Releases carry the epoch they
    // were reserved in and are dropped if it no longer matches.
    uint64_t m_requestEpoch{ 0 };

    // Requests the caller has submitted that are waiting for a free slot. Unbounded by design:
    // titles may queue as many requests as they like, only concurrency is capped. FIFO, so a
    // queued request cannot be starved by later arrivals.
    struct PendingRequest
    {
        HCCallHandle callHandle;
        XAsyncBlock* async;
    };
    http_internal_list<PendingRequest> m_pendingRequests;

#if HC_PLATFORM == HC_PLATFORM_GDK
public: // For testing purposes only
    void Suspend();
    void Resume();

private:
    static void CALLBACK AppStateChangedCallback(BOOLEAN isSuspended, void* context);

    bool m_isSuspended{ false };
    PAPPSTATE_REGISTRATION m_appStateChangedToken{ nullptr };

    // Serializes the whole suspend sequence against resume and against provider destruction.
    // Suspend drops m_lock while it blocks waiting for connections to close, so m_lock alone cannot
    // keep a concurrent Resume (or a destructor running CloseAllConnections) from interleaving with
    // a suspend that is still in progress.
    std::mutex m_suspendLock;
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
class WinHttp_WebSocketProvider : public IWebSocketProvider, public IProviderLifecycle
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

    void OnSuspending() noexcept override;
    void OnResuming() noexcept override;

    SharedPtr<WinHttpProvider> const WinHttpProvider;
};
#endif
NAMESPACE_XBOX_HTTP_CLIENT_END
