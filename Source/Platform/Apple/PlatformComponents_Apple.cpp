#include "pch.h"
#include "PlatformComponents.h"
#include "HTTP/Apple/http_apple.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    // no initArgs expected for Apple
    RETURN_HR_IF(E_INVALIDARG, initArgs);
    
    components.HttpProvider = http_allocate_unique<AppleHttpProvider>();
#if !HC_NOWEBSOCKETS
    components.WebSocketProvider = http_allocate_unique<WebSocketppProvider>();
#endif
    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
