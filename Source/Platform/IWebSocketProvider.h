#pragma once

#include <httpClient/httpClient.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#if !HC_NOWEBSOCKETS

// Internal interface for a WebSocket Provider. Used as a base class for any in-box WebSocket implementations.
class IWebSocketProvider
{
public:
    IWebSocketProvider() = default;
    IWebSocketProvider(IWebSocketProvider const&) = delete;
    IWebSocketProvider& operator=(IWebSocketProvider const&) = delete;
    virtual ~IWebSocketProvider() = default;


    virtual HRESULT ConnectAsync(
        String const& uri,
        String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async
    ) noexcept = 0;

    virtual HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async
    ) noexcept = 0;

    virtual HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* async
    ) noexcept = 0;

    virtual HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept = 0;
};
#endif // !HC_NOWEBSOCKETS

NAMESPACE_XBOX_HTTP_CLIENT_END
