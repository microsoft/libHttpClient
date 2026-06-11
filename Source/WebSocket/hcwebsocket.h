// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <atomic>

#include <httpClient/httpClient.h>
#include "HTTP/httpcall.h"
#include "Platform/IWebSocketProvider.h"

HC_DECLARE_TRACE_AREA(WEBSOCKET);

struct HC_WEBSOCKET_OBSERVER;

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

struct ObserverDeleter
{
    void operator()(HC_WEBSOCKET_OBSERVER* ptr) noexcept;
};

using ObserverPtr = std::unique_ptr<HC_WEBSOCKET_OBSERVER, ObserverDeleter>;

}
}

#ifndef HC_NOWEBSOCKETS

// An observer of a WebSocket. Holds a shared reference to the WebSocket and receives callbacks on WebSocket events
struct HC_WEBSOCKET_OBSERVER
{
public:
    int AddRef() noexcept;
    int Release() noexcept;

    static xbox::httpclient::ObserverPtr Initialize(
        _In_ std::shared_ptr<xbox::httpclient::WebSocket> WebSocket,
        _In_opt_ HCWebSocketMessageFunction messageFunc = nullptr,
        _In_opt_ HCWebSocketBinaryMessageFunction binaryMessageFunc = nullptr,
        _In_opt_ HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc = nullptr,
        _In_opt_ HCWebSocketCloseEventFunction closeFunc = nullptr,
        _In_opt_ void* callbackContext = nullptr
    );

    HRESULT SetBinaryMessageFragmentEventFunction(HCWebSocketBinaryMessageFragmentFunction binaryFragmentFunc) noexcept;
    bool HasBinaryMessageFragmentEventFunction() const noexcept;

    std::shared_ptr<xbox::httpclient::WebSocket> const websocket;

private:
    HC_WEBSOCKET_OBSERVER(std::shared_ptr<xbox::httpclient::WebSocket> WebSocket);
    virtual ~HC_WEBSOCKET_OBSERVER();

    static void CALLBACK MessageFunc(HCWebsocketHandle handle, const char* message, void* context);
    static void CALLBACK BinaryMessageFunc(HCWebsocketHandle handle, const uint8_t* bytes, uint32_t payloadSize, void* context);
    static void CALLBACK BinaryMessageFragmentFunc(HCWebsocketHandle handle, const uint8_t* payloadBytes, uint32_t payloadSize, bool isLastFragment, void* functionContext);
    static void CALLBACK CloseFunc(HCWebsocketHandle handle, HCWebSocketCloseStatus status, void* context);

    std::atomic<int> m_refCount{ 1 };
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

inline
void ObserverDeleter::operator()(HC_WEBSOCKET_OBSERVER* ptr) noexcept
{
    ptr->Release();
}

constexpr size_t WEBSOCKET_RECVBUFFER_MAXSIZE_DETERMINISTIC_DEFAULT = 32000000;

class WebSocket : public std::enable_shared_from_this<WebSocket>
{
public:
    WebSocket(uint64_t id, IWebSocketProvider& provider);
    WebSocket(const WebSocket&) = delete;
    WebSocket(WebSocket&&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;
    virtual ~WebSocket();

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
    HRESULT Disconnect(HCWebSocketCloseStatus closeStatus);

    // Unique ID for logging
    uint64_t const id;

    const http_internal_string& Uri() const noexcept;
    const http_internal_string& SubProtocol() const noexcept;
    const xbox::httpclient::HttpHeaders& Headers() const noexcept;
    const http_internal_string& ProxyUri() const noexcept;
    const bool ProxyDecryptsHttps() const noexcept;
    size_t MaxReceiveBufferSize() const noexcept;
    size_t DeterministicMaxReceiveBufferSize() const noexcept;
    bool MaxReceiveBufferSizeExplicitlySet() const noexcept;
    uint32_t PingInterval() const noexcept;
    HCWebSocketOptions Options() const noexcept;
    bool OptionsExplicitlySet() const noexcept;
    bool UsesLegacySemantics() const noexcept;
    bool UsesDeterministicSemantics() const noexcept;
    HRESULT GetResponseHeader(_In_z_ const char* headerName, _Out_ const char** headerValue) const noexcept;
    uint32_t GetNumResponseHeaders() const noexcept;
    HRESULT GetResponseHeaderAtIndex(_In_ uint32_t headerIndex, _Out_ const char** headerName, _Out_ const char** headerValue) const noexcept;

    HRESULT SetHeader(http_internal_string&& headerName, http_internal_string&& headerValue) noexcept;
    HRESULT SetProxyUri(http_internal_string&& proxyUri) noexcept;
    HRESULT SetProxyDecryptsHttps(bool allowProxyToDecryptHttps) noexcept;  
    HRESULT SetMaxReceiveBufferSize(size_t maxReceiveBufferSizeBytes) noexcept;
    HRESULT SetPingInterval(uint32_t pingInterval) noexcept;
    HRESULT SetOptions(HCWebSocketOptions options) noexcept;
    void ClearResponseHeaders() noexcept;
    HRESULT SetResponseHeaders(xbox::httpclient::HttpHeaders&& headers) noexcept;

    // Event functions
    static void CALLBACK MessageFunc(HCWebsocketHandle handle, const char* message, void* context);
    static void CALLBACK BinaryMessageFunc(HCWebsocketHandle handle, const uint8_t* bytes, uint32_t payloadSize, void* context);
    static void CALLBACK BinaryMessageFragmentFunc(HCWebsocketHandle handle, const uint8_t* payloadBytes, uint32_t payloadSize, bool isLastFragment, void* functionContext);
    static void CALLBACK CloseFunc(HCWebsocketHandle handle, HCWebSocketCloseStatus status, void* context);

    // Internal providers use this to store context
    std::shared_ptr<hc_websocket_impl> impl;

private:
    static HRESULT CALLBACK ConnectAsyncProvider(XAsyncOp op, XAsyncProviderData const* data);
    static void CALLBACK ConnectComplete(XAsyncBlock* async);
    void ClearResponseHeadersLockHeld() noexcept;
    bool HasBinaryMessageFragmentHandlers() const noexcept;

    static void NotifyWebSocketRoutedHandlers(
        _In_ HCWebsocketHandle websocket,
        _In_ bool receiving,
        _In_opt_z_ const char* message,
        _In_opt_ const uint8_t* payloadBytes,
        _In_ size_t payloadSize
    );

    xbox::httpclient::HttpHeaders m_connectHeaders;
    xbox::httpclient::HttpHeaders m_connectResponseHeaders;
    bool m_allowProxyToDecryptHttps{ false };
    http_internal_string m_proxyUri;
    http_internal_string m_uri;
    http_internal_string m_subProtocol;
    size_t m_maxReceiveBufferSize{ 0 };
    uint32_t m_pingInterval{ 0 };

    struct ConnectContext;
    struct ProviderContext;

    struct EventCallbacks
    {
        HCWebSocketMessageFunction messageFunc{ nullptr };
        HCWebSocketBinaryMessageFunction binaryMessageFunc{ nullptr };
        HCWebSocketBinaryMessageFragmentFunction binaryMessageFragmentFunc{ nullptr };
        HCWebSocketCloseEventFunction closeFunc{ nullptr };
        void* context{ nullptr };
    };

    mutable DefaultUnnamedMutex m_stateMutex;
    enum class State
    {
        Initial,
        Connecting,
        Connected,
        Disconnecting,
        Disconnected
    } m_state{ State::Initial };

    mutable std::recursive_mutex m_eventCallbacksMutex;
    http_internal_map<uint32_t, EventCallbacks> m_eventCallbacks{};
    uint32_t m_nextToken{ 1 };

    ProviderContext* m_providerContext{ nullptr };

    // Stable routing provider for this WebSocket. Hybrid providers may delegate internally,
    // but the WebSocket does not keep per-connection active-provider state.
    IWebSocketProvider& m_provider;
    HCWebSocketOptions m_options{ HCWebSocketOptions::None };
    bool m_optionsExplicitlySet{ false };
    bool m_maxReceiveBufferSizeExplicitlySet{ false };
};

} // namespace httpclient
} // namespace xbox

#endif // !HC_NOWEBSOCKETS
