// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "httpcall.h"
#include "uri.h"
#include "winhttp_connection.h"
#include <schannel.h>

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XNetworking.h>
#include <XGameRuntimeFeature.h>
#include <winsock2.h>
#include <iphlpapi.h>
#endif

using namespace xbox::httpclient;

#define CRLF L"\r\n"
#define WINHTTP_WEBSOCKET_RECVBUFFER_INITIAL_SIZE (1024 * 4)

#ifndef WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET
#define WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET 114
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

WinHttpConnection::WinHttpConnection(
    HINTERNET hSession,
    HCCallHandle call,
    proxy_type proxyType,
    XPlatSecurityInformation&& securityInformation
) :
    m_hSession{ hSession },
    m_call{ call },
    m_proxyType{ proxyType },
    m_securityInformation{ std::move(securityInformation) },
    m_winHttpWebSocketExports{ WinHttpProvider::GetWinHttpWebSocketExports() }
{
}

WinHttpConnection::~WinHttpConnection()
{
    HC_TRACE_VERBOSE(HTTPCLIENT, __FUNCTION__);

    if (m_state == ConnectionState::WebSocketConnected && m_hRequest && m_winHttpWebSocketExports.close)
    {
        // Use WinHttpWebSocketClose rather than disconnect in this case to close both the send and receive channels
        m_winHttpWebSocketExports.close(m_hRequest, static_cast<USHORT>(HCWebSocketCloseStatus::GoingAway), nullptr, 0);
    }
    if (m_websocketCall)
    {
        HCHttpCallCloseHandle(m_websocketCall);
    }

    if (m_hRequest != nullptr)
    {
        WinHttpCloseHandle(m_hRequest);
    }
    if (m_hConnection != nullptr)
    {
        WinHttpCloseHandle(m_hConnection);
    }
}

Result<std::shared_ptr<WinHttpConnection>> WinHttpConnection::Initialize(
    HINTERNET hSession,
    HCCallHandle call,
    proxy_type proxyType,
    XPlatSecurityInformation&& securityInformation
)
{
    RETURN_HR_IF(E_INVALIDARG, !hSession);
    RETURN_HR_IF(E_INVALIDARG, !call);

    http_stl_allocator<WinHttpConnection> a{};
    auto connection = std::shared_ptr<WinHttpConnection>{ new (a.allocate(1)) WinHttpConnection(hSession, call, proxyType, std::move(securityInformation)), http_alloc_deleter<WinHttpConnection>()  };
    RETURN_IF_FAILED(connection->Initialize());

    return connection;
}

#if !HC_NOWEBSOCKETS
Result<std::shared_ptr<WinHttpConnection>> WinHttpConnection::Initialize(
    HINTERNET hSession,
    HCWebsocketHandle webSocket,
    const char* uri,
    const char* /*subprotocol*/,
    proxy_type proxyType,
    XPlatSecurityInformation&& securityInformation
)
{
    RETURN_HR_IF(E_INVALIDARG, !webSocket);

    // For WebSocket connections, create a dummy HCHttpCall so that the rest of the logic can be reused more easily
    HCCallHandle webSocketCall{};
    RETURN_IF_FAILED(HCHttpCallCreate(&webSocketCall));
    RETURN_IF_FAILED(HCHttpCallRequestSetUrl(webSocketCall, "GET", uri));

    auto initResult = WinHttpConnection::Initialize(hSession, webSocketCall, proxyType, std::move(securityInformation));
    if (FAILED(initResult.hr))
    {
        HCHttpCallCloseHandle(webSocketCall);
        return initResult.hr;
    }
    else
    {
        // WinHttpConnection now owns webSocketCall
        initResult.Payload()->m_websocketCall = webSocketCall;
        initResult.Payload()->m_websocketHandle = webSocket;
        return std::move(initResult);
    }
}
#endif

http_internal_wstring flatten_http_headers(_In_ const HttpHeaders& headers)
{
    http_internal_wstring flattened_headers;

    bool foundUserAgent = false;
    for (const auto& header : headers)
    {
        auto wHeaderName = utf16_from_utf8(header.first);
        if (wHeaderName == L"User-Agent")
        {
            foundUserAgent = true;
        }

        flattened_headers.append(wHeaderName);
        flattened_headers.push_back(L':');
        flattened_headers.append(utf16_from_utf8(header.second));
        flattened_headers.append(CRLF);
    }

    if (!foundUserAgent)
    {
        flattened_headers.append(L"User-Agent:libHttpClient/1.0.0.0\r\n");
    }

    return flattened_headers;
}

HRESULT WinHttpConnection::Initialize()
{
    try
    {
        const char* url = nullptr;
        const char* method = nullptr;
        RETURN_IF_FAILED(HCHttpCallRequestGetUrl(m_call, &method, &url));

        m_uri = Uri{ url };
        unsigned int port = m_uri.IsPortDefault() ? (m_uri.IsSecure() ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT) : m_uri.Port();

        http_internal_wstring wUrlHost = utf16_from_utf8(m_uri.Host());
        m_hConnection = WinHttpConnect(
            m_hSession,
            wUrlHost.c_str(),
            (INTERNET_PORT)port,
            0);

        if (m_hConnection == nullptr)
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpConnect errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
            return HRESULT_FROM_WIN32(dwError);
        }

        // Need to form uri path, query, and fragment for this request.
        http_internal_wstring wEncodedResource = utf16_from_utf8(m_uri.Resource());
        http_internal_wstring wMethod = utf16_from_utf8(method);

        // Open the request.
        m_hRequest = WinHttpOpenRequest(
            m_hConnection,
            wMethod.c_str(),
            wEncodedResource.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_ESCAPE_DISABLE | (m_uri.IsSecure() ? WINHTTP_FLAG_SECURE : 0));

        if (m_hRequest == nullptr)
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpOpenRequest errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
            return HRESULT_FROM_WIN32(dwError);
        }

        uint32_t timeoutInSeconds = 0;
        RETURN_IF_FAILED(HCHttpCallRequestGetTimeout(m_call, &timeoutInSeconds));

        int timeoutInMilliseconds = static_cast<int>(timeoutInSeconds * 1000);
        if (!WinHttpSetTimeouts(
            m_hSession,
            timeoutInMilliseconds,
            timeoutInMilliseconds,
            timeoutInMilliseconds,
            timeoutInMilliseconds))
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpSetTimeouts errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
            return HRESULT_FROM_WIN32(dwError);
        }

#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
        if (!m_call->sslValidation)
        {
            DWORD dwOption = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
            if (!WinHttpSetOption(
                m_hRequest,
                WINHTTP_OPTION_SECURITY_FLAGS,
                &dwOption,
                sizeof(dwOption)))
            {
                DWORD dwError = GetLastError();
                HC_TRACE_WARNING(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpSetOption errorcode %d", HCHttpCallGetId(m_call), GetCurrentThreadId(), dwError);
            }
        }
#endif

#if HC_PLATFORM != HC_PLATFORM_GDK
        if (m_proxyType == proxy_type::autodiscover_proxy)
        {
            RETURN_IF_FAILED(set_autodiscover_proxy());
        }
#endif

        HCHttpCallRequestBodyReadFunction requestBodyReadFunction{ nullptr };
        void* context{ nullptr };
        RETURN_IF_FAILED(HCHttpCallRequestGetRequestBodyReadFunction(m_call, &requestBodyReadFunction, &m_requestBodySize, &context));

        if (m_requestBodySize > 0)
        {
            // While we won't be transfer-encoding the data, we will write it in portions.
            m_requestBodyType = msg_body_type::content_length_chunked;
            m_requestBodyRemainingToWrite = m_requestBodySize;
        }
        else
        {
            m_requestBodyType = msg_body_type::no_body;
            m_requestBodyRemainingToWrite = 0;
        }

        uint32_t numHeaders = 0;
        RETURN_IF_FAILED(HCHttpCallRequestGetNumHeaders(m_call, &numHeaders));

        if (numHeaders > 0)
        {
            http_internal_wstring flattenedHeaders = flatten_http_headers(m_call->requestHeaders);
            if (!WinHttpAddRequestHeaders(
                m_hRequest,
                flattenedHeaders.c_str(),
                static_cast<DWORD>(flattenedHeaders.length()),
                WINHTTP_ADDREQ_FLAG_ADD))
            {
                DWORD dwError = GetLastError();
                HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpAddRequestHeaders errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
                return HRESULT_FROM_WIN32(dwError);
            }
        }

        if (m_uri.IsSecure())
        {
            if (!WinHttpSetOption(
                m_hRequest,
                WINHTTP_OPTION_CLIENT_CERT_CONTEXT,
                WINHTTP_NO_CLIENT_CERT_CONTEXT,
                0))
            {
                DWORD dwError = GetLastError();
                HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpSetOption errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
                return HRESULT_FROM_WIN32(dwError);
            }
        }
    }
    catch (std::bad_alloc const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [%d] std::bad_alloc: %s", E_OUTOFMEMORY, e.what());
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [%d] unknown exception", E_FAIL);
        return E_FAIL;
    }

    return S_OK;
}

HRESULT WinHttpConnection::HttpCallPerformAsync(XAsyncBlock* async)
{
    RETURN_HR_IF(E_INVALIDARG, !async);
    m_asyncBlock = async;
    return SendRequest();
}

#if !HC_NOWEBSOCKETS
HRESULT WinHttpConnection::WebSocketConnectAsync(XAsyncBlock* async)
{
    RETURN_HR_IF(E_INVALIDARG, !async);

    // Set WebSocket specific options and then call send
    auto& headers{ m_websocketHandle->websocket->Headers() };
    if (!headers.empty())
    {
        http_internal_wstring flattenedHeaders = flatten_http_headers(headers);
        if (!WinHttpAddRequestHeaders(
            m_hRequest,
            flattenedHeaders.c_str(),
            static_cast<DWORD>(flattenedHeaders.length()),
            WINHTTP_ADDREQ_FLAG_ADD))
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "winhttp_http_task [ID %llu] [TID %ul] WinHttpAddRequestHeaders errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
            return HRESULT_FROM_WIN32(dwError);
        }
    }

    // Request protocol upgrade from http to websocket.
    #pragma warning(push)
    #pragma warning(disable : 6387)  // WinHttpSetOption's SAL doesn't understand WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET
    bool status = WinHttpSetOption(m_hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
    if (!status)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "winhttp_http_task [ID %llu] [TID %ul] WinHttpAddRequestHeaders errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }
    #pragma warning(pop)

    m_asyncBlock = async;

    // Unlike HTTP, WebSocket providers need to implement the XAsyncProvider for each operation
    RETURN_IF_FAILED(XAsyncBegin(async, this, HCWebSocketConnectAsync, "HCWebSocketConnectAsync", WinHttpConnection::WebSocketConnectProvider));

    return S_OK;
}

HRESULT WinHttpConnection::WebSocketSendMessageAsync(XAsyncBlock* async, const char* message)
{
    return WebSocketSendMessageAsync(async, (const uint8_t*)message, strlen(message), WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE);
}

HRESULT WinHttpConnection::WebSocketSendMessageAsync(XAsyncBlock* async, const uint8_t* payloadBytes, size_t payloadSize, WINHTTP_WEB_SOCKET_BUFFER_TYPE payloadType)
{
    auto sendContext = http_allocate_unique<WebSocketSendContext>();
    sendContext->async = async;
    sendContext->connection = this;
    sendContext->socket = m_websocketHandle; // Store socket handle because 'connection' may be invalid in WebSocketSendProvider GetResult
    sendContext->payload = http_internal_vector<uint8_t>(payloadBytes, payloadBytes + payloadSize);
    sendContext->payloadType = payloadType;

    RETURN_IF_FAILED(XAsyncBegin(async, sendContext.get(), HCWebSocketSendMessageAsync, "HCWebSocketSendMessageAsync", WinHttpConnection::WebSocketSendProvider));

    // At this point WebSocketSendProvider is responsible for lifetime of sendContext
    sendContext.release();

    return S_OK;
}

HRESULT WinHttpConnection::WebSocketDisconnect(_In_ HCWebSocketCloseStatus closeStatus)
{
    assert(m_winHttpWebSocketExports.shutdown);

    {
        win32_cs_autolock autoCriticalSection(&m_lock);
        m_state = ConnectionState::WebSocketClosing;
    }

    // Shutdown closes the send channel after sending a close frame. When we receive a close frame we are fully disconnected
    DWORD dwError = m_winHttpWebSocketExports.shutdown(m_hRequest, static_cast<short>(closeStatus), nullptr, 0);
    return HRESULT_FROM_WIN32(dwError);
}
#endif

HRESULT WinHttpConnection::Close(ConnectionClosedCallback callback)
{
    bool doWebSocketClose = false;
    bool doWinHttpClose = false;
    bool closeComplete = false;

    {
        win32_cs_autolock autoCriticalSection(&m_lock);

        if (m_connectionClosedCallback)
        {
            // WinHttpProvider shouldn't close connection more than once
            assert(!m_connectionClosedCallback);
            return E_UNEXPECTED;
        }
        m_connectionClosedCallback = std::move(callback);

        switch (m_state)
        {
        case ConnectionState::WebSocketConnected:
        {
            doWebSocketClose = true;
            m_state = ConnectionState::WebSocketClosing;
            break;
        }
        case ConnectionState::WebSocketClosing:
        {
            // Nothing to do. WinHttpClose will happen after websocket close completes
            return S_OK;
        }
        case ConnectionState::WinHttpClosing:
        {
            // Nothing to do
            return S_OK;
        }
        case ConnectionState::Closed:
        {
            closeComplete = true;
            break;
        }
        default:
        {
            doWinHttpClose = true;
            break;
        }
        }
    }

    if (doWebSocketClose)
    {
        assert(m_winHttpWebSocketExports.close);
        DWORD result = m_winHttpWebSocketExports.close(m_hRequest, static_cast<USHORT>(HCWebSocketCloseStatus::GoingAway), nullptr, 0);
        return HRESULT_FROM_WIN32(result);
    }
    else if (doWinHttpClose)
    {
        return StartWinHttpClose();
    }
    else if (closeComplete)
    {
        m_connectionClosedCallback();
    }

    return S_OK;
}

void WinHttpConnection::complete_task(_In_ HRESULT translatedHR)
{
    complete_task(translatedHR, translatedHR);
}

void WinHttpConnection::complete_task(_In_ HRESULT translatedHR, uint32_t platformSpecificError)
{
    if (m_asyncBlock != nullptr)
    {
        // WebSocket Connect XAyncProvider will pull connect result from m_call
        HCHttpCallResponseSetNetworkErrorCode(m_call, translatedHR, platformSpecificError);

        size_t resultSize{ 0 };
#if !HC_NOWEBSOCKETS
        if (m_websocketHandle)
        {
            resultSize = sizeof(WebSocketCompletionResult);
        }
#endif
        XAsyncComplete(m_asyncBlock, S_OK, resultSize);
        m_asyncBlock = nullptr;
    }

    if (!m_websocketHandle)
    {
        StartWinHttpClose();
    }
}

// Helper function to query/read next part of response data from winhttp.
void WinHttpConnection::read_next_response_chunk(_In_ WinHttpConnection* pRequestContext, DWORD /*bytesRead*/)
{
    if (!WinHttpQueryDataAvailable(pRequestContext->m_hRequest, nullptr))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpQueryDataAvailable errorcode %d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
    }
}

void WinHttpConnection::_multiple_segment_write_data(_In_ WinHttpConnection* pRequestContext)
{
    const size_t defaultChunkSize = 64 * 1024;
    size_t safeSize = std::min(pRequestContext->m_requestBodyRemainingToWrite, defaultChunkSize);

    HCHttpCallRequestBodyReadFunction readFunction = nullptr;
    size_t bodySize = 0;
    void* context = nullptr;
    HRESULT hr = HCHttpCallRequestGetRequestBodyReadFunction(pRequestContext->m_call, &readFunction, &bodySize, &context);
    if (FAILED(hr))
    {
        pRequestContext->complete_task(hr);
        return;
    }

    if (readFunction == nullptr) {
        pRequestContext->complete_task(E_UNEXPECTED);
        return;
    }

    size_t bytesWritten = 0;
    try
    {
        pRequestContext->m_requestBuffer.resize(safeSize);

        hr = readFunction(pRequestContext->m_call, pRequestContext->m_requestBodyOffset, safeSize, context, pRequestContext->m_requestBuffer.data(), &bytesWritten);
        if (FAILED(hr))
        {
            pRequestContext->complete_task(hr);
            return;
        }
    }
    catch (...)
    {
        pRequestContext->complete_task(E_FAIL, static_cast<uint32_t>(E_FAIL));
        return;
    }

    if( !WinHttpWriteData(
        pRequestContext->m_hRequest,
        pRequestContext->m_requestBuffer.data(),
        static_cast<DWORD>(bytesWritten),
        nullptr))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpWriteData errorcode %d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return;
    }

    // Stop writing chunks after this one if no more data.
    pRequestContext->m_requestBodyRemainingToWrite -= bytesWritten;
    if (pRequestContext->m_requestBodyRemainingToWrite == 0)
    {
        pRequestContext->m_requestBodyType = msg_body_type::no_body;
    }
    pRequestContext->m_requestBodyOffset += bytesWritten;
}

void WinHttpConnection::callback_status_write_complete(
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* statusInfo)
{
    {
        win32_cs_autolock autoCriticalSection(&pRequestContext->m_lock);

        DWORD bytesWritten = *((DWORD *)statusInfo);
        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE bytesWritten=%d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), bytesWritten);
        UNREFERENCED_LOCAL(bytesWritten);

        if (pRequestContext->m_requestBodyType == content_length_chunked)
        {
            _multiple_segment_write_data(pRequestContext);
            return;
        }
    }

    if (!WinHttpReceiveResponse(hRequestHandle, nullptr))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpReceiveResponse errorcode %d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return;
    }
}

void WinHttpConnection::callback_websocket_status_write_complete(WinHttpConnection* connection)
{
#if !HC_NOWEBSOCKETS
    WebSocketSendContext* nextSendContext{ nullptr };
    WebSocketSendContext* completedSendContext{ nullptr };

    {
        std::lock_guard<std::recursive_mutex> lock{ connection->m_websocketSendMutex };

        assert(!connection->m_websocketSendQueue.empty());
        completedSendContext = connection->m_websocketSendQueue.front();
        connection->m_websocketSendQueue.pop();

        if (!connection->m_websocketSendQueue.empty())
        {
            nextSendContext = connection->m_websocketSendQueue.front();
        }
    }

    assert(completedSendContext);
    XAsyncComplete(completedSendContext->async, S_OK, sizeof(WebSocketCompletionResult));

    if (nextSendContext)
    {
        connection->WebSocketSendMessage(*nextSendContext);
    }
#else
    UNREFERENCED_PARAMETER(connection);
    assert(false);
#endif
}

void WinHttpConnection::callback_status_request_error(
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* statusInfo)
{
    WINHTTP_ASYNC_RESULT *error_result = reinterpret_cast<WINHTTP_ASYNC_RESULT *>(statusInfo);
    if (error_result == nullptr)
        return;

    DWORD errorCode = error_result->dwError;
    HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_REQUEST_ERROR dwResult=%d dwError=%d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), error_result->dwResult, error_result->dwError);

    bool reissueSend{ false };

    if (error_result->dwError == ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED)
    {
        SecPkgContext_IssuerListInfoEx* pIssuerList{ nullptr };
        DWORD dwBufferSize{ sizeof(void*) };

        if (WinHttpQueryOption(
            hRequestHandle,
            WINHTTP_OPTION_CLIENT_CERT_ISSUER_LIST,
            &pIssuerList,
            &dwBufferSize
        ))
        {
            PCERT_CONTEXT pClientCert{ nullptr };
            PCCERT_CHAIN_CONTEXT pClientCertChain{ nullptr };

            CERT_CHAIN_FIND_BY_ISSUER_PARA searchCriteria{};
            searchCriteria.cbSize = sizeof(CERT_CHAIN_FIND_BY_ISSUER_PARA);
            searchCriteria.cIssuer = pIssuerList->cIssuers;
            searchCriteria.rgIssuer = pIssuerList->aIssuers;

            HCERTSTORE hCertStore = CertOpenSystemStore(0, L"MY");
            if (hCertStore)
            {
                pClientCertChain = CertFindChainInStore(
                    hCertStore,
                    X509_ASN_ENCODING,
                    CERT_CHAIN_FIND_BY_ISSUER_CACHE_ONLY_URL_FLAG | CERT_CHAIN_FIND_BY_ISSUER_CACHE_ONLY_FLAG,
                    CERT_CHAIN_FIND_BY_ISSUER,
                    &searchCriteria,
                    nullptr
                );

                if (pClientCertChain)
                {
                    pClientCert = (PCERT_CONTEXT)pClientCertChain->rgpChain[0]->rgpElement[0]->pCertContext;

                    // "!!" to cast from BOOL to bool
                    reissueSend = !!WinHttpSetOption(
                        hRequestHandle,
                        WINHTTP_OPTION_CLIENT_CERT_CONTEXT,
                        (LPVOID)pClientCert,
                        sizeof(CERT_CONTEXT)
                    );

                    CertFreeCertificateChain(pClientCertChain);
                }
                CertCloseStore(hCertStore, 0);
            }
            GlobalFree(pIssuerList);
        }
        else
        {
            auto certError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttp returned ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED but unable to get cert issuer list, dwError=%d", certError);
        }
    }

    if (reissueSend)
    {
        HRESULT hr = pRequestContext->SendRequest();
        if (FAILED(hr))
        {
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection Failure to send HTTP request 0x%0.8x", hr);
            pRequestContext->complete_task(E_FAIL, hr);
        }
    }
    else
    {
        if (pRequestContext->m_websocketHandle && (pRequestContext->m_state == ConnectionState::WebSocketConnected || pRequestContext->m_state == ConnectionState::WebSocketClosing))
        {
            // Only trigger if we're already connected, never during a connection attempt
            if (pRequestContext->m_asyncBlock == nullptr)
            {
                pRequestContext->on_websocket_disconnected(static_cast<USHORT>(errorCode));
            }
        }

        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(errorCode));
    }
}


#if HC_PLATFORM == HC_PLATFORM_GDK
void WinHttpConnection::callback_status_sending_request(
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* /*statusInfo*/)
{
    if (hRequestHandle != nullptr)
    {
        HRESULT hr = XNetworkingVerifyServerCertificate(hRequestHandle, pRequestContext->m_securityInformation.securityInformation);
        if (FAILED(hr))
        {
            win32_cs_autolock autoCriticalSection(&pRequestContext->m_lock);

            pRequestContext->complete_task(hr, hr);

            // Set the failure and complete the web request before calling WinHttpCloseHandle because the
            // WinHttpCloseHandle call can cause a synchronous callback indicating the request has been canceled
            // which would complete the web request with the wrong error.
            (void)WinHttpCloseHandle(hRequestHandle);
            pRequestContext->m_hRequest = nullptr;
        }
    }
}
#endif

void WinHttpConnection::callback_status_sendrequest_complete(
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* /*statusInfo*/)
{
    {
        win32_cs_autolock autoCriticalSection(&pRequestContext->m_lock);

        HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId());

        if (pRequestContext->m_requestBodyType == content_length_chunked)
        {
            _multiple_segment_write_data(pRequestContext);
            return;
        }
    }

    if (!WinHttpReceiveResponse(hRequestHandle, nullptr))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpReceiveResponse errorcode %d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return;
    }
}

HRESULT WinHttpConnection::query_header_length(
    _In_ HCCallHandle call,
    _In_ HINTERNET hRequestHandle,
    _In_ DWORD header,
    _Out_ DWORD* pLength)
{
    *pLength = 0;

    if (!WinHttpQueryHeaders(
        hRequestHandle,
        header,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        pLength,
        WINHTTP_NO_HEADER_INDEX))
    {
        DWORD dwError = GetLastError();
        if (dwError != ERROR_INSUFFICIENT_BUFFER)
        {
            HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpQueryHeaders errorcode %d", TO_ULL(HCHttpCallGetId(call)), GetCurrentThreadId(), dwError);
            return E_FAIL;
        }
    }

    return S_OK;
}

uint32_t WinHttpConnection::parse_status_code(
    _In_ HCCallHandle call,
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpConnection* pRequestContext
    )
{
    DWORD length = 0;
    HRESULT hr = query_header_length(pRequestContext->m_call, hRequestHandle, WINHTTP_QUERY_STATUS_CODE, &length);
    if (FAILED(hr))
    {
        pRequestContext->complete_task(E_FAIL, hr);
        return 0;
    }

    http_internal_wstring buffer;
    buffer.resize(length);

    if (!WinHttpQueryHeaders(
        hRequestHandle,
        WINHTTP_QUERY_STATUS_CODE,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &buffer[0],
        &length,
        WINHTTP_NO_HEADER_INDEX))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpQueryHeaders errorcode %d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return 0;
    }

    uint32_t statusCode = static_cast<uint32_t>(_wtoi(buffer.c_str()));
    HCHttpCallResponseSetStatusCode(call, statusCode);

    return statusCode;
}


void WinHttpConnection::parse_headers_string(
    _In_ HCCallHandle call,
    _In_ wchar_t* headersStr)
{
    wchar_t* context = nullptr;
    wchar_t* line = wcstok_s(headersStr, CRLF, &context);
    while (line != nullptr)
    {
        http_internal_wstring header_line(line);
        const size_t colonIndex = header_line.find_first_of(L":");
        if (colonIndex != http_internal_wstring::npos)
        {
            http_internal_wstring key = header_line.substr(0, colonIndex);
            http_internal_wstring value = header_line.substr(colonIndex + 1, header_line.length() - colonIndex - 1);
            trim_whitespace(key);
            trim_whitespace(value);

            http_internal_string aKey = utf8_from_utf16(key);
            http_internal_string aValue = utf8_from_utf16(value);
            HCHttpCallResponseSetHeader(call, aKey.c_str(), aValue.c_str());
        }
        line = wcstok_s(nullptr, CRLF, &context);
    }
}

void WinHttpConnection::callback_status_headers_available(
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* /*statusInfo*/)
{
    HC_TRACE_INFORMATION(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId() );

    // First need to query to see what the headers size is.
    DWORD headerBufferLength = 0;
    HRESULT hr = query_header_length(pRequestContext->m_call, hRequestHandle, WINHTTP_QUERY_RAW_HEADERS_CRLF, &headerBufferLength);
    if (FAILED(hr))
    {
        pRequestContext->complete_task(E_FAIL, hr);
        return;
    }

    // Now allocate buffer for headers and query for them.
    http_internal_vector<unsigned char> header_raw_buffer;
    header_raw_buffer.resize(headerBufferLength);
    wchar_t* headerBuffer = reinterpret_cast<wchar_t*>(&header_raw_buffer[0]);
    if (!WinHttpQueryHeaders(
        hRequestHandle,
        WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX,
        headerBuffer,
        &headerBufferLength,
        WINHTTP_NO_HEADER_INDEX))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpQueryHeaders errorcode %d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), dwError);
        pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return;
    }

    parse_status_code(pRequestContext->m_call, hRequestHandle, pRequestContext);
    parse_headers_string(pRequestContext->m_call, headerBuffer);
    read_next_response_chunk(pRequestContext, 0);
}

void WinHttpConnection::callback_status_data_available(
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* statusInfo)
{
    pRequestContext->m_lock.lock();

    // Status information contains pointer to DWORD containing number of bytes available.
    DWORD newBytesAvailable = *(PDWORD)statusInfo;

    HC_TRACE_INFORMATION(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE newBytesAvailable=%d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), newBytesAvailable);

    // Flush response buffer from previous call to WinHttpReadData
    if (pRequestContext->m_responseBuffer.size())
    {
        HRESULT hr = flush_response_buffer(pRequestContext);
        if (FAILED(hr)) {
            pRequestContext->m_lock.unlock();
            pRequestContext->complete_task(hr);
            return;
        }
    }

    // Read new data into buffer
    if (newBytesAvailable > 0)
    {
        pRequestContext->m_responseBuffer.resize(newBytesAvailable);

        // Read in body all at once.
        if (!WinHttpReadData(
            hRequestHandle,
            pRequestContext->m_responseBuffer.data(),
            newBytesAvailable,
            nullptr))
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpReadData errorcode %d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), GetLastError());
            pRequestContext->m_lock.unlock();
            pRequestContext->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
            return;
        }
        pRequestContext->m_lock.unlock();
    }
    else
    {
        pRequestContext->m_lock.unlock();
        // No more data available
        pRequestContext->complete_task(S_OK);
    }
}

void WinHttpConnection::callback_status_read_complete(
    _In_ HINTERNET /*hRequestHandle*/,
    _In_ WinHttpConnection* pRequestContext,
    _In_ DWORD statusInfoLength)
{
    // Status information length contains the number of bytes read.
    const DWORD bytesRead = statusInfoLength;

    HC_TRACE_INFORMATION(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_READ_COMPLETE bytesRead=%d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), bytesRead);

    // If no bytes have been read, then this is the end of the response.
    if (bytesRead == 0)
    {
        HRESULT hr = S_OK;
        {
            win32_cs_autolock autoCriticalSection(&pRequestContext->m_lock);

            // Flush remaining buffered data
            hr = flush_response_buffer(pRequestContext);
        }
        pRequestContext->complete_task(hr);
    }
    else
    {
        read_next_response_chunk(pRequestContext, bytesRead);
    }
}

HRESULT WinHttpConnection::flush_response_buffer(
    _In_ WinHttpConnection* pRequestContext
)
{
    HCHttpCallResponseBodyWriteFunction writeFunction = nullptr;
    void* context = nullptr;
    HRESULT hr = HCHttpCallResponseGetResponseBodyWriteFunction(pRequestContext->m_call, &writeFunction, &context);
    if (FAILED(hr))
    {
        return hr;
    }

    if (writeFunction == nullptr)
    {
        return E_UNEXPECTED;
    }

    try
    {
        hr = writeFunction(pRequestContext->m_call, pRequestContext->m_responseBuffer.data(), pRequestContext->m_responseBuffer.size(), context);
        if (FAILED(hr))
        {
            return hr;
        }
    }
    catch (...)
    {
        return E_FAIL;
    }

    pRequestContext->m_responseBuffer.clear();
    return S_OK;
}

void WinHttpConnection::callback_status_secure_failure(
    _In_ HINTERNET /*hRequestHandle*/,
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* statusInfo)
{
    // Status information contains pointer to DWORD containing a bitwise-OR combination of one or more error flags.
    DWORD statusFlags = *(PDWORD)statusInfo;

    HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_SECURE_FAILURE statusFlags=%d", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId(), statusFlags);
}

struct WinHttpCallbackContext
{
    WinHttpCallbackContext(std::shared_ptr<WinHttpConnection> _winHttpConnection) : winHttpConnection{ std::move(_winHttpConnection) }
    {
    }
    ~WinHttpCallbackContext()
    {
        HC_TRACE_VERBOSE(HTTPCLIENT, "~WinHttpCallbackContext");
    }
    std::shared_ptr<WinHttpConnection> winHttpConnection;
};

void CALLBACK WinHttpConnection::completion_callback(
    HINTERNET hRequestHandle,
    DWORD_PTR context,
    DWORD statusCode,
    _In_ void* statusInfo,
    DWORD statusInfoLength)
{
    // Callback used with WinHTTP to listen for async completions.
    UNREFERENCED_PARAMETER(statusInfoLength);

    WinHttpCallbackContext* callbackContext = reinterpret_cast<WinHttpCallbackContext*>(context);
    WinHttpConnection* pRequestContext = callbackContext->winHttpConnection.get();

    try
    {
        switch (statusCode)
        {
            case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            {
                callback_status_request_error(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

#if HC_PLATFORM == HC_PLATFORM_GDK
            case WINHTTP_CALLBACK_STATUS_SENDING_REQUEST:
            {
                callback_status_sending_request(hRequestHandle, pRequestContext, statusInfo);
                break;
            }
#endif // HC_PLATFORM_GDK

            case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
            {
                callback_status_sendrequest_complete(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            {
                if (pRequestContext->m_websocketHandle)
                {
                    callback_websocket_status_headers_available(hRequestHandle, callbackContext);
                }
                else
                {
                    callback_status_headers_available(hRequestHandle, pRequestContext, statusInfo);
                }
                break;
            }

            case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
            {
                callback_status_data_available(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            {
                if (pRequestContext->m_websocketHandle)
                {
                    callback_websocket_status_read_complete(pRequestContext, statusInfo);
                }
                else
                {
                    callback_status_read_complete(hRequestHandle, pRequestContext, statusInfoLength);
                }
                break;
            }

            case WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE:
            {
                if (pRequestContext->m_websocketHandle)
                {
                    callback_websocket_status_write_complete(pRequestContext);
                }
                else
                {
                    callback_status_write_complete(hRequestHandle, pRequestContext, statusInfo);
                }
                break;
            }

            case WINHTTP_CALLBACK_STATUS_CLOSE_COMPLETE:
            {
                if (pRequestContext->m_websocketHandle)
                {
                    assert(pRequestContext->m_winHttpWebSocketExports.queryCloseStatus);

                    USHORT closeReason = 0;
                    DWORD dwReasonLengthConsumed = 0;
                    pRequestContext->m_winHttpWebSocketExports.queryCloseStatus(pRequestContext->m_hRequest, &closeReason, nullptr, 0, &dwReasonLengthConsumed);

                    pRequestContext->on_websocket_disconnected(closeReason);
                }
                break;
            }

            case WINHTTP_CALLBACK_STATUS_SECURE_FAILURE:
            {
                callback_status_secure_failure(hRequestHandle, pRequestContext, statusInfo);
                break;
            }

            case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
            {
                // For WebSocket, we will also get a notification when the original request handle is closed. We have no action to take in that case
                if (hRequestHandle == pRequestContext->m_hRequest)
                {
                    HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING", TO_ULL(HCHttpCallGetId(pRequestContext->m_call)), GetCurrentThreadId());

                    // WinHttp Shutdown complete. WinHttp guarantees we will get no more callbacks for this request so we can safely cleanup context.
                    // Ensure WinHttpCallbackContext is cleaned before invoking callback in case this is happening during HCCleanup
                    HC_UNIQUE_PTR<WinHttpCallbackContext> reclaim{ callbackContext };

                    ConnectionClosedCallback connectionClosedCallback{};
                    {
                        win32_cs_autolock cs{ &pRequestContext->m_lock };
                        pRequestContext->m_hRequest = nullptr;
                        connectionClosedCallback = std::move(pRequestContext->m_connectionClosedCallback);
                        pRequestContext->m_state = ConnectionState::Closed;
                    }
                    reclaim.reset();

                    if (connectionClosedCallback)
                    {
                        connectionClosedCallback();
                    }
                }
                break;
            }

            default:
            {
                HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpConnection WinHttp callback statusCode=%ul", statusCode);
                break;
            }
        }
    }
    catch (std::bad_alloc const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [%d] std::bad_alloc in completion_callback: %s", E_OUTOFMEMORY, e.what());
        pRequestContext->complete_task(E_OUTOFMEMORY);
    }
    catch (std::exception const& e)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [%d] std::exception in completion_callback: %s", E_FAIL, e.what());
        pRequestContext->complete_task(E_FAIL);
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [%d] unknown exception in completion_callback", E_FAIL);
        pRequestContext->complete_task(E_FAIL);
    }
}

#if HC_PLATFORM != HC_PLATFORM_GDK
HRESULT WinHttpConnection::set_autodiscover_proxy()
{
    WINHTTP_PROXY_INFO info = { 0 };

    WINHTTP_AUTOPROXY_OPTIONS autoproxy_options;
    memset(&autoproxy_options, 0, sizeof(WINHTTP_AUTOPROXY_OPTIONS));
    memset(&info, 0, sizeof(WINHTTP_PROXY_INFO));

    autoproxy_options.dwFlags = WINHTTP_AUTOPROXY_AUTO_DETECT;
    autoproxy_options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    autoproxy_options.fAutoLogonIfChallenged = TRUE;

    auto result = WinHttpGetProxyForUrl(
        m_hSession,
        utf16_from_utf8(m_uri.FullPath()).c_str(),
        &autoproxy_options,
        &info);
    if (result)
    {
        result = WinHttpSetOption(
            m_hRequest,
            WINHTTP_OPTION_PROXY,
            &info,
            sizeof(WINHTTP_PROXY_INFO));
        if (!result)
        {
            DWORD dwError = GetLastError();
            HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpSetOption errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
        }
    }
    else
    {
        // Failure to download the auto-configuration script is not fatal. Fall back to the default proxy.
    }

    return S_OK;
}
#endif

HRESULT WinHttpConnection::SendRequest()
{
    HC_TRACE_VERBOSE(HTTPCLIENT, "WinHttpConnection [%d] SendRequest", TO_ULL(HCHttpCallGetId(m_call)));

    HC_UNIQUE_PTR<WinHttpCallbackContext> context = http_allocate_unique<WinHttpCallbackContext>(shared_from_this());

    if (WINHTTP_INVALID_STATUS_CALLBACK == WinHttpSetStatusCallback(
        m_hRequest,
        &WinHttpConnection::completion_callback,
#if HC_PLATFORM == HC_PLATFORM_GDK
        WINHTTP_CALLBACK_FLAG_SEND_REQUEST |
#endif
        WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS,
        0))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpSetStatusCallback errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    DWORD dwTotalLength = 0;
    switch (m_requestBodyType)
    {
        case msg_body_type::no_body: dwTotalLength = 0; break;
        case msg_body_type::content_length_chunked: dwTotalLength = (DWORD)m_requestBodySize; break;
        default: dwTotalLength = WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH; break;
    }

    if (!WinHttpSendRequest(
        m_hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS,
        0,
        nullptr,
        0,
        dwTotalLength,
        (DWORD_PTR)context.get()))
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpSendRequest errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    // WinHttp callback context will be reclaimed during WinHttp shutdown
    context.release();

    return S_OK;
}

HRESULT WinHttpConnection::StartWinHttpClose()
{
    {
        win32_cs_autolock autoCriticalSection(&m_lock);
        m_state = ConnectionState::WinHttpClosing;
    }

    BOOL closed = WinHttpCloseHandle(m_hRequest);
    if (!closed)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpCloseHandle failed with errorCode=%d", dwError);
        return HRESULT_FROM_WIN32(dwError);
    }

    return S_OK;
}

void WinHttpConnection::WebSocketSendMessage(const WebSocketSendContext& sendContext)
{
#if !HC_NOWEBSOCKETS
    assert(m_winHttpWebSocketExports.send);

    DWORD dwError = m_winHttpWebSocketExports.send(m_hRequest,
        sendContext.payloadType,
        (PVOID)sendContext.payload.data(),
        static_cast<DWORD>(sendContext.payload.size()));

    // If WinHttpWebSocketSend fails synchronously, complete all pending sends with that error since the only reason it can fail this way is because the WebSocket
    // is in an invalid state (i.e. closing/already closed).
    // Otherwise the callback will be invoked from the winHttp completion callback when we receive the WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE event.
    if (FAILED(HRESULT_FROM_WIN32(dwError)))
    {
        WebSocketCompleteEntireSendQueueWithError(HRESULT_FROM_WIN32(dwError));
    }
#else
    UNREFERENCED_PARAMETER(sendContext);
    assert(false);
#endif
}

void WinHttpConnection::WebSocketCompleteEntireSendQueueWithError(HRESULT error)
{
#if !HC_NOWEBSOCKETS
    std::lock_guard<std::recursive_mutex> lock{ m_websocketSendMutex };
    for (; !m_websocketSendQueue.empty(); m_websocketSendQueue.pop())
    {
        XAsyncComplete(m_websocketSendQueue.front()->async, error, 0);
    }
#else
    UNREFERENCED_PARAMETER(error);
    assert(false);
#endif
}

void WinHttpConnection::on_websocket_disconnected(_In_ USHORT closeReason)
{
#if !HC_NOWEBSOCKETS
    HCWebSocketCloseEventFunction disconnectFunc = nullptr;
    void* functionContext = nullptr;
    HCWebSocketGetEventFunctions(m_websocketHandle, nullptr, nullptr, &disconnectFunc, &functionContext);

    try
    {
        HCWebSocketCloseStatus closeStatus = static_cast<HCWebSocketCloseStatus>(closeReason);
        disconnectFunc(m_websocketHandle, closeStatus, functionContext);
    }
    catch (...)
    {
    }

    StartWinHttpClose();
#else
    UNREFERENCED_PARAMETER(closeReason);
    assert(false);
#endif
}

const char* WinHttpConnection::winhttp_web_socket_buffer_type_to_string(
    _In_ WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType
)
{
#if !HC_NOWEBSOCKETS
    switch (bufferType)
    {
        case WINHTTP_WEB_SOCKET_BUFFER_TYPE::WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE: return "WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE";
        case WINHTTP_WEB_SOCKET_BUFFER_TYPE::WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE: return "WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE";
        case WINHTTP_WEB_SOCKET_BUFFER_TYPE::WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE: return "WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE";
        case WINHTTP_WEB_SOCKET_BUFFER_TYPE::WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE: return "WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE";
        case WINHTTP_WEB_SOCKET_BUFFER_TYPE::WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE: return "WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE";
    }
    return "unknown";
#else
    UNREFERENCED_PARAMETER(bufferType);
    assert(false);
    return "";
#endif
}

void WinHttpConnection::callback_websocket_status_read_complete(
    _In_ WinHttpConnection* pRequestContext,
    _In_ void* statusInfo)
{
#if !HC_NOWEBSOCKETS
    WINHTTP_WEB_SOCKET_STATUS* wsStatus = static_cast<WINHTTP_WEB_SOCKET_STATUS*>(statusInfo);
    if (wsStatus == nullptr)
    {
        return;
    }

    HC_TRACE_VERBOSE(WEBSOCKET, "[WinHttp] callback_websocket_status_read_complete: buffer type %s", winhttp_web_socket_buffer_type_to_string(wsStatus->eBufferType));
    if (wsStatus->eBufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
    {
        assert(pRequestContext->m_winHttpWebSocketExports.queryCloseStatus);

        USHORT closeReason = 0;
        DWORD dwReasonLengthConsumed = 0;
        pRequestContext->m_winHttpWebSocketExports.queryCloseStatus(pRequestContext->m_hRequest, &closeReason, nullptr, 0, &dwReasonLengthConsumed);

        pRequestContext->on_websocket_disconnected(closeReason);
    }
    else if (wsStatus->eBufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE || wsStatus->eBufferType == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE)
    {
        bool readBufferFull{ false };
        {
            win32_cs_autolock autoCriticalSection(&pRequestContext->m_lock);
            pRequestContext->m_websocketReceiveBuffer.FinishWriteData(wsStatus->dwBytesTransferred);

            // If the receive buffer is full & at max size, invoke client fragment handler with partial message
            readBufferFull = pRequestContext->m_websocketReceiveBuffer.GetBufferByteCount() >= pRequestContext->m_websocketHandle->websocket->MaxReceiveBufferSize();
        }

        if (readBufferFull)
        {
            // Treat all message fragments as binary as they may not be null terminated
            pRequestContext->WebSocketReadComplete(true, false);
        }

        pRequestContext->WebSocketReadAsync();
    }
    else if (wsStatus->eBufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || wsStatus->eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
    {
        pRequestContext->m_websocketReceiveBuffer.FinishWriteData(wsStatus->dwBytesTransferred);
        pRequestContext->WebSocketReadComplete(wsStatus->eBufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, true);
        pRequestContext->WebSocketReadAsync();
    }
#else
    UNREFERENCED_PARAMETER(statusInfo);
    UNREFERENCED_PARAMETER(pRequestContext);
    assert(false);
#endif
}

HRESULT WinHttpConnection::WebSocketReadAsync()
{
#if !HC_NOWEBSOCKETS
    win32_cs_autolock autoCriticalSection(&m_lock);

    if (m_websocketReceiveBuffer.GetBuffer() == nullptr)
    {
        // Initialize buffer with default size of WINHTTP_WEBSOCKET_RECVBUFFER_SIZE
        RETURN_IF_FAILED(m_websocketReceiveBuffer.Resize(WINHTTP_WEBSOCKET_RECVBUFFER_INITIAL_SIZE));
    }
    else if (m_websocketReceiveBuffer.GetRemainingCapacity() == 0)
    {
        // Expand buffer
        size_t newSize = (size_t)m_websocketReceiveBuffer.GetBufferByteCount() * 2;
        if (newSize > m_websocketHandle->websocket->MaxReceiveBufferSize())
        {
            newSize = m_websocketHandle->websocket->MaxReceiveBufferSize();
        }

        RETURN_IF_FAILED(m_websocketReceiveBuffer.Resize((uint32_t)newSize));
    }

    assert(m_winHttpWebSocketExports.receive);

    uint8_t* bufferPtr = m_websocketReceiveBuffer.GetNextWriteLocation();
    uint64_t bufferSize = m_websocketReceiveBuffer.GetRemainingCapacity();
    DWORD dwError = ERROR_SUCCESS;
    DWORD bytesRead{ 0 }; // not used but required.  bytes read comes from FinishWriteData(wsStatus->dwBytesTransferred)
    UINT bufType{};
    dwError = m_winHttpWebSocketExports.receive(m_hRequest, bufferPtr, (DWORD)bufferSize, &bytesRead, &bufType);
    if (dwError)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[WinHttp] websocket_read_message [ID %llu] [TID %ul] errorcode %d", TO_ULL(HCHttpCallGetId(m_call)), GetCurrentThreadId(), dwError);
    }

    return S_OK;
#else
    assert(false);
    return E_FAIL;
#endif
}

HRESULT WinHttpConnection::WebSocketReadComplete(bool binaryMessage, bool endOfMessage)
{
#if !HC_NOWEBSOCKETS
    websocket_message_buffer messageBuffer;
    HCWebSocketMessageFunction messageFunc = nullptr;
    HCWebSocketBinaryMessageFunction binaryMessageFunc = nullptr;
    HCWebSocketBinaryMessageFragmentFunction binaryMessageFragmentFunc = nullptr;
    void* functionContext = nullptr;

    bool isFragment{ false };

    {
        win32_cs_autolock autoCriticalSection(&m_lock);
        HCWebSocketGetEventFunctions(m_websocketHandle, &messageFunc, &binaryMessageFunc, nullptr, &functionContext);
        HCWebSocketGetBinaryMessageFragmentEventFunction(m_websocketHandle, &binaryMessageFragmentFunc, &functionContext);
        m_websocketReceiveBuffer.TransferBuffer(&messageBuffer);

        // WinHttp tells us when the end of a message is received. Invoke the fragment handler if our buffer is full but we haven't yet
        // received the end of a message OR if we've previously passed along a partial message and this is a continuation.
        isFragment = !endOfMessage || m_websocketForwardingFragments;
        m_websocketForwardingFragments = !endOfMessage;
    }

    try
    {
        if (isFragment && binaryMessageFragmentFunc)
        {
            binaryMessageFragmentFunc(m_websocketHandle, messageBuffer.GetBuffer(), messageBuffer.GetBufferByteCount(), endOfMessage, functionContext);
        }
        else if (binaryMessage && binaryMessageFunc)
        {
            binaryMessageFunc(m_websocketHandle, messageBuffer.GetBuffer(), messageBuffer.GetBufferByteCount(), functionContext);
        }
        else if (!binaryMessage && messageFunc)
        {
            char* buffer = reinterpret_cast<char*>(messageBuffer.GetBuffer());
            uint32_t bufferLength = messageBuffer.GetBufferByteCount();
            buffer[bufferLength] = 0;

            messageFunc(m_websocketHandle, buffer, functionContext);
        }
    }
    catch (...)
    {
        HC_TRACE_ERROR(HTTPCLIENT, "[WinHttp] Caught unhandled exception from client message handler");
    }
    return S_OK;
#else
    UNREFERENCED_PARAMETER(binaryMessage);
    UNREFERENCED_PARAMETER(endOfMessage);
    assert(false);
    return E_FAIL;
#endif
}

void WinHttpConnection::callback_websocket_status_headers_available(
    _In_ HINTERNET hRequestHandle,
    _In_ WinHttpCallbackContext* winHttpContext
)
{
#if !HC_NOWEBSOCKETS
    auto winHttpConnection = winHttpContext->winHttpConnection;
    winHttpConnection->m_lock.lock();

    HC_TRACE_INFORMATION(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] Websocket WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE", TO_ULL(HCHttpCallGetId(winHttpConnection->m_call)), GetCurrentThreadId());

    assert(winHttpConnection->m_winHttpWebSocketExports.completeUpgrade);

    // Application should check what is the HTTP status code returned by the server and behave accordingly.
    // WinHttpWebSocketCompleteUpgrade will fail if the HTTP status code is different than 101.
    winHttpConnection->m_hRequest = winHttpConnection->m_winHttpWebSocketExports.completeUpgrade(hRequestHandle, (DWORD_PTR)(winHttpContext));
    if (winHttpConnection->m_hRequest == NULL)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "HCHttpCallPerform [ID %llu] [TID %ul] WinHttpWebSocketCompleteUpgrade errorcode %d", TO_ULL(HCHttpCallGetId(winHttpConnection->m_call)), GetCurrentThreadId(), dwError);
        winHttpConnection->m_lock.unlock();
        winHttpConnection->complete_task(E_FAIL, HRESULT_FROM_WIN32(dwError));
        return;
    }

    constexpr DWORD closeTimeoutMs = 1000;
    bool status = WinHttpSetOption(winHttpConnection->m_hRequest, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, (LPVOID)&closeTimeoutMs, sizeof(DWORD));
    if (!status)
    {
        DWORD dwError = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "WinHttpConnection [ID %llu] [TID %ul] WinHttpSetOption errorcode %d", TO_ULL(HCHttpCallGetId(winHttpConnection->m_call)), GetCurrentThreadId(), dwError);
    }

    winHttpConnection->m_state = ConnectionState::WebSocketConnected;

    WinHttpCloseHandle(hRequestHandle); // The old request handle is not needed anymore.  We're using pRequestContext->m_hRequest now
    winHttpConnection->m_lock.unlock();

    // This will now complete the WebSocket Connect operation
    winHttpConnection->complete_task(S_OK, S_OK);

    // Begin listening for messages
    winHttpConnection->WebSocketReadAsync();
#else
    UNREFERENCED_PARAMETER(hRequestHandle);
    UNREFERENCED_PARAMETER(winHttpContext);
    assert(false);
#endif
}

#if !HC_NOWEBSOCKETS
HRESULT CALLBACK WinHttpConnection::WebSocketConnectProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    auto winHttpConnection = static_cast<WinHttpConnection*>(data->context);

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        return winHttpConnection->SendRequest();
    }
    case XAsyncOp::GetResult:
    {
        auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
        result->websocket = winHttpConnection->m_websocketHandle;
        RETURN_IF_FAILED(HCHttpCallResponseGetNetworkErrorCode(winHttpConnection->m_call, &result->errorCode, &result->platformErrorCode));
        return S_OK;
    }
    default: return S_OK;
    }
}

HRESULT CALLBACK WinHttpConnection::WebSocketSendProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    auto context = static_cast<WebSocketSendContext*>(data->context);
    assert(context);

    switch (op)
    {
    case XAsyncOp::Begin:
    {
        std::lock_guard<std::recursive_mutex> lock{ context->connection->m_websocketSendMutex };
        context->connection->m_websocketSendQueue.push(context);

        // By design, limit to a single WinHttpWebSocketSend send at a time. If there isn't another send already in progress,
        // send the message now. If there is, the next message will be sent when that send completes
        if (context->connection->m_websocketSendQueue.size() == 1)
        {
            context->connection->WebSocketSendMessage(*context);
        }
        return S_OK;
    }
    case XAsyncOp::GetResult:
    {
        auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
        result->websocket = context->socket;
        result->platformErrorCode = S_OK;
        result->errorCode = S_OK;
        return S_OK;
    }
    case XAsyncOp::Cleanup:
    {
        HC_UNIQUE_PTR<WebSocketSendContext> reclaim{ context };
        return S_OK;
    }
    default: return S_OK;
    }
}
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
