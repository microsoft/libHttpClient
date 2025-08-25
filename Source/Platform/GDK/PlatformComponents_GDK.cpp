#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/Curl/CurlProvider.h"
#include "HTTP/WinHttp/winhttp_provider.h"

#if HC_PLATFORM == HC_PLATFORM_GDK
#include "XSystem.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// Helper function to detect if running on Xbox console hardware
static bool IsRunningOnXboxConsole()
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    auto deviceType = XSystemGetDeviceType();

    // Explicitly list all Xbox console device types
    return deviceType == XSystemDeviceType::XboxOne ||
           deviceType == XSystemDeviceType::XboxOneS ||
           deviceType == XSystemDeviceType::XboxOneX ||
           deviceType == XSystemDeviceType::XboxOneXDevkit ||
           deviceType == XSystemDeviceType::XboxScarlettLockhart || // Xbox Series S
           deviceType == XSystemDeviceType::XboxScarlettAnaconda || // Xbox Series X
           deviceType == XSystemDeviceType::XboxScarlettDevkit; // Xbox Series Devkit
#else
    return false;
#endif
}

HRESULT PlatformInitialize(PlatformComponents& components, HCInitArgs* initArgs)
{
    // We don't expect initArgs on GDK
    RETURN_HR_IF(E_INVALIDARG, initArgs);

    // Detect runtime platform to choose appropriate HTTP provider
    if (IsRunningOnXboxConsole())
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "PlatformInitialize: Detected Xbox console, using XCurl for HTTP");
        
        // Use XCurl for Xbox console with full PLM support
        auto initXCurlResult = CurlProvider::Initialize();
        RETURN_IF_FAILED(initXCurlResult.hr);

        components.HttpProvider = initXCurlResult.ExtractPayload();

#ifndef HC_NOWEBSOCKETS
        // For Xbox consoles with XCurl HTTP, still use WinHttp for WebSockets
        auto initWinHttpResult = WinHttpProvider::Initialize();
        RETURN_IF_FAILED(initWinHttpResult.hr);
 
        auto winHttpProvider = initWinHttpResult.ExtractPayload();
        components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(SharedPtr<WinHttpProvider>{ winHttpProvider.release(), std::move(winHttpProvider.get_deleter()), http_stl_allocator<WinHttpProvider>{} });
#endif
    }
    else
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "PlatformInitialize: Detected non-console platform. Using WinHTTP for HTTP");
        
        // Use WinHTTP for non-console platforms
        auto initWinHttpResult = WinHttpProvider::Initialize();
        RETURN_IF_FAILED(initWinHttpResult.hr);

        auto winHttpProvider = initWinHttpResult.ExtractPayload();
        
        // Use the same WinHttpProvider instance for both HTTP and WebSocket
        auto sharedWinHttpProvider = SharedPtr<WinHttpProvider>{ winHttpProvider.release(), std::move(winHttpProvider.get_deleter()), http_stl_allocator<WinHttpProvider>{} };
        
        components.HttpProvider = http_allocate_unique<WinHttp_HttpProvider>(sharedWinHttpProvider);

#ifndef HC_NOWEBSOCKETS
        components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(sharedWinHttpProvider);
#endif
    }

    return S_OK;
}

// Test hooks for GDK Suspend/Resume testing
// Note: These hooks assume WinHttp WebSocket provider is available.
// They will work correctly on both Xbox consoles and non-console platforms
// since both configurations use WinHttp for WebSockets.
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
