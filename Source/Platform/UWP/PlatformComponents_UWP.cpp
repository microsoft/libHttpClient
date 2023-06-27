#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/XMLHttp/xmlhttp_provider.h"
#include "WebSocket/WinRT/winrt_websocket.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    RETURN_HR_IF(E_INVALIDARG, initArgs);

    components.HttpProvider = http_allocate_unique<XmlHttpProvider>();
#if !HC_NOWEBSOCKETS
    components.WebSocketProvider = http_allocate_unique<WinRTWebSocketProvider>();
#endif

    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END