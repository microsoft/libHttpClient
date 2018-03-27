// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#if HC_UNITTEST_API
#include "websocket.h"

using namespace xbox::httpclient;




HRESULT Internal_HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ hc_websocket_handle websocket,
    _In_ AsyncBlock* asyncBlock
    )
{
    return S_OK;
}

HRESULT Internal_HCWebSocketSendMessage(
    _In_ hc_websocket_handle websocket,
    _In_z_ PCSTR message,
    _In_ AsyncBlock* asyncBlock
    )
{
    return S_OK;
}

HRESULT Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle websocket,
    _In_ HcWebsocketCloseStatus closeStatus
    )
{
    return S_OK;
}



#endif