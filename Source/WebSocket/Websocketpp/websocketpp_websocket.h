#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "WebSocket/hcwebsocket.h"
#include "Platform/IWebSocketProvider.h"
#include "utils.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#ifndef HC_NOWEBSOCKETS
bool IsWebSocketppConnection(std::shared_ptr<hc_websocket_impl> const& connection) noexcept;
inline bool ShouldForceTlsValidationForGdkSandbox(HRESULT sandboxQueryHr, const char* sandbox) noexcept
{
    return FAILED(sandboxQueryHr) || (sandbox != nullptr && 0 == str_icmp(sandbox, "RETAIL"));
}

inline bool ApplyTlsValidationBackstopForGdkConsole(bool allowProxyToDecryptHttps, bool isDevicePc) noexcept
{
    return isDevicePc ? allowProxyToDecryptHttps : false;
}

class WebSocketppProvider : public IWebSocketProvider, public IProviderLifecycle
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

    HRESULT OptionsResult(HCWebSocketOptions options) const noexcept override;

    void OnSuspending() noexcept override;
    void OnResuming() noexcept override;

private:
    void TrackConnection(std::shared_ptr<hc_websocket_impl> connection) noexcept;

    std::mutex m_connectionsMutex;
    std::vector<std::weak_ptr<hc_websocket_impl>> m_connections;
    std::atomic<bool> m_isSuspended{ false };
};
#endif

NAMESPACE_XBOX_HTTP_CLIENT_END
