#pragma once

#include <httpClient/httpProvider.h>
#include "IWebSocketProvider.h"

#if !HC_NOWEBSOCKETS

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class ExternalWebSocketProvider : public IWebSocketProvider
{
public:
    static ExternalWebSocketProvider& Get() noexcept;

    HRESULT SetCallbacks(
        HCWebSocketConnectFunction websocketConnectFunc,
        HCWebSocketSendMessageFunction websocketSendMessageFunc,
        HCWebSocketSendBinaryMessageFunction websocketSendBinaryMessageFunc,
        HCWebSocketDisconnectFunction websocketDisconnectFunc,
        void* context
    ) noexcept;

    HRESULT GetCallbacks(
        HCWebSocketConnectFunction* websocketConnectFunc,
        HCWebSocketSendMessageFunction* websocketSendMessageFunc,
        HCWebSocketSendBinaryMessageFunction* websocketSendBinaryMessageFunc,
        HCWebSocketDisconnectFunction* websocketDisconnectFunc,
        void** context
    ) const noexcept;

    bool HasCallbacks() const noexcept;

public: // IWebSocketProvider
    virtual HRESULT ConnectAsync(
        String const& uri,
        String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async
    ) noexcept override;

    virtual HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async
    ) noexcept override;

    virtual HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* async
    ) noexcept override;

    virtual HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept override;

    ExternalWebSocketProvider(ExternalWebSocketProvider const&) = delete;
    ExternalWebSocketProvider& operator=(ExternalWebSocketProvider const&) = delete;
    ~ExternalWebSocketProvider() = default;

private:
    ExternalWebSocketProvider() = default;

    HCWebSocketConnectFunction m_connect{ nullptr };
    HCWebSocketSendMessageFunction m_send{ nullptr };
    HCWebSocketSendBinaryMessageFunction m_sendBinary{ nullptr };
    HCWebSocketDisconnectFunction m_disconnect{ nullptr };
    void* m_context{ nullptr };
};

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif
