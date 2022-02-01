// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <httpClient/httpClient.h>
#include "HTTP/httpcall.h"
#include "Global/perform_env.h"

HC_DECLARE_TRACE_AREA(WEBSOCKET);

namespace xbox
{
namespace httpclient
{
class WebSocket;

// Base class for platform specific implementations
struct hc_websocket_impl
{
    hc_websocket_impl() {}
    virtual ~hc_websocket_impl() {}
};

}
}

// An observer of a WebSocket. Holds a shared reference to the WebSocket and receives callbacks on WebSocket events
struct HC_WEBSOCKET_OBSERVER
{
public: 
    virtual ~HC_WEBSOCKET_OBSERVER();

    static HC_UNIQUE_PTR<HC_WEBSOCKET_OBSERVER> Initialize(
        _In_ std::shared_ptr<xbox::httpclient::WebSocket> WebSocket,
        _In_opt_ HCWebSocketMessageFunction messageFunc = nullptr,
        _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc = nullptr,
        _In_opt_ HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc = nullptr,
        _In_opt_ HCWebSocketCloseEventFunction closeFunc = nullptr,
        _In_opt_ void* callbackContext = nullptr
    );

    void SetBinaryMessageFragmentEventFunction(HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc);

    std::atomic<int> refCount{ 1 };
    std::shared_ptr<xbox::httpclient::WebSocket> const websocket;

private:
    HC_WEBSOCKET_OBSERVER(std::shared_ptr<xbox::httpclient::WebSocket> WebSocket);

    HCWebSocketMessageFunction m_messageFunc{ nullptr };
    HCWebSocketBinaryMessageFunction m_binaryMessageFunc{ nullptr };
    HCWebSocketBinaryMessageFragmentFunction m_binaryFragmentFunc{ nullptr };
    HCWebSocketCloseEventFunction m_closeFunc{ nullptr };
    void* m_callbackContext{ nullptr };
    uint32_t m_handlerToken{ 0 };
};

namespace xbox
{
namespace httpclient
{

class WebSocket : public std::enable_shared_from_this<WebSocket>
{
public:
    WebSocket(const WebSocket&) = delete;
    WebSocket(WebSocket&&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;
    virtual ~WebSocket();

    static Result<std::shared_ptr<WebSocket>> Initialize();

    uint32_t RegisterEventCallbacks(
        _In_opt_ HCWebSocketMessageFunction messageFunc,
        _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc,
        _In_opt_ HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc,
        _In_opt_ HCWebSocketCloseEventFunction closeFunc,
        _In_opt_ void* callbackContext
    );

    void UnregisterEventCallbacks(uint32_t registrationToken);

    HRESULT ConnectAsync(
        _In_ http_internal_string&& uri,
        _In_ http_internal_string&& subProtocol,
        _Inout_ XAsyncBlock* asyncBlock
    ) noexcept;

    HRESULT SendAsync(
        _In_z_ const char* message,
        _Inout_ XAsyncBlock* asyncBlock
    ) noexcept;

    HRESULT SendBinaryAsync(
        _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
        _In_ uint32_t payloadSize,
        _Inout_ XAsyncBlock* asyncBlock
    ) noexcept;

    HRESULT Disconnect();

    // Unique ID for logging
    uint64_t const id;

    const xbox::httpclient::HttpHeaders& Headers() const noexcept;
    const http_internal_string& ProxyUri() const noexcept;
    const bool ProxyDecryptsHttps() const noexcept;
    size_t MaxReceiveBufferSize() const noexcept;

    HRESULT SetHeader(http_internal_string&& headerName, http_internal_string&& headerValue) noexcept;
    HRESULT SetProxyUri(http_internal_string&& proxyUri) noexcept;
    HRESULT SetProxyDecryptsHttps(bool allowProxyToDecryptHttps) noexcept;  
    HRESULT SetMaxReceiveBufferSize(size_t maxReceiveBufferSizeBytes) noexcept;

    // Event functions
    static void CALLBACK MessageFunc(HCWebsocketHandle handle, const char* message, void* context);
    static void CALLBACK BinaryMessageFunc(HCWebsocketHandle handle, const uint8_t* bytes, uint32_t payloadSize, void* context);
    static void CALLBACK BinaryMessageFragmentFunc(HCWebsocketHandle handle, const uint8_t* payloadBytes, uint32_t payloadSize, bool isLastFragment, void* functionContext);
    static void CALLBACK CloseFunc(HCWebsocketHandle handle, HCWebSocketCloseStatus status, void* context);

    // Internal providers use this to store context
    std::shared_ptr<hc_websocket_impl> impl;

private:
    WebSocket(uint64_t id, WebSocketPerformInfo performInfo, HC_PERFORM_ENV* performEnv);

    static HRESULT CALLBACK ConnectAsyncProvider(XAsyncOp op, XAsyncProviderData const* data);
    static void CALLBACK ConnectComplete(XAsyncBlock* async);

    static void NotifyWebSocketRoutedHandlers(
        _In_ HCWebsocketHandle websocket,
        _In_ bool receiving,
        _In_opt_z_ const char* message,
        _In_opt_ const uint8_t* payloadBytes,
        _In_ size_t payloadSize
    );

    xbox::httpclient::HttpHeaders m_connectHeaders;
    bool m_allowProxyToDecryptHttps{ false };
    http_internal_string m_proxyUri;
    http_internal_string m_uri;
    http_internal_string m_subProtocol;
    size_t m_maxReceiveBufferSize{ 0 };

    struct EventCallbacks;
    struct ProviderContext;

    std::mutex m_mutex;
    http_internal_map<uint32_t, EventCallbacks> m_eventCallbacks{};
    uint32_t m_nextToken{ 1 };

    WebSocketPerformInfo const m_performInfo{};
    HC_PERFORM_ENV* const m_performEnv{ nullptr }; // non-owning

    ProviderContext* m_providerContext{ nullptr };
};

} // namespace httpclient
} // namespace xbox
