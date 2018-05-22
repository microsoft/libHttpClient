// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#if HC_UNITTEST_API
#include "websocket.h"

using namespace xbox::httpclient;




HRESULT Internal_HCWebSocketConnectAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ hc_websocket_handle_t websocket
    )
{
    return S_OK;
}

HRESULT Internal_HCWebSocketSendMessageAsync(
    _In_ AsyncBlock* asyncBlock,
    _In_ hc_websocket_handle_t websocket,
    _In_z_ PCSTR message
    )
{
    return S_OK;
}

HRESULT Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle_t websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    )
{
    return S_OK;
}



#endif