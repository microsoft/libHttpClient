#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/WinHttp/winhttp_provider.h"
#if !defined(HC_NOWEBSOCKETS) && defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
#include "HTTP/WinHttp/winhttp_websocket_hybrid.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    // We don't expect initArgs on Win32
    RETURN_HR_IF(E_INVALIDARG, initArgs);

    // Initialize Shared WinHttpProvider. It acts as both the WebSocketProvider and the HttpProvider
    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);

    auto winHttpProvider = initWinHttpResult.ExtractPayload();
    std::shared_ptr<WinHttpProvider> sharedProvider{ winHttpProvider.release(), std::move(winHttpProvider.get_deleter()), http_stl_allocator<WinHttpProvider>{} };

    components.HttpProvider = http_allocate_unique<WinHttp_HttpProvider>(sharedProvider);
#ifndef HC_NOWEBSOCKETS
#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
    components.WebSocketProvider = http_allocate_unique<WinHttpHybrid_WebSocketProvider>(sharedProvider);
#else
    components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(sharedProvider);
#endif
#endif

    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
