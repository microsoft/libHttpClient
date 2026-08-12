#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/Curl/CurlProvider.h"
#include "WebSocket/Websocketpp/websocketpp_websocket.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    // We don't expect initArgs on linux
    RETURN_HR_IF(E_INVALIDARG, initArgs);

    // libcurl will be used for HTTP
    auto initCurlResult = CurlProvider::Initialize();
    RETURN_IF_FAILED(initCurlResult.hr);

    components.HttpProvider = initCurlResult.ExtractPayload();

#ifndef HC_NOWEBSOCKETS
    // Websocketpp will be used for WebSockets
    components.WebSocketProvider = http_allocate_unique<WebSocketppProvider>();
#endif

    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
