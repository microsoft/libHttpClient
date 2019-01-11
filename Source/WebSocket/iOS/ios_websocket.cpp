// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
 #include "pch.h"
 #include "../hcwebsocket.h"
 using namespace xbox::httpclient;
 HRESULT Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ AsyncBlock* async
    )
{
    // TODO
    return S_OK;
}
 HRESULT Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ AsyncBlock* async
    )
{
    // TODO
    return S_OK;
}
 HRESULT Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus
)
{
    // TODO
    return S_OK;
}