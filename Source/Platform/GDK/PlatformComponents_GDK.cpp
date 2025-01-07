#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/Curl/CurlProvider.h"
#include "HTTP/WinHttp/winhttp_provider.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    // We don't expect initArgs on GDK
    RETURN_HR_IF(E_INVALIDARG, initArgs);

    // XCurl will be used for HTTP
    auto initXCurlResult = CurlProvider::Initialize();
    RETURN_IF_FAILED(initXCurlResult.hr);

    components.HttpProvider = initXCurlResult.ExtractPayload();

#if !HC_NOWEBSOCKETS
    // WinHttp will be used for WebSockets
    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);
 
    auto winHttpProvider = initWinHttpResult.ExtractPayload();
    components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(SharedPtr<WinHttpProvider>{ winHttpProvider.release(), std::move(winHttpProvider.get_deleter()), http_stl_allocator<WinHttpProvider>{} });
#endif

    return S_OK;
}

// Test hooks for GDK Suspend/Resume testing
STDAPI_(void) HCWinHttpSuspend()
{
    auto httpSingleton = get_http_singleton();
    auto& winHttpProvider = dynamic_cast<WinHttp_WebSocketProvider*>(&httpSingleton->m_networkState->WebSocketProvider())->WinHttpProvider;
    winHttpProvider->Suspend();
}

STDAPI_(void) HCWinHttpResume()
{
    auto httpSingleton = get_http_singleton();
    auto& winHttpProvider = dynamic_cast<WinHttp_WebSocketProvider*>(&httpSingleton->m_networkState->WebSocketProvider())->WinHttpProvider;
    winHttpProvider->Resume();
}

NAMESPACE_XBOX_HTTP_CLIENT_END
