#pragma once

#include "winhttp_provider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#if !defined(HC_NOWEBSOCKETS) && defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
class WinHttpHybrid_WebSocketProvider final : public IWebSocketProvider, public IProviderLifecycle
{
public:
    WinHttpHybrid_WebSocketProvider(std::shared_ptr<WinHttpProvider> provider);

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
        XAsyncBlock* asyncBlock
    ) noexcept override;

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept override;

    HRESULT OptionsResult(HCWebSocketOptions options) const noexcept override;

    void OnSuspending() noexcept override;
    void OnResuming() noexcept override;

private:
    IWebSocketProvider& ConnectProvider(HCWebsocketHandle websocketHandle) const noexcept;
    IWebSocketProvider* ActiveProvider(HCWebsocketHandle websocketHandle) const noexcept;

    UniquePtr<IWebSocketProvider> m_winHttpProvider;
    UniquePtr<IWebSocketProvider> m_wsppProvider;
};
#endif // !HC_NOWEBSOCKETS && HC_ENABLE_WEBSOCKET_COMPRESSION

NAMESPACE_XBOX_HTTP_CLIENT_END
