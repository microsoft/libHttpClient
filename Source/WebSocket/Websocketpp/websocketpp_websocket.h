#pragma once

#include "../hcwebsocket.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT CALLBACK WebSocketppConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* async,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
);

HRESULT CALLBACK WebSocketppSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* async,
    _In_opt_ void* context
);

HRESULT CALLBACK WebSocketppSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
);

HRESULT CALLBACK WebSocketppDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
);

NAMESPACE_XBOX_HTTP_CLIENT_END
