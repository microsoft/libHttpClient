#pragma once

#include "IHttpProvider.h"
#include "IWebSocketProvider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// Container for all Platform specific components needed by libHttpClient.
// Implementations for each of these interfaces is needed for each supported platform.
// Additionally, each platform must implement its own version of PlatformInitialize.
struct PlatformComponents
{
    HC_UNIQUE_PTR<IHttpProvider> HttpProvider;
    HC_UNIQUE_PTR<IWebSocketProvider> WebSocketProvider;
};

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs);

NAMESPACE_XBOX_HTTP_CLIENT_END