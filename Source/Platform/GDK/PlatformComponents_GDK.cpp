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
 
    components.WebSocketProvider = initWinHttpResult.ExtractPayload();
#endif

    return S_OK;
}

// Test hooks for GDK Suspend/Resume testing
void HCWinHttpSuspend()
{
    //auto httpSingleton = get_http_singleton();
    //httpSingleton->m_performEnv->winHttpProvider->Suspend();
}

void HCWinHttpResume()
{
    //auto httpSingleton = get_http_singleton();
    //httpSingleton->m_performEnv->winHttpProvider->Resume();
}

NAMESPACE_XBOX_HTTP_CLIENT_END
