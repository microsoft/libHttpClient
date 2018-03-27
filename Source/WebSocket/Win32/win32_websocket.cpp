// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../hcwebsocket.h"

using namespace xbox::httpclient;


hresult_t Internal_HCWebSocketConnect(
    _In_z_ const_utf8_string uri,
    _In_z_ const_utf8_string subProtocol,
    _In_ hc_websocket_handle websocket,
    _In_ AsyncBlock* async)
{
    // TODO
    return S_OK;
}

hresult_t Internal_HCWebSocketSendMessage(
    _In_ hc_websocket_handle websocket,
    _In_z_ const_utf8_string message,
    _In_ AsyncBlock* async)
{
    // TODO
    return S_OK;
}

hresult_t Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle websocket,
    _In_ HcWebsocketCloseStatus closeStatus
    )
{
    // TODO
    return S_OK;
}

