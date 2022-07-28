// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

#include <winhttp.h>
#include "utils.h"
#include "uri.h"
#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XNetworking.h>
#endif
#if !HC_NOWEBSOCKETS
#include "WebSocket/hcwebsocket.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

enum msg_body_type
{
    no_body,
    content_length_chunked,
    transfer_encoding_chunked
};

class win32_cs
{
public:
    win32_cs() { InitializeCriticalSection(&m_cs); }
    ~win32_cs() { DeleteCriticalSection(&m_cs); }

    void lock() { EnterCriticalSection(&m_cs); }
    void unlock() { LeaveCriticalSection(&m_cs); }

private:
    CRITICAL_SECTION m_cs;
};

class win32_cs_autolock
{
public:
    win32_cs_autolock(win32_cs* pCS)
        : m_pCS(pCS)
    {
        m_pCS->lock();
        //HC_TRACE_INFORMATION(HTTPCLIENT, "win32_cs_autolock locked [ID %lu]", GetCurrentThreadId());
    }

    ~win32_cs_autolock()
    {
        //HC_TRACE_INFORMATION(HTTPCLIENT, "win32_cs_autolock unlocking [ID %lu]", GetCurrentThreadId());
        m_pCS->unlock();
    }

private:
    win32_cs* m_pCS;
};

class websocket_message_buffer
{
public:
    websocket_message_buffer() = default;
    ~websocket_message_buffer()
    {
        if (m_buffer != nullptr)
        {
            http_memory::mem_free(m_buffer);
        }
    }

    inline uint8_t* GetBuffer() { return m_buffer; }
    inline uint8_t* GetNextWriteLocation() { return m_buffer + m_bufferByteCount; }
    inline uint32_t GetBufferByteCount() { return m_bufferByteCount; }
    inline uint32_t GetRemainingCapacity() { return m_bufferByteCapacity - m_bufferByteCount; }

    void FinishWriteData(_In_ uint32_t dataByteCount)
    {
        m_bufferByteCount += dataByteCount;
        assert(m_bufferByteCount <= m_bufferByteCapacity);
    }

    void TransferBuffer(_Inout_ websocket_message_buffer* destBuffer)
    {
        assert(destBuffer != nullptr);
        destBuffer->m_buffer = m_buffer;
        destBuffer->m_bufferByteCount = m_bufferByteCount;
        destBuffer->m_bufferByteCapacity = m_bufferByteCapacity;

        m_buffer = nullptr;
        m_bufferByteCount = 0;
        m_bufferByteCapacity = 0;
    }

    HRESULT Resize(_In_ uint32_t dataByteCount)
    {
        HRESULT hr = S_OK;
        uint8_t* newBuffer;

        if (dataByteCount > m_bufferByteCapacity)
        {
            newBuffer = static_cast<uint8_t*>(http_memory::mem_alloc(dataByteCount));
            if (newBuffer != nullptr)
            {
                if (m_buffer != nullptr)
                {
                    // Copy the contents of the old buffer
                    CopyMemory(newBuffer, m_buffer, m_bufferByteCount);
                    http_memory::mem_free(m_buffer);
                }
                m_buffer = newBuffer;
                m_bufferByteCapacity = dataByteCount;
            }
            else
            {
                hr = E_OUTOFMEMORY;
            }
        }

        return hr;
    }

private:
    uint8_t* m_buffer = nullptr;
    uint32_t m_bufferByteCount = 0;
    uint32_t m_bufferByteCapacity = 0;
};

enum class ConnectionState : uint32_t
{
    Initialized,
    WinHttpRunning,
    WebSocketConnected,
    WebSocketClosing,
    WinHttpClosing,
    Closed
};

using ConnectionClosedCallback = std::function<void()>;

class WinHttpConnection : public std::enable_shared_from_this<WinHttpConnection>
#if !HC_NOWEBSOCKETS
    , public hc_websocket_impl
#endif
{
public:
    static Result<std::shared_ptr<WinHttpConnection>> Initialize(
        HINTERNET hSession,
        HCCallHandle call,
        proxy_type proxyType,
        XPlatSecurityInformation&& securityInformation
    );

#if !HC_NOWEBSOCKETS
    static Result<std::shared_ptr<WinHttpConnection>> Initialize(
        HINTERNET hSession,
        HCWebsocketHandle webSocket,
        const char* uri,
        const char* subprotocol,
        proxy_type proxyType,
        XPlatSecurityInformation&& securityInformation
    );
#endif

    WinHttpConnection(const WinHttpConnection&) = delete;
    WinHttpConnection(WinHttpConnection&&) = delete;
    WinHttpConnection& operator=(const WinHttpConnection&) = delete;
    WinHttpConnection& operator=(WinHttpConnection&&) = delete;
    virtual ~WinHttpConnection();

    // Client API entry points
    HRESULT HttpCallPerformAsync(XAsyncBlock* async);

#if !HC_NOWEBSOCKETS
    HRESULT WebSocketConnectAsync(XAsyncBlock* async);
    HRESULT WebSocketSendMessageAsync(XAsyncBlock* async, const char* message);
    HRESULT WebSocketSendMessageAsync(XAsyncBlock* async, const uint8_t* payloadBytes, size_t payloadSize, WINHTTP_WEB_SOCKET_BUFFER_TYPE payloadType = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);
    HRESULT WebSocketDisconnect(_In_ HCWebSocketCloseStatus closeStatus);
#endif

    // Called by WinHttpProvider to force close on PLM Suspend or shutdown
    HRESULT Close(ConnectionClosedCallback callback);

private:
    WinHttpConnection(
        HINTERNET hSession,
        HCCallHandle call,
        proxy_type proxyType,
        XPlatSecurityInformation&& securityInformation
    );

    HRESULT Initialize();

    static HRESULT query_header_length(_In_ HCCallHandle call, _In_ HINTERNET hRequestHandle, _In_ DWORD header, _Out_ DWORD* pLength);
    static uint32_t parse_status_code(
        _In_ HCCallHandle call,
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext);

    static void read_next_response_chunk(_In_ WinHttpConnection* pRequestContext, DWORD bytesRead);
    static void _multiple_segment_write_data(_In_ WinHttpConnection* pRequestContext);

    static void parse_headers_string(_In_ HCCallHandle call, _In_ wchar_t* headersStr);

    // WinHttp event callbacks
    static void CALLBACK completion_callback(
        HINTERNET hRequestHandle,
        DWORD_PTR context,
        DWORD statusCode,
        _In_ void* statusInfo,
        DWORD statusInfoLength);

    static void callback_status_request_error(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_sending_request(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_sendrequest_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_write_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_headers_available(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_data_available(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_read_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ DWORD statusInfoLength);

    static void callback_status_secure_failure(
        _In_ HINTERNET hRequestHandle,
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static HRESULT flush_response_buffer(
        _In_ WinHttpConnection* pRequestContext
    );

    static void callback_websocket_status_write_complete(
        _In_ WinHttpConnection* pRequestContext);

    static void callback_websocket_status_headers_available(
        _In_ HINTERNET hRequestHandle,
        _In_ struct WinHttpCallbackContext* winHttpContext);

    static void callback_websocket_status_read_complete(
        _In_ WinHttpConnection* pRequestContext,
        _In_ void* statusInfo);

    static const char* winhttp_web_socket_buffer_type_to_string(
        _In_ WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType
    );

    void complete_task(_In_ HRESULT translatedHR);
    void complete_task(_In_ HRESULT translatedHR, uint32_t platformSpecificError);

    HRESULT SendRequest();
    HRESULT StartWinHttpClose();

#if HC_PLATFORM != HC_PLATFORM_GDK
    HRESULT set_autodiscover_proxy();
#endif

    // HttpCall state
    ConnectionState m_state{ ConnectionState::Initialized };
    HINTERNET m_hSession; // non-owning
    HINTERNET m_hConnection = nullptr;
    HINTERNET m_hRequest = nullptr;

    HCCallHandle m_call; // non-owning
    Uri m_uri;
    XAsyncBlock* m_asyncBlock = nullptr; // non-owning
    XPlatSecurityInformation const m_securityInformation{};
    ConnectionClosedCallback m_connectionClosedCallback;

    msg_body_type m_requestBodyType = msg_body_type::no_body;
    size_t m_requestBodySize = 0;
    size_t m_requestBodyRemainingToWrite = 0;
    size_t m_requestBodyOffset = 0;
    http_internal_vector<uint8_t> m_requestBuffer;
    http_internal_vector<uint8_t> m_responseBuffer;
    proxy_type m_proxyType = proxy_type::default_proxy;
    win32_cs m_lock;

    struct WebSocketSendContext
    {
        XAsyncBlock* async; // non-owning
        WinHttpConnection* connection; // non-owning
        HCWebsocketHandle socket; // non-owning
        http_internal_vector<uint8_t> payload;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE payloadType;
    };

    WinHttpWebSocketExports m_winHttpWebSocketExports;

    // WinHttp WebSocket methods
    void WebSocketSendMessage(const WebSocketSendContext& sendContext);
    void WebSocketCompleteEntireSendQueueWithError(HRESULT error);
    HRESULT WebSocketReadAsync();
    HRESULT WebSocketReadComplete(bool binaryMessage, bool endOfMessage);
    void on_websocket_disconnected(_In_ USHORT closeReason);

    static HRESULT CALLBACK WebSocketConnectProvider(XAsyncOp op, const XAsyncProviderData* data);
    static HRESULT CALLBACK WebSocketSendProvider(XAsyncOp op, const XAsyncProviderData* data);

    // WebSocket state
    HCWebsocketHandle m_websocketHandle{ nullptr };
    HCCallHandle m_websocketCall{ nullptr };
    std::recursive_mutex m_websocketSendMutex; // controls access to m_websocketSendQueue
    http_internal_queue<WebSocketSendContext*> m_websocketSendQueue{};
    websocket_message_buffer m_websocketReceiveBuffer;
    bool m_websocketForwardingFragments{ false };
};

NAMESPACE_XBOX_HTTP_CLIENT_END
