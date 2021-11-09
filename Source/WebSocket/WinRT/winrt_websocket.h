#pragma once

#include "hcwebsocket.h"

HRESULT CALLBACK WinRTWebSocketConnectAsync(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
);

HRESULT CALLBACK WinRTWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ PCSTR message,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
);

HRESULT CALLBACK WinRTWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
);

HRESULT CALLBACK WinRTWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
);
