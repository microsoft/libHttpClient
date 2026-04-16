#include "pch.h"
#include "winhttp_websocket_hybrid.h"

#if !defined(HC_NOWEBSOCKETS) && defined(HC_ENABLE_WEBSOCKET_COMPRESSION)

#include "winhttp_connection.h"
#include "WebSocket/websocket_options.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

WinHttpHybrid_WebSocketProvider::WinHttpHybrid_WebSocketProvider(std::shared_ptr<xbox::httpclient::WinHttpProvider> provider) :
    m_winHttpProvider{ http_allocate_unique<WinHttp_WebSocketProvider>(provider) },
    m_wsppProvider{ http_allocate_unique<WebSocketppProvider>() }
{
}

HRESULT WinHttpHybrid_WebSocketProvider::ConnectAsync(
    String const& uri,
    String const& subprotocol,
    HCWebsocketHandle websocketHandle,
    XAsyncBlock* async
) noexcept
{
    return ConnectProvider(websocketHandle).ConnectAsync(uri, subprotocol, websocketHandle, async);
}

HRESULT WinHttpHybrid_WebSocketProvider::SendAsync(
    HCWebsocketHandle websocketHandle,
    const char* message,
    XAsyncBlock* async
) noexcept
{
    auto provider = ActiveProvider(websocketHandle);
    RETURN_HR_IF(E_UNEXPECTED, !provider);
    return provider->SendAsync(websocketHandle, message, async);
}

HRESULT WinHttpHybrid_WebSocketProvider::SendBinaryAsync(
    HCWebsocketHandle websocketHandle,
    const uint8_t* payloadBytes,
    uint32_t payloadSize,
    XAsyncBlock* asyncBlock
) noexcept
{
    auto provider = ActiveProvider(websocketHandle);
    RETURN_HR_IF(E_UNEXPECTED, !provider);
    return provider->SendBinaryAsync(websocketHandle, payloadBytes, payloadSize, asyncBlock);
}

HRESULT WinHttpHybrid_WebSocketProvider::Disconnect(
    HCWebsocketHandle websocketHandle,
    HCWebSocketCloseStatus closeStatus
) noexcept
{
    auto provider = ActiveProvider(websocketHandle);
    RETURN_HR_IF(E_UNEXPECTED, !provider);
    return provider->Disconnect(websocketHandle, closeStatus);
}

HRESULT WinHttpHybrid_WebSocketProvider::OptionsResult(HCWebSocketOptions options) const noexcept
{
    if (HasUnsupportedWebSocketOptions(options))
    {
        return E_NOT_SUPPORTED;
    }

    if (RequestsLegacyWebSocketSemantics(options))
    {
        return S_OK;
    }

    return m_wsppProvider->OptionsResult(options);
}

void WinHttpHybrid_WebSocketProvider::OnSuspending() noexcept
{
    if (auto lifecycle = GetProviderLifecycle(m_winHttpProvider.get()))
    {
        lifecycle->OnSuspending();
    }

    if (auto lifecycle = GetProviderLifecycle(m_wsppProvider.get()))
    {
        lifecycle->OnSuspending();
    }
}

void WinHttpHybrid_WebSocketProvider::OnResuming() noexcept
{
    if (auto lifecycle = GetProviderLifecycle(m_winHttpProvider.get()))
    {
        lifecycle->OnResuming();
    }

    if (auto lifecycle = GetProviderLifecycle(m_wsppProvider.get()))
    {
        lifecycle->OnResuming();
    }
}

IWebSocketProvider& WinHttpHybrid_WebSocketProvider::ConnectProvider(HCWebsocketHandle websocketHandle) noexcept
{
    if (websocketHandle->websocket->UsesDeterministicSemantics())
    {
        return *m_wsppProvider;
    }

    return *m_winHttpProvider;
}

IWebSocketProvider* WinHttpHybrid_WebSocketProvider::ActiveProvider(HCWebsocketHandle websocketHandle) noexcept
{
    if (websocketHandle == nullptr || !websocketHandle->websocket)
    {
        return nullptr;
    }

    auto const& impl = websocketHandle->websocket->impl;
    if (!impl)
    {
        return nullptr;
    }

    if (std::dynamic_pointer_cast<WinHttpConnection>(impl))
    {
        return m_winHttpProvider.get();
    }

    if (IsWebSocketppConnection(impl))
    {
        return m_wsppProvider.get();
    }

    return nullptr;
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif // !HC_NOWEBSOCKETS && HC_ENABLE_WEBSOCKET_COMPRESSION
