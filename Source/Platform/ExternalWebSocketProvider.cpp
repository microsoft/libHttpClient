#include "pch.h"
#include "ExternalWebSocketProvider.h"

#ifndef HC_NOWEBSOCKETS

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

ExternalWebSocketProvider& ExternalWebSocketProvider::Get() noexcept
{
    static ExternalWebSocketProvider s_instance{};
    return s_instance;
}

HRESULT ExternalWebSocketProvider::SetCallbacks(
    HCWebSocketConnectFunction websocketConnectFunc,
    HCWebSocketSendMessageFunction websocketSendMessageFunc,
    HCWebSocketSendBinaryMessageFunction websocketSendBinaryMessageFunc,
    HCWebSocketDisconnectFunction websocketDisconnectFunc,
    void* context
) noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !websocketConnectFunc);
    RETURN_HR_IF(E_INVALIDARG, !websocketSendMessageFunc);
    RETURN_HR_IF(E_INVALIDARG, !websocketSendBinaryMessageFunc);
    RETURN_HR_IF(E_INVALIDARG, !websocketDisconnectFunc);

    m_connect = websocketConnectFunc;
    m_send = websocketSendMessageFunc;
    m_sendBinary = websocketSendBinaryMessageFunc;
    m_disconnect = websocketDisconnectFunc;
    m_context = context;

    return S_OK;
}

HRESULT ExternalWebSocketProvider::GetCallbacks(
    HCWebSocketConnectFunction* websocketConnectFunc,
    HCWebSocketSendMessageFunction* websocketSendMessageFunc,
    HCWebSocketSendBinaryMessageFunction* websocketSendBinaryMessageFunc,
    HCWebSocketDisconnectFunction* websocketDisconnectFunc,
    void** context
) const noexcept
{
    RETURN_HR_IF(E_INVALIDARG, !websocketConnectFunc);
    RETURN_HR_IF(E_INVALIDARG, !websocketSendMessageFunc);
    RETURN_HR_IF(E_INVALIDARG, !websocketSendBinaryMessageFunc);
    RETURN_HR_IF(E_INVALIDARG, !websocketDisconnectFunc);
    RETURN_HR_IF(E_INVALIDARG, !context);

    *websocketConnectFunc = m_connect;
    *websocketSendMessageFunc = m_send;
    *websocketSendBinaryMessageFunc = m_sendBinary;
    *websocketDisconnectFunc = m_disconnect;
    *context = m_context;

    return S_OK;
}

bool ExternalWebSocketProvider::HasCallbacks() const noexcept
{
    assert((m_connect && m_send && m_sendBinary && m_disconnect) || (!m_connect && !m_send && !m_sendBinary && !m_disconnect));
    return m_connect;
}

HRESULT ExternalWebSocketProvider::ConnectAsync(
    String const& uri,
    String const& subprotocol,
    HCWebsocketHandle websocketHandle,
    XAsyncBlock* async
) noexcept
{
    return m_connect(uri.data(), subprotocol.data(), websocketHandle, async, m_context, nullptr);
}

HRESULT ExternalWebSocketProvider::SendAsync(
    HCWebsocketHandle websocketHandle,
    const char* message,
    XAsyncBlock* async
) noexcept
{
    return m_send(websocketHandle, message, async, m_context);
}

HRESULT ExternalWebSocketProvider::SendBinaryAsync(
    HCWebsocketHandle websocketHandle,
    const uint8_t* payloadBytes,
    uint32_t payloadSize,
    XAsyncBlock* async
) noexcept
{
    return m_sendBinary(websocketHandle, payloadBytes, payloadSize, async, m_context);
}

HRESULT ExternalWebSocketProvider::Disconnect(
    HCWebsocketHandle websocketHandle,
    HCWebSocketCloseStatus closeStatus
) noexcept
{
    return m_disconnect(websocketHandle, closeStatus, m_context);
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif // !HC_NOWEBSOCKETS
