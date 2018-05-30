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

HRESULT Internal_HCWebSocketConnectAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ hc_websocket_handle_t websocket
    );

HRESULT Internal_HCWebSocketSendMessageAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ hc_websocket_handle_t websocket,
    _In_z_ const char* message
    );

HRESULT Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle_t websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    );
