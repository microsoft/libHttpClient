// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <httpClient/httpClient.h>

HC_DECLARE_TRACE_AREA(WEBSOCKET);

// Base class for platform specific implementations
struct hc_websocket_impl 
{
    hc_websocket_impl() {}
    virtual ~hc_websocket_impl() {}
};

#if !HC_NOWEBSOCKETS

typedef struct HC_WEBSOCKET : std::enable_shared_from_this<HC_WEBSOCKET>
{
public:
    HC_WEBSOCKET(
        _In_ uint64_t id,
        _In_opt_ HCWebSocketMessageFunction messageFunc,
        _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
        _In_opt_ HCWebSocketCloseEventFunction closeFunc,
        _In_opt_ void* functionContext
    );
    virtual ~HC_WEBSOCKET();

    HRESULT Connect(
        _In_z_ const char* uri,
        _In_z_ const char* subProtocol,
        _Inout_ XAsyncBlock* asyncBlock
    ) noexcept;

    HRESULT Send(
        _In_z_ const char* message,
        _Inout_ XAsyncBlock* asyncBlock
    ) noexcept;

    HRESULT SendBinary(
        _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
        _In_ uint32_t payloadSize,
        _Inout_ XAsyncBlock* asyncBlock
    ) noexcept;

    HRESULT Disconnect();

    const uint64_t id;
    const http_header_map& Headers() const noexcept;
    const http_internal_string& ProxyUri() const noexcept;
    const bool ProxyDecryptsHttps() const noexcept;
    const http_internal_string& Uri() const noexcept;
    const http_internal_string& SubProtocol() const noexcept;

    HRESULT SetProxyUri(http_internal_string&& proxyUri) noexcept;
    HRESULT SetProxyDecryptsHttps(bool allowProxyToDecryptHttps) noexcept;
    HRESULT SetHeader(http_internal_string&& headerName, http_internal_string&& headerValue) noexcept;

    void AddClientRef();
    void DecClientRef();
    void AddRef();
    void DecRef();

    static void CALLBACK MessageFunc(HC_WEBSOCKET* websocket, const char* message, void* context);
    static void CALLBACK BinaryMessageFunc(HC_WEBSOCKET* websocket, const uint8_t* bytes, uint32_t payloadSize, void* context);
    static void CALLBACK CloseFunc(HC_WEBSOCKET* websocket, HCWebSocketCloseStatus status, void* context);

    std::shared_ptr<hc_websocket_impl> impl;

private:
    XAsyncBlock m_connectAsyncBlock{};
    WebSocketCompletionResult m_connectResult{};
    XAsyncBlock* m_clientConnectAsyncBlock{ nullptr };

    enum class State
    {
        Initial,
        Disconnecting,
        Disconnected,
        Connecting,
        Connected
    } m_state{ State::Initial };

    http_header_map m_connectHeaders;
    bool m_allowProxyToDecryptHttps{ false };
    http_internal_string m_proxyUri;
    http_internal_string m_uri;
    http_internal_string m_subProtocol;

    HCWebSocketMessageFunction const m_clientMessageFunc;
    HCWebSocketBinaryMessageFunction const m_clientBinaryMessageFunc;
    HCWebSocketCloseEventFunction const m_clientCloseEventFunc;
    void* m_clientContext;

    std::recursive_mutex m_mutex;
    std::atomic<int> m_clientRefCount{ 0 };
    std::atomic<int> m_totalRefCount{ 0 };
    std::shared_ptr<HC_WEBSOCKET> m_extraRefHolder;

} HC_WEBSOCKET;

struct WebSocketPerformInfo
{
    WebSocketPerformInfo(
        _In_ HCWebSocketConnectFunction conn,
        _In_ HCWebSocketSendMessageFunction st,
        _In_ HCWebSocketSendBinaryMessageFunction sb,
        _In_ HCWebSocketDisconnectFunction dc,
        _In_opt_ void* ctx
    ):
        connect{ conn },
        sendText{ st },
        sendBinary{ sb },
        disconnect{ dc },
        context{ ctx }
    {}

    HCWebSocketConnectFunction connect = nullptr;
    HCWebSocketSendMessageFunction sendText = nullptr;
    HCWebSocketSendBinaryMessageFunction sendBinary = nullptr;
    HCWebSocketDisconnectFunction disconnect = nullptr;
    void* context = nullptr;
};

#endif // !HC_NOWEBSOCKETS
