#pragma once

#include "Platform/IWebSocketProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#if !HC_NOWEBSOCKETS
class PlatformComponents_Android;

class AndroidWebSocketProvider : public IWebSocketProvider
{
public:
    AndroidWebSocketProvider(SharedPtr<PlatformComponents_Android> platformComponents);
    AndroidWebSocketProvider(AndroidWebSocketProvider const&) = delete;
    AndroidWebSocketProvider& operator=(AndroidWebSocketProvider const&) = delete;
    virtual ~AndroidWebSocketProvider() = default;

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

private:
    SharedPtr<PlatformComponents_Android> m_platformComponents;
};
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
