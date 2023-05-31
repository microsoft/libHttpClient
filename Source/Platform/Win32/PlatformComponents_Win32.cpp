#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/WinHttp/winhttp_provider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class WinHttp_HttpProvider : public IHttpProvider
{
public:
    WinHttp_HttpProvider(std::shared_ptr<WinHttpProvider> provider);

    HRESULT PerformAsync(
        HCCallHandle callHandle,
        XAsyncBlock* async
    ) noexcept override;

private:
    std::shared_ptr<WinHttpProvider> m_sharedProvider;
};

class WinHttp_WebSocketProvider : public IWebSocketProvider
{
public:
    WinHttp_WebSocketProvider(std::shared_ptr<WinHttpProvider> provider);

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

private:
    std::shared_ptr<WinHttpProvider> m_sharedProvider;
};

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    // We don't expect initArgs on Win32
    RETURN_HR_IF(E_INVALIDARG, initArgs);

    // Initialize Shared WinHttpProvider. It acts as both the WebSocketProvider and the HttpProvider
    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);

    std::shared_ptr<WinHttpProvider> sharedProvider{ initWinHttpResult.ExtractPayload() };

    components.HttpProvider = http_allocate_unique<WinHttp_HttpProvider>(sharedProvider);
    components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(sharedProvider);

    return S_OK;
}

WinHttp_HttpProvider::WinHttp_HttpProvider(std::shared_ptr<WinHttpProvider> provider) : m_sharedProvider{ std::move(provider) }
{
}

HRESULT WinHttp_HttpProvider::PerformAsync(HCCallHandle callHandle, XAsyncBlock* async) noexcept
{
    return m_sharedProvider->PerformAsync(callHandle, async);
}

WinHttp_WebSocketProvider::WinHttp_WebSocketProvider(std::shared_ptr<WinHttpProvider> provider) : m_sharedProvider{ std::move(provider) }
{
}

HRESULT WinHttp_WebSocketProvider::ConnectAsync(
    String const& uri,
    String const& subprotocol,
    HCWebsocketHandle websocketHandle,
    XAsyncBlock* async
) noexcept
{
    return m_sharedProvider->ConnectAsync(uri, subprotocol, websocketHandle, async);
}

HRESULT WinHttp_WebSocketProvider::SendAsync(
    HCWebsocketHandle websocketHandle,
    const char* message,
    XAsyncBlock* async
) noexcept
{
    return m_sharedProvider->SendAsync(websocketHandle, message, async);
}

HRESULT WinHttp_WebSocketProvider::SendBinaryAsync(
    HCWebsocketHandle websocketHandle,
    const uint8_t* payloadBytes,
    uint32_t payloadSize,
    XAsyncBlock* asyncBlock
) noexcept
{
    return m_sharedProvider->SendBinaryAsync(websocketHandle, payloadBytes, payloadSize, asyncBlock);
}

HRESULT WinHttp_WebSocketProvider::Disconnect(
    HCWebsocketHandle websocketHandle,
    HCWebSocketCloseStatus closeStatus
) noexcept
{
    return m_sharedProvider->Disconnect(websocketHandle, closeStatus);
}

NAMESPACE_XBOX_HTTP_CLIENT_END
