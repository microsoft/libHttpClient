#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "Platform/IHttpProvider.h"
#include "Platform/IWebSocketProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class GenericHttpProvider : public IHttpProvider
{
public:
    HRESULT PerformAsync(
        HCCallHandle /*callHandle*/,
        XAsyncBlock* /*async*/
    ) noexcept override
    {
        return E_NOTIMPL;
    }
};

class GenericWebSocketProvider : public IWebSocketProvider
{
public:
    HRESULT ConnectAsync(
        String const& /*uri*/,
        String const& /*subprotocol*/,
        HCWebsocketHandle /*websocketHandle*/,
        XAsyncBlock* /*async*/
    ) noexcept override
    {
        return E_NOTIMPL;
    }

    HRESULT SendAsync(
        HCWebsocketHandle /*websocketHandle*/,
        const char* /*message*/,
        XAsyncBlock* /*async*/
    ) noexcept override
    {
        return E_NOTIMPL;
    }

    HRESULT SendBinaryAsync(
        HCWebsocketHandle /*websocketHandle*/,
        const uint8_t* /*payloadBytes*/,
        uint32_t /*payloadSize*/,
        XAsyncBlock* /*async*/
    ) noexcept override
    {
        return E_NOTIMPL;
    }

    HRESULT Disconnect(
        HCWebsocketHandle /*websocketHandle*/,
        HCWebSocketCloseStatus /*closeStatus*/
    ) noexcept override
    {
        return E_NOTIMPL;
    }
};

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* /*initArgs*/)
{
    components.HttpProvider = http_allocate_unique<GenericHttpProvider>();
    components.WebSocketProvider = http_allocate_unique<GenericWebSocketProvider>();

    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END