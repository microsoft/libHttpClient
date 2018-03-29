// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once
#include "pch.h"

typedef struct HC_WEBSOCKET
{
    HC_WEBSOCKET() :
        id(0),
        refCount(1),
        connectCalled(false)
    {
    }

    uint64_t id;
    std::atomic<int> refCount;
    bool connectCalled;
    http_internal_map<http_internal_string, http_internal_string> connectHeaders;
    http_internal_string proxyUri;
    http_internal_string uri;
    http_internal_string subProtocol;
    std::shared_ptr<xbox::httpclient::hc_task> task;
} HC_WEBSOCKET;

HRESULT Internal_HCWebSocketConnect(
    _In_z_ const_utf8_string uri,
    _In_z_ const_utf8_string subProtocol,
    _In_ hc_websocket_handle websocket,
    _In_ AsyncBlock* asyncBlock
    );

HRESULT Internal_HCWebSocketSendMessage(
    _In_ hc_websocket_handle websocket,
    _In_z_ const_utf8_string message,
    _In_ AsyncBlock* asyncBlock
    );

HRESULT Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle websocket,
    _In_ HCWebsocketCloseStatus closeStatus
    );
