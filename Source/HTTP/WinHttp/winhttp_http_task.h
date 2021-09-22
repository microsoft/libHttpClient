// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "pch.h"
#include <winhttp.h>
#include "utils.h"
#include "uri.h"

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <XNetworking.h>
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

struct WinHttpState
{
public:
    WinHttpState() = default;
    WinHttpState(const WinHttpState&) = delete;
    WinHttpState(WinHttpState&&) = delete;
    WinHttpState& operator=(const WinHttpState&) = delete;
    WinHttpState& operator=(WinHttpState&&) = delete;
    virtual ~WinHttpState();

    static uint32_t GetDefaultHttpSecurityProtocolFlagsForWin7();
    HINTERNET GetSessionForHttpSecurityProtocolFlags(_In_ uint32_t enabledHttpSecurityProtocolFlags);
    HINTERNET CreateHSessionForForHttpSecurityProtocolFlags(_In_ uint32_t enabledHttpSecurityProtocolFlags);
    XTaskQueueHandle GetImmediateQueue();

    HRESULT SetGlobalProxy(_In_ const char* proxyUri);

    http_internal_map<uint32_t, HINTERNET> m_hSessions;
    xbox::httpclient::proxy_type m_proxyType = xbox::httpclient::proxy_type::automatic_proxy;
    http_internal_string m_globalProxy;
    XTaskQueueHandle m_immediateQueue{ nullptr };
    std::mutex m_lock;

private:

};

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


#if HC_WINHTTP_WEBSOCKETS
class websocket_message_buffer
{
public:
    inline uint8_t* GetBuffer() { return m_buffer; }
    inline uint8_t* GetNextWriteLocation() { return m_buffer + m_bufferByteCount; }
    inline uint32_t GetBufferByteCount() { return m_bufferByteCount; }
    inline uint32_t GetRemainingCapacity() { return m_bufferByteCapacity - m_bufferByteCount; }

    HRESULT InitializeBuffer(_In_ uint32_t dataByteCount)
    {
        assert(m_buffer == nullptr);

        HRESULT hr = S_OK;
        if (dataByteCount > 0)
        {
            m_buffer = static_cast<uint8_t*>(http_memory::mem_alloc(dataByteCount));
            if (m_buffer != nullptr)
            {
                m_bufferByteCapacity = dataByteCount;
            }
            else
            {
                hr = E_OUTOFMEMORY;
            }
        }
        else
        {
            m_buffer = nullptr;
            m_bufferByteCount = 0;
            m_bufferByteCapacity = 0;
        }

        return hr;
    }

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
                // Copy the contents of the old buffer
                CopyMemory(newBuffer, m_buffer, m_bufferByteCount);
                http_memory::mem_free(m_buffer);
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

    ~websocket_message_buffer()
    {
        if (m_buffer != nullptr)
        {
            http_memory::mem_free(m_buffer);
        }
    }

private:
    uint8_t* m_buffer = nullptr;
    uint32_t m_bufferByteCount = 0;
    uint32_t m_bufferByteCapacity = 0;
};

enum class WinHttpWebsockState
{
    Created,
    Connecting,
    Connected,
    Closing,
    Closed,
    Destroyed
};
#endif

class winhttp_http_task : public xbox::httpclient::hc_task
{
public:
    winhttp_http_task(
        _Inout_ XAsyncBlock* asyncBlock,
        _In_ HCCallHandle call,
        _In_ std::shared_ptr<WinHttpState> winHttpState,
        _In_ proxy_type proxyType,
        _In_ bool isWebSocket);
    ~winhttp_http_task();

    HRESULT connect_and_send_async();

#if HC_WINHTTP_WEBSOCKETS
    void send_websocket_message(WINHTTP_WEB_SOCKET_BUFFER_TYPE eBufferType, _In_ const void* payloadPtr, _In_ size_t payloadLength);
    HRESULT disconnect_websocket(_In_ HCWebSocketCloseStatus closeStatus);
    HRESULT on_websocket_disconnected(_In_ USHORT closeReason);
    std::function<void(HRESULT)> m_websocketSendCompleteCallback;
    std::atomic<WinHttpWebsockState> m_socketState = WinHttpWebsockState::Created;
    HCWebsocketHandle m_websocketHandle = nullptr;
    HRESULT m_connectHr{ S_OK };
    uint32_t m_connectPlatformError{ 0 };
#endif

private:
    static HRESULT query_header_length(_In_ HCCallHandle call, _In_ HINTERNET hRequestHandle, _In_ DWORD header, _Out_ DWORD* pLength);
    static uint32_t parse_status_code(
        _In_ HCCallHandle call,
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext);

    static void read_next_response_chunk(_In_ winhttp_http_task* pRequestContext, DWORD bytesRead);
    static void _multiple_segment_write_data(_In_ winhttp_http_task* pRequestContext);

    static void parse_headers_string(_In_ HCCallHandle call, _In_ wchar_t* headersStr);

    static void callback_status_request_error(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_sending_request(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_sendrequest_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_write_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_headers_available(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_data_available(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_status_read_complete(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ DWORD statusInfoLength);

    static void callback_status_secure_failure(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static void callback_websocket_status_headers_available(
        _In_ HINTERNET hRequestHandle,
        _In_ winhttp_http_task* pRequestContext);

    static void callback_websocket_status_read_complete(
        _In_ winhttp_http_task* pRequestContext,
        _In_ void* statusInfo);

    static char* winhttp_web_socket_buffer_type_to_string(
        _In_ WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType
    );

    static HRESULT flush_response_buffer(
        _In_ winhttp_http_task* pRequestContext
    );

    HRESULT query_security_information(_In_ http_internal_wstring wUrlHost);

    HRESULT send(_In_ const xbox::httpclient::Uri& cUri, _In_ const char* method);

    HRESULT connect(_In_ const xbox::httpclient::Uri& cUri);

    void complete_task(_In_ HRESULT translatedHR);

    void complete_task(_In_ HRESULT translatedHR, uint32_t platformSpecificError);

    static void get_proxy_name(
        _In_ proxy_type proxyType,
        _Out_ DWORD* pAccessType,
        _Out_ const wchar_t** pwProxyName
        );

#if HC_PLATFORM != HC_PLATFORM_GDK
    void set_autodiscover_proxy(_In_ const xbox::httpclient::Uri& cUri);
#endif

    static void get_proxy_info(
        _In_ WINHTTP_PROXY_INFO* pInfo,
        _In_ bool* pProxyInfoRequired,
        _In_ const xbox::httpclient::Uri& cUri);

    static void CALLBACK completion_callback(
        HINTERNET hRequestHandle,
        DWORD_PTR context,
        DWORD statusCode,
        _In_ void* statusInfo,
        DWORD statusInfoLength);

    HCCallHandle m_call = nullptr;
    XAsyncBlock* m_asyncBlock = nullptr;

    std::shared_ptr<WinHttpState> m_winHttpState = nullptr;
    HINTERNET m_hConnection = nullptr;
    HINTERNET m_hRequest = nullptr;
    msg_body_type m_requestBodyType = msg_body_type::no_body;
    size_t m_requestBodyRemainingToWrite = 0;
    size_t m_requestBodyOffset = 0;
    http_internal_vector<uint8_t> m_requestBuffer;
    http_internal_vector<uint8_t> m_responseBuffer;
    proxy_type m_proxyType = proxy_type::default_proxy;
    win32_cs m_lock;
    bool m_isWebSocket = false;
    bool m_isSecure = false;

#if HC_WINHTTP_WEBSOCKETS
    // websocket state
    HRESULT websocket_start_listening();
    HRESULT websocket_read_message();
    websocket_message_buffer m_websocketResponseBuffer;
#endif

#if HC_PLATFORM == HC_PLATFORM_GDK
    http_internal_vector<uint8_t> m_securityInformationBuffer;
    XNetworkingSecurityInformation* m_securityInformation{ nullptr }; // lifespan owned by m_securityInformationBuffer
#endif
};

NAMESPACE_XBOX_HTTP_CLIENT_END
