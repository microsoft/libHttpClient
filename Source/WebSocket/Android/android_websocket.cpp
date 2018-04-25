// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../HCWebSocket.h"

using namespace xbox::httpclient;

HRESULT Internal_HCWebSocketConnect(
    _In_z_ UTF8CSTR uri,
    _In_z_ UTF8CSTR subProtocol,
    _In_ hc_websocket_handle_t websocket,
    _In_ AsyncBlock* async)
{
    // TODO
    return S_OK;
}

HRESULT Internal_HCWebSocketSendMessage(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ UTF8CSTR message,
    _In_ AsyncBlock* async)
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
