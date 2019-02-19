// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

// Base class for platform specific implementations
struct hc_websocket_impl 
{
    hc_websocket_impl() {}
    virtual ~hc_websocket_impl() {}
};

typedef struct HC_WEBSOCKET
{
    HC_WEBSOCKET()
    {
        refCount = 1;
    }
    
    ~HC_WEBSOCKET()
    {
#if !HC_NOWEBSOCKETS
        HC_TRACE_VERBOSE(WEBSOCKET, "HCWebsocketHandle dtor");
#endif
    }

    uint64_t id = 0;
    std::atomic<int> refCount;
    bool connectCalled = false;
    http_header_map connectHeaders;
    http_internal_string proxyUri;
    http_internal_string uri;
    http_internal_string subProtocol;

    HCWebSocketMessageFunction callbackMessageFunc;
    HCWebSocketBinaryMessageFunction callbackBinaryMessageFunc;
    HCWebSocketCloseEventFunction callbackCloseEventFunc;
    void* callbackContext;

    std::shared_ptr<hc_websocket_impl> impl;
} HC_WEBSOCKET;

HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_ HCPerformEnv env
    );

HRESULT CALLBACK Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock
    );

HRESULT CALLBACK Internal_HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock
);

HRESULT CALLBACK Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    );
