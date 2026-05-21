#include "pch.h"
#include "winhttp_websocket_hybrid.h"

#if !defined(HC_NOWEBSOCKETS) && defined(HC_ENABLE_WEBSOCKET_COMPRESSION)

#include "winhttp_connection.h"
#include "WebSocket/websocket_options.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"

namespace
{

void TraceUnexpectedActiveProvider(char const* operation, HCWebsocketHandle websocketHandle) noexcept
{
    if (websocketHandle == nullptr)
    {
        HC_TRACE_ERROR(WEBSOCKET, "[Hybrid] %s called with null websocket handle", operation);
        return;
    }

    if (!websocketHandle->websocket)
    {
        HC_TRACE_ERROR(WEBSOCKET, "[Hybrid] %s called with handle missing WebSocket state", operation);
        return;
    }

    auto const id = TO_ULL(websocketHandle->websocket->id);
    auto const& impl = websocketHandle->websocket->impl;
    if (!impl)
    {
        HC_TRACE_ERROR(WEBSOCKET, "[Hybrid] %s [ID %llu] called before an active provider implementation was attached", operation, id);
        return;
    }

    HC_TRACE_ERROR(WEBSOCKET, "[Hybrid] %s [ID %llu] encountered unknown active provider implementation", operation, id);
}

}

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
    if (!provider)
    {
        TraceUnexpectedActiveProvider("SendAsync", websocketHandle);
        return E_UNEXPECTED;
    }
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
    if (!provider)
    {
        TraceUnexpectedActiveProvider("SendBinaryAsync", websocketHandle);
        return E_UNEXPECTED;
    }
    return provider->SendBinaryAsync(websocketHandle, payloadBytes, payloadSize, asyncBlock);
}

HRESULT WinHttpHybrid_WebSocketProvider::Disconnect(
    HCWebsocketHandle websocketHandle,
    HCWebSocketCloseStatus closeStatus
) noexcept
{
    auto provider = ActiveProvider(websocketHandle);
    if (!provider)
    {
        TraceUnexpectedActiveProvider("Disconnect", websocketHandle);
        return E_UNEXPECTED;
    }
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

    if (RequestsWebSocketCompression(options))
    {
        return m_wsppProvider->OptionsResult(options);
    }

    return S_OK;
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

IWebSocketProvider& WinHttpHybrid_WebSocketProvider::ConnectProvider(HCWebsocketHandle websocketHandle) const noexcept
{
    if (RequestsWebSocketCompression(websocketHandle->websocket->Options()))
    {
        return *m_wsppProvider;
    }

    return *m_winHttpProvider;
}

IWebSocketProvider* WinHttpHybrid_WebSocketProvider::ActiveProvider(HCWebsocketHandle websocketHandle) const noexcept
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
