// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

HC_DECLARE_TRACE_AREA(WEBSOCKET);

// Base class for platform specific implementations
struct hc_websocket_impl 
{
    hc_websocket_impl() {}
    virtual ~hc_websocket_impl() {}
};

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

    void AddClientRef();
    void DecClientRef();
    void AddRef();
    void DecRef();

    static void CALLBACK MessageFunc(HC_WEBSOCKET* websocket, const char* message, void* context);
    static void CALLBACK BinaryMessageFunc(HC_WEBSOCKET* websocket, const uint8_t* bytes, uint32_t payloadSize, void* context);
    static void CALLBACK CloseFunc(HC_WEBSOCKET* websocket, HCWebSocketCloseStatus status, void* context);

    uint64_t id;
    bool disconnectCallExpected{ false };
    http_header_map connectHeaders;
    http_internal_string proxyUri;
    http_internal_string uri;
    http_internal_string subProtocol;

    std::shared_ptr<hc_websocket_impl> impl;
private:
    HCWebSocketMessageFunction m_clientMessageFunc;
    HCWebSocketBinaryMessageFunction m_clientBinaryMessageFunc;
    HCWebSocketCloseEventFunction m_clientCloseEventFunc;
    void* m_clientContext;

    std::atomic<int> m_clientRefCount{ 0 };
    std::atomic<int> m_totalRefCount{ 0 };
    std::shared_ptr<HC_WEBSOCKET> m_extraRefHolder;

} HC_WEBSOCKET;

HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
);

HRESULT CALLBACK Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
);

HRESULT CALLBACK Internal_HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
);

HRESULT CALLBACK Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
);

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
