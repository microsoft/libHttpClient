#include "pch.h"
#include "winhttp_provider.h"

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XGameRuntimeFeature.h>
#endif
#if HC_WINHTTP_WEBSOCKETS
#include "Websocket/WinHTTP/winhttp_websocket.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

Result<std::shared_ptr<WinHttpProvider>> WinHttpProvider::Initialize()
{
    http_stl_allocator<WinHttpProvider> a{};
    auto provider = std::shared_ptr<WinHttpProvider>{ new (a.allocate(1)) WinHttpProvider };

    RETURN_IF_FAILED(XTaskQueueCreate(XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Immediate, &provider->m_immediateQueue));

#if HC_PLATFORM == HC_PLATFORM_GDK
    if (!XGameRuntimeIsFeatureAvailable(XGameRuntimeFeature::XNetworking))
    {
        return E_HC_NO_NETWORK;
    }

    RETURN_IF_FAILED(XNetworkingRegisterConnectivityHintChanged(provider->m_immediateQueue, provider.get(), WinHttpProvider::NetworkConnectivityChangedCallback, &provider->m_networkConnectivityChangedToken));
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

    if (m_networkConnectivityChangedToken.token)
    {
        XNetworkingUnregisterConnectivityHintChanged(m_networkConnectivityChangedToken, true);
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

HRESULT WinHttpProvider::HttpCallPerform(HCCallHandle callHandle, XAsyncBlock* async) noexcept
{
    // Get Security information for the call
    auto getSecurityInfoResult = GetSecurityInformation(callHandle->url.data());
    RETURN_IF_FAILED(getSecurityInfoResult.hr);

    // Get HSession for the call
    auto getHSessionResult = GetHSession(getSecurityInfoResult.Payload().enabledHttpSecurityProtocolFlags);
    RETURN_IF_FAILED(getHSessionResult.hr);

    // Initialize WinHttpConnection
    auto initConnectionResult = WinHttpConnection::Initialize(getHSessionResult.ExtractPayload(), callHandle, m_proxyType, getSecurityInfoResult.ExtractPayload());
    RETURN_IF_FAILED(initConnectionResult.hr);

    // Store weak reference to connection so we can close it if it is still active on shutdown
    m_connections.push_back(initConnectionResult.Payload());

    // WinHttpConnection manages its own lifetime from here
    return initConnectionResult.Payload()->HttpCallPerformAsync(async);
}

#if HC_WINHTTP_WEBSOCKETS
HRESULT WinHttpProvider::WebSocketConnect(const char* uri, const char* /*subprotocol*/, HCWebsocketHandle websocketHandle, XAsyncBlock* async) noexcept
{
    // Get Security information for the call
    auto getSecurityInfoResult = GetSecurityInformation(uri);
    RETURN_IF_FAILED(getSecurityInfoResult.hr);

    // Get HSession for the call
    auto getHSessionResult = GetHSession(getSecurityInfoResult.Payload().enabledHttpSecurityProtocolFlags);
    RETURN_IF_FAILED(getHSessionResult.hr);

    // Initialize WinHttpConnection
    auto initConnectionResult = WinHttpConnection::Initialize(getHSessionResult.ExtractPayload(), websocketHandle, m_proxyType, getSecurityInfoResult.ExtractPayload());
    RETURN_IF_FAILED(initConnectionResult.hr);

    auto connection = initConnectionResult.ExtractPayload();
    // Store weak reference to connection so we can close it if it is still active on shutdown
    m_connections.push_back(connection);
    RETURN_IF_FAILED(connection->WebSocketConnectAsync(async));

    websocketHandle->impl = http_allocate_shared<WinHttpWebSocket>(std::move(connection));
    return S_OK;
}
#endif

HRESULT WinHttpProvider::SetGlobalProxy(_In_ const char* proxyUri) noexcept
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_globalProxy = proxyUri;
    for (auto& e : m_hSessions)
    {
        auto hSession = e.second;
        if (hSession != nullptr)
        {
            HRESULT hr = SetGlobalProxyForHSession(hSession, proxyUri);
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }

    return S_OK;
}

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

    closeContext.connectionsClosedEvent = CreateEvent(nullptr, TRUE, TRUE, nullptr);
    if (closeContext.connectionsClosedEvent == nullptr)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    auto connectionClosedCallback = [&closeContext]()
    {
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
    for (auto& connection : connections)
    {
        connection->Close(connectionClosedCallback);
    }

    WaitForSingleObject(closeContext.connectionsClosedEvent, INFINITE);

    return S_OK;
}

Result<XPlatSecurityInformation> WinHttpProvider::GetSecurityInformation(const char* url)
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    // Synchronously query SecurityInfo
    XAsyncBlock asyncBlock{};
    asyncBlock.queue = m_immediateQueue;
    RETURN_IF_FAILED(XNetworkingQuerySecurityInformationForUrlAsync(url, &asyncBlock));
    RETURN_IF_FAILED(XAsyncGetStatus(&asyncBlock, true));

    // TODO should there be a !m_isSuspended check at this point?

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
#else
    // Use default security protocol flags independent of URL
    UNREFERENCED_PARAMETER(url);
    return XPlatSecurityInformation{ WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 };
#endif
}

Result<HINTERNET> WinHttpProvider::GetHSession(uint32_t securityProtocolFlags)
{
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

    HINTERNET hSession = WinHttpOpen(
        nullptr,
        accessType,
        wProxyName.length() > 0 ? wProxyName.c_str() : WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
#if HC_PLATFORM == HC_PLATFORM_GDK
        WINHTTP_FLAG_SECURE_DEFAULTS
#else
        WINHTTP_FLAG_ASYNC
#endif
    );

#if HC_PLATFORM == HC_PLATFORM_GDK
    DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER)
    {
        // This might happen on older Win10 PC versions that don't support WINHTTP_FLAG_SECURE_DEFAULTS
        hSession = WinHttpOpen(
            nullptr,
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            WINHTTP_FLAG_ASYNC);
    }
#endif

    if (hSession == nullptr)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpProvider WinHttpOpen");
        return hr;
    }

    auto result = WinHttpSetOption(
        hSession,
        WINHTTP_OPTION_SECURE_PROTOCOLS,
        &securityProtocolFlags,
        sizeof(securityProtocolFlags));
    if (!result)
    {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpProvider WinHttpSetOption");
        return hr;
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

        http_internal_wstring wProxyHost = utf16_from_utf8(proxyUri.Host());

        // WinHttpOpen cannot handle trailing slash in the name, so here is some string gymnastics to keep WinHttpOpen happy
        if (proxyUri.IsPortDefault())
        {
            pwProxyName = wProxyHost;
        }
        else
        {
            if (proxyUri.Port() > 0)
            {
                http_internal_basic_stringstream<wchar_t> ss;
                ss.imbue(std::locale::classic());
                ss << wProxyHost << L":" << proxyUri.Port();
                pwProxyName = ss.str().c_str();
            }
            else
            {
                pwProxyName = wProxyHost;
            }
        }
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
    HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpProvider::Suspend");

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
    HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpProvider::Resume");

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
        // Always requery the latest network connectivity hint rather than relying on the passed parameter in case this is a stale notification
        XNetworkingConnectivityHint hint{};
        HRESULT hr = XNetworkingGetConnectivityHint(&hint);
        if (SUCCEEDED(hr))
        {
            HC_TRACE_VERBOSE(HTTPCLIENT, "NetworkConnectivityChangedCallback, hint.networkInitialized=%d", hint.networkInitialized);
            provider->m_networkInitialized = hint.networkInitialized;
        }
        else
        {
            HC_TRACE_ERROR(HTTPCLIENT, "Unable to get NetworkConnectivityHint, setting m_networkInitialized=false");
            provider->m_networkInitialized = false;
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

NAMESPACE_XBOX_HTTP_CLIENT_END

#if HC_PLATFORM != HC_PLATFORM_GDK

using namespace xbox::httpclient;

HC_PERFORM_ENV::HC_PERFORM_ENV(std::shared_ptr<xbox::httpclient::WinHttpProvider> _winHttpProvider)
    : winHttpProvider{ std::move(_winHttpProvider) }
{
}

HRESULT Internal_InitializeHttpPlatform(HCInitArgs* args, PerformEnv& performEnv) noexcept
{
    assert(args == nullptr);
    UNREFERENCED_PARAMETER(args);

    auto initWinHttpProviderResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpProviderResult.hr);

    // Mem hooked unique ptr with non-standard dtor handler in PerformEnvDeleter
    http_stl_allocator<HC_PERFORM_ENV> a{};
    performEnv.reset(new (a.allocate(1)) HC_PERFORM_ENV{ initWinHttpProviderResult.ExtractPayload() });
    if (!performEnv) 
    { 
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

void Internal_CleanupHttpPlatform(HC_PERFORM_ENV* performEnv) noexcept
{
    http_stl_allocator<HC_PERFORM_ENV> a{};
    std::allocator_traits<http_stl_allocator<HC_PERFORM_ENV>>::destroy(a, performEnv);
    std::allocator_traits<http_stl_allocator<HC_PERFORM_ENV>>::deallocate(a, performEnv, 1);
}

HRESULT Internal_SetGlobalProxy(
    _In_ HC_PERFORM_ENV* performEnv,
    _In_ const char* proxyUri
) noexcept
{
    RETURN_HR_IF(E_UNEXPECTED, !performEnv || !performEnv->winHttpProvider);
    return performEnv->winHttpProvider->SetGlobalProxy(proxyUri);
}

void CALLBACK Internal_HCHttpCallPerformAsync(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
    ) noexcept
{
    UNREFERENCED_PARAMETER(context);
    assert(env && env->winHttpProvider);

    HRESULT hr = env->winHttpProvider->HttpCallPerform(call, asyncBlock);
    if (FAILED(hr))
    {
        // Complete XAsyncBlock if we fail synchronously
        XAsyncComplete(asyncBlock, hr, 0);
    }
}

#endif // HC_PLATFORM != HC_PLATFORM_GDK
