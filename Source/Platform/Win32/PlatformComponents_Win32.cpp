#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/WinHttp/winhttp_provider.h"

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
#if !HC_NOWEBSOCKETS
    components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(sharedProvider);
#endif

    return S_OK;
}

NAMESPACE_XBOX_HTTP_CLIENT_END
