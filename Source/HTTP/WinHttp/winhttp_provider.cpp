#include "pch.h"
#include "HTTP/httpcall.h"
#include "winhttp_provider.h"
#include "winhttp_connection.h"
#include "uri.h"

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XGameRuntimeFeature.h>
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

Result<HC_UNIQUE_PTR<WinHttpProvider>> WinHttpProvider::Initialize()
{
    http_stl_allocator<WinHttpProvider> a{};
    auto provider = HC_UNIQUE_PTR<WinHttpProvider>{ new (a.allocate(1)) WinHttpProvider };

    RETURN_IF_FAILED(XTaskQueueCreate(XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Immediate, &provider->m_immediateQueue));

#if HC_PLATFORM == HC_PLATFORM_GDK
    if (XGameRuntimeIsFeatureAvailable(XGameRuntimeFeature::XNetworking))
    {
        RETURN_IF_FAILED(XNetworkingRegisterConnectivityHintChanged(provider->m_immediateQueue, provider.get(), WinHttpProvider::NetworkConnectivityChangedCallback, &provider->m_networkConnectivityChangedToken));
    }
    else
    {
        // XNetworking not available (e.g., PC GDK build), assume network is ready
        provider->m_networkInitialized = true;
    }

    RETURN_IF_FAILED(RegisterAppStateChangeNotification(WinHttpProvider::AppStateChangedCallback, provider.get(), &provider->m_appStateChangedToken));

#endif // HC_PLATFORM == HC_PLATFORM_GDK

    return std::move(provider);
}

WinHttpProvider::~WinHttpProvider()
{
    if (m_immediateQueue)
    {
        XTaskQueueCloseHandle(m_immediateQueue);
    }

#if HC_PLATFORM == HC_PLATFORM_GDK
    if (m_appStateChangedToken)
    {
        UnregisterAppStateChangeNotification(m_appStateChangedToken);
    }

    if (XGameRuntimeIsFeatureAvailable(XGameRuntimeFeature::XNetworking))
    {
        if (m_networkConnectivityChangedToken.token)
        {
            XNetworkingUnregisterConnectivityHintChanged(m_networkConnectivityChangedToken, true);
        }
    }
#endif

    HRESULT hr = CloseAllConnections();
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpProvider::CloseAllConnections failed during shutdown");
    }

    for (auto& pair : m_hSessions)
    {
        if (pair.second)
        {
            WinHttpCloseHandle(pair.second);
        }
    }
    m_hSessions.clear();
}

WinHttpWebSocketExports GetWinHttpWebSocketExportsHelper()
{
    WinHttpWebSocketExports exports;
    exports.winHttpModule = LoadLibrary(TEXT("Winhttp.dll"));
    if (exports.winHttpModule)
    {
        exports.completeUpgrade = (WinHttpWebSocketCompleteUpgradeExport)GetProcAddress(exports.winHttpModule, "WinHttpWebSocketCompleteUpgrade");
        exports.send = (WinHttpWebSocketSendExport)GetProcAddress(exports.winHttpModule, "WinHttpWebSocketSend");
        exports.receive = (WinHttpWebSocketReceiveExport)GetProcAddress(exports.winHttpModule, "WinHttpWebSocketReceive");
        exports.close = (WinHttpWebSocketCloseExport)GetProcAddress(exports.winHttpModule, "WinHttpWebSocketClose");
        exports.queryCloseStatus = (WinHttpWebSocketQueryCloseStatusExport)GetProcAddress(exports.winHttpModule, "WinHttpWebSocketQueryCloseStatus");
        exports.shutdown = (WinHttpWebSocketShutdownExport)GetProcAddress(exports.winHttpModule, "WinHttpWebSocketShutdown");
    }
    return exports;
}

WinHttpWebSocketExports WinHttpProvider::GetWinHttpWebSocketExports()
{
    static WinHttpWebSocketExports s_exports{ GetWinHttpWebSocketExportsHelper() };
    return s_exports;
}

HRESULT WinHttpProvider::PerformAsync(
    HCCallHandle callHandle,
    XAsyncBlock* async
) noexcept
{
    // Get Security information for the call
    auto getSecurityInfoResult = GetSecurityInformation(callHandle->url.data());
    RETURN_IF_FAILED(getSecurityInfoResult.hr);

    // Get HSession for the call
    auto getHSessionResult = GetHSession(getSecurityInfoResult.Payload().enabledHttpSecurityProtocolFlags, callHandle->url.data());
    RETURN_IF_FAILED(getHSessionResult.hr);

    std::unique_lock<std::mutex> lock{ m_lock };
#if HC_PLATFORM == HC_PLATFORM_GDK
    if (!m_networkInitialized)
    {
        return E_HC_NETWORK_NOT_INITIALIZED;
    }
#endif

    // Initialize WinHttpConnection
    auto initConnectionResult = WinHttpConnection::Initialize(getHSessionResult.ExtractPayload(), callHandle, m_proxyType, getSecurityInfoResult.ExtractPayload());
    RETURN_IF_FAILED(initConnectionResult.hr);

    // Store weak reference to connection so we can close it if it is still active on shutdown
    m_connections.push_back(initConnectionResult.Payload());

    // WinHttpConnection manages its own lifetime from here
    return initConnectionResult.Payload()->HttpCallPerformAsync(async);
}

HRESULT WinHttpProvider::SetGlobalProxy(_In_ String const& proxyUri) noexcept
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_globalProxy = proxyUri;
    for (auto& e : m_hSessions)
    {
        auto hSession = e.second;
        if (hSession != nullptr)
        {
            HRESULT hr = SetGlobalProxyForHSession(hSession, proxyUri.data());
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }

    return S_OK;
}

#ifndef HC_NOWEBSOCKETS
HRESULT WinHttpProvider::ConnectAsync(
    String const& uri,
    String const& subprotocol,
    HCWebsocketHandle websocketHandle,
    XAsyncBlock* async
) noexcept
{
    // Get Security information for the call
    auto getSecurityInfoResult = GetSecurityInformation(uri.data());
    RETURN_IF_FAILED(getSecurityInfoResult.hr);

    // Get HSession for the call
    auto getHSessionResult = GetHSession(getSecurityInfoResult.Payload().enabledHttpSecurityProtocolFlags, uri.data());
    RETURN_IF_FAILED(getHSessionResult.hr);

    std::unique_lock<std::mutex> lock{ m_lock };
#if HC_PLATFORM == HC_PLATFORM_GDK
    if (!m_networkInitialized)
    {
        return E_HC_NETWORK_NOT_INITIALIZED;
    }
#endif

    // Initialize WinHttpConnection
    auto initConnectionResult = WinHttpConnection::Initialize(getHSessionResult.ExtractPayload(), websocketHandle, uri.data(), subprotocol.data(), m_proxyType, getSecurityInfoResult.ExtractPayload());
    RETURN_IF_FAILED(initConnectionResult.hr);

    auto connection = initConnectionResult.ExtractPayload();
    // Store weak reference to connection so we can close it if it is still active on shutdown
    m_connections.push_back(connection);
    RETURN_IF_FAILED(connection->WebSocketConnectAsync(async));

    websocketHandle->websocket->impl = std::move(connection);

    return S_OK;
}

HRESULT WinHttpProvider::SendAsync(
    HCWebsocketHandle websocketHandle,
    const char* message,
    XAsyncBlock* async
) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !websocketHandle);

    auto connection = std::dynamic_pointer_cast<WinHttpConnection>(websocketHandle->websocket->impl);
    RETURN_HR_IF(E_UNEXPECTED, !connection);

    return connection->WebSocketSendMessageAsync(async, message);
}

HRESULT WinHttpProvider::SendBinaryAsync(
    HCWebsocketHandle websocketHandle,
    const uint8_t* payloadBytes,
    uint32_t payloadSize,
    XAsyncBlock* asyncBlock
) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !websocketHandle);

    auto connection = std::dynamic_pointer_cast<WinHttpConnection>(websocketHandle->websocket->impl);
    RETURN_HR_IF(E_UNEXPECTED, !connection);

    return connection->WebSocketSendMessageAsync(asyncBlock, payloadBytes, payloadSize);
}

HRESULT WinHttpProvider::Disconnect(
    HCWebsocketHandle websocketHandle,
    HCWebSocketCloseStatus closeStatus
) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !websocketHandle);

    auto connection = std::dynamic_pointer_cast<WinHttpConnection>(websocketHandle->websocket->impl);
    RETURN_HR_IF(E_UNEXPECTED, !connection);

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", TO_ULL(websocketHandle->websocket->id));

    return connection->WebSocketDisconnect(closeStatus);
}
#endif //!HC_NOWEBSOCKETS

HRESULT WinHttpProvider::CloseAllConnections()
{
    // Should set result to HRESULT_FROM_WIN32(PROCESS_SUSPEND_RESUME)

    struct CloseContext
    {
        ~CloseContext()
        {
            if (connectionsClosedEvent)
            {
                CloseHandle(connectionsClosedEvent);
            }
        }

        HANDLE connectionsClosedEvent;
        std::atomic<size_t> openConnections;

    } closeContext;

    closeContext.connectionsClosedEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (closeContext.connectionsClosedEvent == nullptr)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    auto connectionClosedCallback = [&closeContext]()
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpProvider::Connection Closed, %llu remaining", closeContext.openConnections - 1);

        if (--closeContext.openConnections == 0)
        {
            SetEvent(closeContext.connectionsClosedEvent);
        }
    };

    http_internal_list<std::shared_ptr<WinHttpConnection>> connections;
    {
        std::lock_guard<std::mutex> lock{ m_lock };

        for (auto& weakConnection : m_connections)
        {
            auto connection{ weakConnection.lock() };
            if (connection)
            {
                connections.emplace_back(std::move(connection));
            }
        }
        m_connections.clear();
    }

    closeContext.openConnections = connections.size();
    if (closeContext.openConnections > 0)
    {
        for (auto& connection : connections)
        {
            assert(connection);
            if (connection)
            {
                HRESULT hr = connection->Close(connectionClosedCallback);
                if (FAILED(hr))
                {
                    HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpConnection::Close failed");
                }
            }
        }
        WaitForSingleObject(closeContext.connectionsClosedEvent, INFINITE);
    }

    return S_OK;
}

Result<XPlatSecurityInformation> WinHttpProvider::GetSecurityInformation(const char* url)
{
    constexpr uint32_t defaultSecurityProtocolFlags =
        WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 |
        WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
        WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;

#if HC_PLATFORM == HC_PLATFORM_GDK
    bool useXNetworking = XGameRuntimeIsFeatureAvailable(XGameRuntimeFeature::XNetworking);
    if (useXNetworking)
    {
        // Synchronously query SecurityInfo
        XAsyncBlock asyncBlock{};
        asyncBlock.queue = m_immediateQueue;
        RETURN_IF_FAILED(XNetworkingQuerySecurityInformationForUrlAsync(url, &asyncBlock));
        RETURN_IF_FAILED(XAsyncGetStatus(&asyncBlock, true));

        size_t securityInformationBufferByteCount{ 0 };
        RETURN_IF_FAILED(XNetworkingQuerySecurityInformationForUrlAsyncResultSize(&asyncBlock, &securityInformationBufferByteCount));
        assert(securityInformationBufferByteCount > 0);

        XPlatSecurityInformation securityInfo;
        securityInfo.buffer.resize(securityInformationBufferByteCount);
        RETURN_IF_FAILED(XNetworkingQuerySecurityInformationForUrlAsyncResult(
            &asyncBlock,
            securityInfo.buffer.size(),
            nullptr,
            securityInfo.buffer.data(),
            &securityInfo.securityInformation));

        // Duplicate security protocol flags for convenience
        securityInfo.enabledHttpSecurityProtocolFlags = securityInfo.securityInformation->enabledHttpSecurityProtocolFlags;

        return std::move(securityInfo);
    }
#else
    UNREFERENCED_PARAMETER(url);
#endif

    // Fallback to default security protocol flags
    return XPlatSecurityInformation{ defaultSecurityProtocolFlags };
}

Result<HINTERNET> WinHttpProvider::GetHSession(uint32_t securityProtocolFlags, const char* url)
{
    // Parse URL to determine scheme
    xbox::httpclient::Uri uri(url);
    if (!uri.IsValid())
    {
        return E_INVALIDARG;
    }
    
    bool isHttps = uri.IsSecure();
    
#if HC_PLATFORM == HC_PLATFORM_GDK
    // Log warning for insecure HTTP requests on GDK for console certification reasons
    if (!isHttps)
    {
        HC_TRACE_WARNING(HTTPCLIENT, "WARNING: Insecure HTTP request \"%s\"", url);
    }
#endif
    
    std::lock_guard<std::mutex> lock(m_lock);
    auto iter = m_hSessions.find(securityProtocolFlags);
    if (iter != m_hSessions.end())
    {
        HINTERNET hSession = iter->second;
        return hSession;
    }

    xbox::httpclient::Uri proxyUri;
    DWORD accessType{ WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY };
    http_internal_wstring wProxyName;
    m_proxyType = get_ie_proxy_info(proxy_protocol::https, proxyUri);
    GetProxyName(m_proxyType, proxyUri, accessType, wProxyName);

    // Determine WinHTTP flags based on URL scheme
    // Use WINHTTP_FLAG_SECURE_DEFAULTS for HTTPS and WINHTTP_FLAG_ASYNC for HTTP
    DWORD openFlags;
    if (isHttps)
    {
        // For HTTPS, use secure defaults which implies WINHTTP_FLAG_ASYNC
        openFlags = WINHTTP_FLAG_SECURE_DEFAULTS;
    }
    else
    {
        // For HTTP, use async only (allow insecure connections)
        openFlags = WINHTTP_FLAG_ASYNC;
    }

    HINTERNET hSession = WinHttpOpen(
        nullptr,
        accessType,
        wProxyName.length() > 0 ? wProxyName.c_str() : WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        openFlags
    );

    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER && isHttps)
    {
        // WINHTTP_FLAG_SECURE_DEFAULTS exists only on newer Windows versions;
        // on earlier OS releases we will receive ERROR_INVALID_PARAMETER and should continue without it.
        hSession = WinHttpOpen(
            nullptr,
            accessType,
            wProxyName.length() > 0 ? wProxyName.c_str() : WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            WINHTTP_FLAG_ASYNC);
    }

    if (hSession == nullptr)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpProvider WinHttpOpen");
        return hr;
    }

    // Only set secure protocols for HTTPS requests
    // For HTTP requests, ignore the security protocol settings as they don't apply
    if (isHttps)
    {
        auto result = WinHttpSetOption(
            hSession,
            WINHTTP_OPTION_SECURE_PROTOCOLS,
            &securityProtocolFlags,
            sizeof(securityProtocolFlags));
        if (!result)
        {
            DWORD lastErr = GetLastError();
            // Occasionally WinHttpSetOption(WINHTTP_OPTION_SECURE_PROTOCOLS) can fail on some
            // platforms / configurations (e.g. older OS versions or when specific protocol
            // flags are already implicitly enabled). The caller requested that we treat this
            // as non-fatal: emit a warning and proceed with the session using WinHTTP defaults.
            // If GetLastError() returned 0 (no extended error), fabricate a generic failure
            // HRESULT just for logging purposes.
            HRESULT hr = lastErr != 0 ? HRESULT_FROM_WIN32(lastErr) : E_FAIL;
            HC_TRACE_WARNING_HR(HTTPCLIENT, hr, "WinHttpProvider WinHttpSetOption WINHTTP_OPTION_SECURE_PROTOCOLS failed; retrying with WinHttpOpen WINHTTP_FLAG_ASYNC session");

            // Retry strategy: Some platforms may not allow modifying secure protocols after
            // opening the session with WINHTTP_FLAG_SECURE_DEFAULTS. Re-open a plain ASYNC
            // session (no secure defaults) and try setting the option again.
            WinHttpCloseHandle(hSession);
            hSession = WinHttpOpen(
                nullptr,
                accessType,
                wProxyName.length() > 0 ? wProxyName.c_str() : WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                WINHTTP_FLAG_ASYNC);
            if (hSession == nullptr)
            {
                HRESULT openHr = HRESULT_FROM_WIN32(GetLastError());
                HC_TRACE_WARNING_HR(HTTPCLIENT, openHr, "WinHttpProvider fallback WinHttpOpen with WINHTTP_FLAG_ASYNC failed; continuing without explicitly setting secure protocols");
            }
            else
            {
                auto retryResult = WinHttpSetOption(
                    hSession,
                    WINHTTP_OPTION_SECURE_PROTOCOLS,
                    &securityProtocolFlags,
                    sizeof(securityProtocolFlags));
                if (!retryResult)
                {
                    DWORD retryErr = GetLastError();
                    HRESULT retryHr = retryErr != 0 ? HRESULT_FROM_WIN32(retryErr) : E_FAIL;
                    HC_TRACE_WARNING_HR(HTTPCLIENT, retryHr, "WinHttpProvider retry WinHttpSetOption WINHTTP_OPTION_SECURE_PROTOCOLS still failed; proceeding with WinHTTP defaults");
                }
                else
                {
                    HC_TRACE_INFORMATION(HTTPCLIENT, "WinHttpProvider retry WinHttpSetOption WINHTTP_OPTION_SECURE_PROTOCOLS succeeded after reopening session");
                }
            }
        }
    }

    BOOL enableFallback = TRUE;
    auto result = WinHttpSetOption(
        hSession,
        WINHTTP_OPTION_IPV6_FAST_FALLBACK,
        &enableFallback,
        sizeof(enableFallback));
    if (!result)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        HC_TRACE_WARNING_HR(HTTPCLIENT, hr, "WinHttpProvider WinHttpSetOption WINHTTP_OPTION_IPV6_FAST_FALLBACK");
    }

    if (!m_globalProxy.empty())
    {
        (void)SetGlobalProxyForHSession(hSession, m_globalProxy.c_str());
    }

    m_hSessions[securityProtocolFlags] = hSession;

    return hSession;
}

HRESULT WinHttpProvider::SetGlobalProxyForHSession(HINTERNET hSession, _In_ const char* proxyUri)
{
    WINHTTP_PROXY_INFO info = { 0 };
    http_internal_wstring wProxyName;

    if (proxyUri == nullptr)
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "Internal_SetGlobalProxy [TID %ul] reseting proxy", GetCurrentThreadId());

        xbox::httpclient::Uri ieProxyUri;
        auto desiredType = get_ie_proxy_info(proxy_protocol::https, ieProxyUri);

        DWORD accessType = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
        RETURN_IF_FAILED(GetProxyName(desiredType, ieProxyUri, accessType, wProxyName));

        info.dwAccessType = accessType;
        if (wProxyName.length() > 0)
        {
            info.lpszProxy = const_cast<LPWSTR>(wProxyName.c_str());
        }
    }
    else
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "Internal_SetGlobalProxy [TID %ul] setting proxy to '%s'", GetCurrentThreadId(), proxyUri);

        wProxyName = utf16_from_utf8(proxyUri);

        info.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        info.lpszProxy = const_cast<LPWSTR>(wProxyName.c_str());
    }

    auto result = WinHttpSetOption(
        hSession,
        WINHTTP_OPTION_PROXY,
        &info,
        sizeof(WINHTTP_PROXY_INFO));
    if (!result)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "Internal_SetGlobalProxy [TID %ul] WinHttpSetOption errorcode %d", GetCurrentThreadId(), dwError);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT WinHttpProvider::GetProxyName(
    _In_ proxy_type proxyType,
    _In_ xbox::httpclient::Uri proxyUri,
    _Out_ DWORD& pAccessType,
    _Out_ http_internal_wstring& pwProxyName)
{
    switch (proxyType)
    {
    case proxy_type::no_proxy:
    {
        pAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
        pwProxyName = L"";
        break;
    }

    case proxy_type::named_proxy:
    {
        pAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        pwProxyName = WinHttpProvider::BuildNamedProxyString(proxyUri);
        break;
    }

    case proxy_type::default_proxy:
    case proxy_type::autodiscover_proxy:
    {
        pAccessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
        pwProxyName = L"";
        break;
    }

    default:
    case proxy_type::automatic_proxy:
    {
        pAccessType = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
        pwProxyName = L"";
        break;
    }
    }

    return S_OK;
}


#if HC_PLATFORM == HC_PLATFORM_GDK

void WinHttpProvider::Suspend()
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "WinHttpProvider::Suspend");

    {
        std::lock_guard<std::mutex> lock{ m_lock };

        assert(!m_isSuspended);
        m_isSuspended = true;
        m_networkInitialized = false;
    }

    HRESULT hr = CloseAllConnections();
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpProvider::CloseAllConnections failed during suspend, continuing with suspend sequence");
    }

    std::lock_guard<std::mutex> lock{ m_lock };
    for (auto& pair : m_hSessions)
    {
        if (pair.second)
        {
            WinHttpCloseHandle(pair.second);
        }
    }
    m_hSessions.clear();
}

void WinHttpProvider::Resume()
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "WinHttpProvider::Resume");

    std::unique_lock<std::mutex> lock{ m_lock };

    assert(m_isSuspended);
    m_isSuspended = false;

    lock.unlock();

    // Force a query of network state since we've ignored notifications during suspend
    NetworkConnectivityChangedCallback(this, nullptr);
}

void WinHttpProvider::NetworkConnectivityChangedCallback(void* context, const XNetworkingConnectivityHint* /*hint*/)
{
    assert(context);
    auto provider = static_cast<WinHttpProvider*>(context);

    std::lock_guard<std::mutex> lock{ provider->m_lock };

    // Ignore network connectivity changes if we are suspended
    if (!provider->m_isSuspended)
    {
        if (XGameRuntimeIsFeatureAvailable(XGameRuntimeFeature::XNetworking))
        {
            // Always requery the latest network connectivity hint rather than relying on the passed parameter in case this is a stale notification
            XNetworkingConnectivityHint hint{};
            HRESULT hr = XNetworkingGetConnectivityHint(&hint);
            if (SUCCEEDED(hr))
            {
                HC_TRACE_INFORMATION(HTTPCLIENT, "NetworkConnectivityChangedCallback, hint.networkInitialized=%d", hint.networkInitialized);
                provider->m_networkInitialized = hint.networkInitialized;
            }
            else
            {
                HC_TRACE_ERROR(HTTPCLIENT, "Unable to get NetworkConnectivityHint, setting m_networkInitialized=false");
                provider->m_networkInitialized = false;
            }
        }
        else
        {
            // Fallback to default network state if XNetworking is not available
            provider->m_networkInitialized = true;
        }
    }
}

void WinHttpProvider::AppStateChangedCallback(BOOLEAN isSuspended, void* context)
{
    assert(context);
    auto provider = static_cast<WinHttpProvider*>(context);

    if (isSuspended)
    {
        provider->Suspend();
    }
    else
    {
        provider->Resume();
    }
}
#endif // HC_PLATFORM == HC_PLATFORM_GDK

WinHttp_HttpProvider::WinHttp_HttpProvider(std::shared_ptr<xbox::httpclient::WinHttpProvider> provider) : WinHttpProvider{ std::move(provider) }
{
}

HRESULT WinHttp_HttpProvider::PerformAsync(HCCallHandle callHandle, XAsyncBlock* async) noexcept
{
    return WinHttpProvider->PerformAsync(callHandle, async);
}

#ifndef HC_NOWEBSOCKETS
WinHttp_WebSocketProvider::WinHttp_WebSocketProvider(std::shared_ptr<xbox::httpclient::WinHttpProvider> provider) : WinHttpProvider{ std::move(provider) }
{
}

HRESULT WinHttp_WebSocketProvider::ConnectAsync(
    String const& uri,
    String const& subprotocol,
    HCWebsocketHandle websocketHandle,
    XAsyncBlock* async
) noexcept
{
    return WinHttpProvider->ConnectAsync(uri, subprotocol, websocketHandle, async);
}

HRESULT WinHttp_WebSocketProvider::SendAsync(
    HCWebsocketHandle websocketHandle,
    const char* message,
    XAsyncBlock* async
) noexcept
{
    return WinHttpProvider->SendAsync(websocketHandle, message, async);
}

HRESULT WinHttp_WebSocketProvider::SendBinaryAsync(
    HCWebsocketHandle websocketHandle,
    const uint8_t* payloadBytes,
    uint32_t payloadSize,
    XAsyncBlock* asyncBlock
) noexcept
{
    return WinHttpProvider->SendBinaryAsync(websocketHandle, payloadBytes, payloadSize, asyncBlock);
}

HRESULT WinHttp_WebSocketProvider::Disconnect(
    HCWebsocketHandle websocketHandle,
    HCWebSocketCloseStatus closeStatus
) noexcept
{
    return WinHttpProvider->Disconnect(websocketHandle, closeStatus);
}
#endif // !HC_NOWEBSOCKETS

NAMESPACE_XBOX_HTTP_CLIENT_END
