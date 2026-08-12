#include "pch.h"
#include "HTTP/httpcall.h"
#include "Global/global.h"
#include <httpClient/httpProvider.h>
#include "winhttp_provider.h"
#include "winhttp_connection.h"
#include "uri.h"

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XGameRuntimeFeature.h>
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

struct WinHttpProvider::CloseContext
{
    ~CloseContext()
    {
        if (connectionsClosedEvent)
        {
            CloseHandle(connectionsClosedEvent);
        }
    }

    HANDLE connectionsClosedEvent{ nullptr };
    std::atomic<size_t> openConnections{ 0 };
};

Result<HC_UNIQUE_PTR<WinHttpProvider>> WinHttpProvider::Initialize()
{
    http_stl_allocator<WinHttpProvider> a{};
    auto provider = HC_UNIQUE_PTR<WinHttpProvider>{ new (a.allocate(1)) WinHttpProvider };

    RETURN_IF_FAILED(XTaskQueueCreate(XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Immediate, &provider->m_immediateQueue));

#if HC_PLATFORM == HC_PLATFORM_GDK
    // Soft-fail: PLM suspend handling is a resilience feature, so losing it must not cost the
    // title HTTP entirely. RegisterAppStateChangeNotification resolves from
    // api-ms-win-core-psm-appnotify, which is not guaranteed to be present in every configuration
    // the GDK runs in. Log and continue without suspend notifications; the destructor tolerates a
    // null token, and Suspend/Resume simply never fire.
    HRESULT registerHr = RegisterAppStateChangeNotification(WinHttpProvider::AppStateChangedCallback, provider.get(), &provider->m_appStateChangedToken);
    if (FAILED(registerHr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, registerHr, "WinHttpProvider::Initialize: RegisterAppStateChangeNotification failed; suspend handling disabled");
        provider->m_appStateChangedToken = nullptr;
    }
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

    // Unregistering above stops new PLM notifications, but one may already be executing. Take the
    // suspend lock so teardown cannot run concurrently with an in-progress Suspend or Resume.
    std::unique_lock<std::mutex> suspendLock{ m_suspendLock };
#endif

    // INFINITE on the shutdown path: waiting for every connection to report closed is what keeps a
    // WinHTTP callback from running after this object, the singleton, and any caller-supplied
    // memory hooks are gone.
    HRESULT hr = CloseAllConnections(INFINITE);
    if (FAILED(hr))
    {
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpProvider::CloseAllConnections failed during shutdown");
    }

    {
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

#if HC_PLATFORM == HC_PLATFORM_GDK
    // Release before the mutex itself is destroyed with the object.
    suspendLock.unlock();
#endif
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
    // Admission control: only GetGlobalRequestLimit() requests are allowed to reach WinHTTP at
    // once, to bound the memory held by in-flight requests (each one costs WinHTTP request and
    // connection handles, TLS state, and receive buffers). Requests beyond the cap are queued and
    // started as earlier ones complete, so the caller never sees a failure for exceeding the cap.
    uint64_t epoch{ 0 };
    {
        std::lock_guard<std::mutex> lock{ m_lock };

        if (m_activeRequestCount >= GetGlobalRequestLimit())
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "WinHttpProvider::PerformAsync queueing request, %u already active (limit %u)",
                m_activeRequestCount, GetGlobalRequestLimit());
            m_pendingRequests.push_back(PendingRequest{ callHandle, async });
            return S_OK;
        }

        ++m_activeRequestCount;
        epoch = m_requestEpoch;
    }

    HRESULT hr = StartRequest(callHandle, async, epoch);
    if (FAILED(hr))
    {
        // The request never reached WinHTTP, so it will never complete and would otherwise leak
        // its slot. Release it and let any queued request take it.
        OnRequestCompleted(epoch);
    }
    return hr;
}

HRESULT WinHttpProvider::StartRequest(
    HCCallHandle callHandle,
    XAsyncBlock* async,
    uint64_t epoch
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
    // Refuse new requests while suspended: PLM requires every network resource to be destroyed
    // before the process is snapshotted, so nothing may be started until Resume.
    //
    // Note there is deliberately no "is the network initialized" check here. That gate used to
    // consult a cached XNetworking connectivity hint, which could latch false permanently when the
    // change notification was never delivered (observed on Steam Deck under Proton after an
    // offline->online transition), leaving every request failing with E_HC_NETWORK_NOT_INITIALIZED
    // for the life of the process. WinHTTP is the authority on whether a request can go out, so we
    // let it try and return a real, retryable error instead.
    if (m_isSuspended)
    {
        return E_HC_NETWORK_NOT_INITIALIZED;
    }
#endif

    // Initialize WinHttpConnection
    auto initConnectionResult = WinHttpConnection::Initialize(getHSessionResult.ExtractPayload(), callHandle, m_proxyType, getSecurityInfoResult.ExtractPayload());
    RETURN_IF_FAILED(initConnectionResult.hr);

    // Store weak reference to connection so we can close it if it is still active on shutdown
    m_connections.push_back(initConnectionResult.Payload());

    // Release the concurrency slot when the request finishes, however it finishes. Safe to capture
    // `this`: the provider outlives its connections, which it force-closes in CloseAllConnections
    // during both suspend and destruction.
    initConnectionResult.Payload()->SetRequestCompletedCallback([this, epoch]() { OnRequestCompleted(epoch); });

    // Unlock before starting: HttpCallPerformAsync can complete synchronously, which re-enters
    // OnRequestCompleted and would deadlock on the non-recursive m_lock.
    auto connection = initConnectionResult.Payload();
    lock.unlock();

    // WinHttpConnection manages its own lifetime from here
    return connection->HttpCallPerformAsync(async);
}

void WinHttpProvider::OnRequestCompleted(uint64_t epoch) noexcept
{
    // Promote at most one queued request per completion, matching the slot that was just freed.
    // Started outside the lock: HttpCallPerformAsync can complete synchronously and re-enter
    // OnRequestCompleted, which would deadlock on the non-recursive m_lock.
    for (;;)
    {
        PendingRequest next{};
        uint64_t nextEpoch{ 0 };
        {
            std::lock_guard<std::mutex> lock{ m_lock };

            // CloseAllConnections already reclaimed every slot reserved in this epoch, so this
            // release refers to a slot that no longer exists. Dropping it is what keeps the
            // unsigned count from underflowing and wedging admission control permanently.
            if (epoch != m_requestEpoch)
            {
                return;
            }

            // Defensive: the epoch check above should make a zero count unreachable here, but the
            // consequence of being wrong is a permanent stall, so saturate rather than wrap.
            if (m_activeRequestCount > 0)
            {
                --m_activeRequestCount;
            }

            if (m_pendingRequests.empty() || m_activeRequestCount >= GetGlobalRequestLimit())
            {
                return;
            }

            next = m_pendingRequests.front();
            m_pendingRequests.pop_front();
            ++m_activeRequestCount;
            nextEpoch = m_requestEpoch;
        }

        HRESULT hr = StartRequest(next.callHandle, next.async, nextEpoch);
        if (SUCCEEDED(hr))
        {
            return;
        }

        // Complete the failed request for the caller, then loop to release its slot and give the
        // next queued request a chance. Without this the caller would wait forever on a request
        // that never reached WinHTTP.
        HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpProvider: queued request failed to start");
        HCHttpCallResponseSetNetworkErrorCode(next.callHandle, hr, static_cast<uint32_t>(hr));
        XAsyncComplete(next.async, S_OK, 0);

        // The next iteration releases the slot just reserved above, which belongs to nextEpoch.
        epoch = nextEpoch;
    }
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
    // See the equivalent comment in StartRequest: suspend blocks new work, but there is
    // deliberately no cached network-initialized gate here.
    if (m_isSuspended)
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

HRESULT WinHttpProvider::CloseAllConnections(DWORD timeoutMs)
{
    // Should set result to HRESULT_FROM_WIN32(PROCESS_SUSPEND_RESUME)

    // Heap allocated and shared with the completion callback rather than living on this stack
    // frame. The wait below is bounded, so a connection can report closed after this function has
    // returned; a stack-allocated context would be freed memory by then.
    auto closeContext = std::allocate_shared<CloseContext>(http_stl_allocator<CloseContext>{});

    closeContext->connectionsClosedEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (closeContext->connectionsClosedEvent == nullptr)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    auto connectionClosedCallback = [closeContext]()
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpProvider::Connection Closed, %llu remaining", closeContext->openConnections - 1);

        if (--closeContext->openConnections == 0)
        {
            SetEvent(closeContext->connectionsClosedEvent);
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

    closeContext->openConnections = connections.size();
    if (closeContext->openConnections > 0)
    {
        for (auto& connection : connections)
        {
            assert(connection);
            if (connection)
            {
                // Drop the budget callback before closing. These connections are about to be torn
                // down as a group and the budget is reset in bulk below, so per-connection release
                // is both unnecessary and unsafe here: a connection can outlive this call and would
                // then invoke a callback capturing a provider that may already be destroyed.
                connection->SetRequestCompletedCallback(nullptr);

                HRESULT hr = connection->Close(connectionClosedCallback);
                if (FAILED(hr))
                {
                    HC_TRACE_ERROR_HR(HTTPCLIENT, hr, "WinHttpConnection::Close failed");
                }
            }
        }
        // Bounded on the PLM suspend path, where the title has a short budget to comply before the
        // OS terminates it: a connection that fails to report closed in time must not turn a slow
        // teardown into a watchdog kill. The close context is shared with the callback, so a late
        // report after this point is still safe. Shutdown passes INFINITE instead, because there
        // the wait is what guarantees no WinHTTP callback can still run against connections that
        // allocate through the libHttpClient allocator after the provider and singleton are gone.
        if (WaitForSingleObject(closeContext->connectionsClosedEvent, timeoutMs) == WAIT_TIMEOUT)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttpProvider::CloseAllConnections timed out after %ums with %llu connection(s) still open",
                timeoutMs, static_cast<unsigned long long>(closeContext->openConnections.load()));

            // Those connections have already been dropped from m_connections and cannot be closed
            // again, so retain the context they were given. The shutdown path drains it below, so
            // their eventual close is still observed rather than being lost entirely.
            std::lock_guard<std::mutex> lock{ m_lock };
            m_pendingCloseContexts.push_back(closeContext);
        }
    }

    // Drain contexts left behind by earlier timed-out closes. Shutdown only.
    //
    // These exist to protect provider destruction: the destructor closes the WinHTTP session
    // handles a straggler may still be using. Suspend destroys nothing, so draining there buys
    // nothing and costs real time - each retained context would burn its full timeout in sequence,
    // and these are by definition the connections least likely to ever report, so worst case adds
    // N x the timeout to the suspend budget. That is precisely the watchdog kill the bounded wait
    // above exists to avoid, and it would delay the E_ABORT completions below by the same amount.
    //
    // Bounded even here rather than INFINITE: a connection that already missed one deadline is the
    // one most likely never to report, and blocking HCCleanup forever would be worse than the race
    // this closes. The unbounded wait above still covers connections closed by this call, which is
    // the case that actually protects the session handles. Best-effort second chance, not a
    // guarantee.
    if (timeoutMs == INFINITE)
    {
        constexpr DWORD c_retainedCloseTimeoutMs = 5000;

        http_internal_vector<std::shared_ptr<CloseContext>> pendingCloseContexts;
        {
            std::lock_guard<std::mutex> lock{ m_lock };
            pendingCloseContexts.swap(m_pendingCloseContexts);
        }

        for (auto& pendingContext : pendingCloseContexts)
        {
            if (WaitForSingleObject(pendingContext->connectionsClosedEvent, c_retainedCloseTimeoutMs) == WAIT_TIMEOUT)
            {
                HC_TRACE_ERROR(HTTPCLIENT, "WinHttpProvider::CloseAllConnections: %llu connection(s) from an earlier close still open",
                    static_cast<unsigned long long>(pendingContext->openConnections.load()));

                std::lock_guard<std::mutex> lock{ m_lock };
                m_pendingCloseContexts.push_back(pendingContext);
            }
        }
    }

    // Every connection is gone, so nothing is in flight. Reset the budget rather than relying on
    // per-request release, and fail anything still queued: those requests never reached WinHTTP and
    // there is no longer a completion that would start them, so callers must not be left waiting.
    http_internal_list<PendingRequest> abandoned;
    {
        std::lock_guard<std::mutex> lock{ m_lock };
        m_activeRequestCount = 0;

        // Invalidate every slot reserved so far. A request that reserved a slot before this reset
        // can still run its completion afterwards (it may have been mid-flight, or its connection
        // may already have been destroyed and so skipped by the callback-clearing loop above);
        // without this its release would decrement a count that was just zeroed and underflow it.
        ++m_requestEpoch;

        abandoned.swap(m_pendingRequests);
    }

    for (auto& pending : abandoned)
    {
        HCHttpCallResponseSetNetworkErrorCode(pending.callHandle, E_ABORT, static_cast<uint32_t>(E_ABORT));
        XAsyncComplete(pending.async, S_OK, 0);
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
    SessionKey const sessionKey{ securityProtocolFlags, isHttps };
    auto iter = m_hSessions.find(sessionKey);
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

    if (hSession == nullptr && GetLastError() == ERROR_INVALID_PARAMETER && isHttps)
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
        DWORD openErr = GetLastError();
        HRESULT hr = openErr != 0 ? HRESULT_FROM_WIN32(openErr) : E_FAIL;
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
                DWORD openErr = GetLastError();
                HRESULT openHr = openErr != 0 ? HRESULT_FROM_WIN32(openErr) : E_FAIL;
                HC_TRACE_ERROR_HR(HTTPCLIENT, openHr, "WinHttpProvider fallback WinHttpOpen with WINHTTP_FLAG_ASYNC failed");
                return openHr;
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

    m_hSessions[sessionKey] = hSession;

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

    // Held across the entire sequence, including the blocking drain in CloseAllConnections, so a
    // concurrent Resume or provider teardown cannot interleave with a suspend in progress.
    std::lock_guard<std::mutex> suspendLock{ m_suspendLock };

    {
        std::lock_guard<std::mutex> lock{ m_lock };

        if (m_isSuspended)
        {
            HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpProvider::Suspend called while already suspended, ignoring");
            return;
        }
        m_isSuspended = true;
    }

    // Bounded on suspend: exceeding the platform's suspend budget gets the title killed by the
    // watchdog, which is worse than proceeding with a connection that has not reported closed.
    constexpr DWORD c_suspendCloseTimeoutMs = 2000;
    HRESULT hr = CloseAllConnections(c_suspendCloseTimeoutMs);
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

    // Blocks until any in-progress Suspend has fully completed, so resume can never race ahead of
    // the teardown and let new requests start against half-destroyed state.
    std::lock_guard<std::mutex> suspendLock{ m_suspendLock };

    std::lock_guard<std::mutex> lock{ m_lock };

    if (!m_isSuspended)
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpProvider::Resume called while not suspended, ignoring");
        return;
    }
    m_isSuspended = false;
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

void WinHttp_WebSocketProvider::OnSuspending() noexcept
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    WinHttpProvider->Suspend();
#endif
}

void WinHttp_WebSocketProvider::OnResuming() noexcept
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    WinHttpProvider->Resume();
#endif
}
#endif // !HC_NOWEBSOCKETS

NAMESPACE_XBOX_HTTP_CLIENT_END
