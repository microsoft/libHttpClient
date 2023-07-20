#pragma once

#include "WebSocket/hcwebsocket.h"
#include "Platform/IWebSocketProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#if !HC_NOWEBSOCKETS
class WebSocketppProvider : public IWebSocketProvider
{
public:
    HRESULT ConnectAsync(
        String const& uri,
        String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* async
    ) noexcept override;

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept override;
};
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
