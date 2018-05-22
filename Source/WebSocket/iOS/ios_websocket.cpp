// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../hcwebsocket.h"

using namespace xbox::httpclient;

HRESULT Internal_HCWebSocketConnectAsync(
    _In_ AsyncBlock* async,
    _In_z_ UTF8CSTR uri,
    _In_z_ UTF8CSTR subProtocol,
    _In_ hc_websocket_handle_t websocket
    )
{
    // TODO
    return S_OK;
}

HRESULT Internal_HCWebSocketSendMessageAsync(
    _In_ AsyncBlock* async,
    _In_ hc_websocket_handle_t websocket,
    _In_z_ UTF8CSTR message
    )
{
    // TODO
    return S_OK;
}

HRESULT Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle_t websocket,
    _In_ HCWebSocketCloseStatus closeStatus
)
{
    // TODO
    return S_OK;
}
