// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#if HC_UNITTEST_API
#include "websocket.h"

using namespace xbox::httpclient;




HRESULT CALLBACK Internal_HCWebSocketConnectAsync(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
    )
{
    UNREFERENCED_PARAMETER(uri);
    UNREFERENCED_PARAMETER(subProtocol);
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(asyncBlock);
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(env);
    return S_OK;
}

HRESULT CALLBACK Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ PCSTR message,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(asyncBlock);
    UNREFERENCED_PARAMETER(context);
    return S_OK;
}

HRESULT CALLBACK Internal_HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(payloadBytes);
    UNREFERENCED_PARAMETER(payloadSize);
    UNREFERENCED_PARAMETER(asyncBlock);
    UNREFERENCED_PARAMETER(context);
    return S_OK;
}

HRESULT CALLBACK Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(closeStatus);
    UNREFERENCED_PARAMETER(context);
    return S_OK;
}



#endif
