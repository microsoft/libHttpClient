// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "../hcwebsocket.h"

using namespace xbox::httpclient;


HC_RESULT Internal_HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ AsyncBlock* async)
{
    // TODO
    return HC_OK;
}

HC_RESULT Internal_HCWebSocketSendMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR message,
    _In_ AsyncBlock* async)
{
    // TODO
    return HC_OK;
}

HC_RESULT Internal_HCWebSocketDisconnect(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CLOSE_STATUS closeStatus
    )
{
    // TODO
    return HC_OK;
}

