#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/Curl/CurlProvider.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    // We don't expect initArgs on linux
    RETURN_HR_IF(E_INVALIDARG, initArgs);

    // XCurl will be used for HTTP
    auto initXCurlResult = CurlProvider::Initialize();
    RETURN_IF_FAILED(initXCurlResult.hr);

    components.HttpProvider = initXCurlResult.ExtractPayload();

#ifndef HC_NOWEBSOCKETS
    // Websocketpp will be used for WebSockets
    components.WebSocketProvider = http_allocate_unique<WebSocketppProvider>();
#endif

    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
