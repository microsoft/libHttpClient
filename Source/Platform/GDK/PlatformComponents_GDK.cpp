#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/Curl/CurlProvider.h"
#include "HTTP/WinHttp/winhttp_provider.h"
#if !defined(HC_NOWEBSOCKETS) && defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
#include "HTTP/WinHttp/winhttp_websocket_hybrid.h"
#endif

#if HC_PLATFORM == HC_PLATFORM_GDK
#include "XSystem.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

// On GDK, desktop PC is the special case. Treat every non-PC device type as console
// so new console device types continue to take the safer console path by default.
static bool IsRunningOnXboxConsole()
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    return XSystemGetDeviceType() != XSystemDeviceType::Pc;
#else
    return false;
#endif
}

#ifndef HC_NOWEBSOCKETS
static bool IsGdkXboxCompressionWebSocketProviderEnabled() noexcept
{
#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION) && defined(HC_ENABLE_GDK_XBOX_WEBSOCKET_COMPRESSION)
    return true;
#else
    return false;
#endif
}

static HRESULT InitializeGdkWebSocketProviders(PlatformComponents& components, bool enableCompressionWebSocketProvider)
{
    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);

    auto winHttpProvider = initWinHttpResult.ExtractPayload();
    auto sharedWinHttpProvider = SharedPtr<WinHttpProvider>{ winHttpProvider.release(), std::move(winHttpProvider.get_deleter()), http_stl_allocator<WinHttpProvider>{} };

#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
    if (enableCompressionWebSocketProvider)
    {
        components.WebSocketProvider = http_allocate_unique<WinHttpHybrid_WebSocketProvider>(sharedWinHttpProvider);
        return S_OK;
    }
#else
    UNREFERENCED_PARAMETER(enableCompressionWebSocketProvider);
#endif

    components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(sharedWinHttpProvider);
    return S_OK;
}
#endif

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
        // For Xbox consoles with XCurl HTTP, still use WinHttp for the default WebSocket path.
        bool const enableCompressionWebSocketProvider = IsGdkXboxCompressionWebSocketProviderEnabled();
        if (!enableCompressionWebSocketProvider)
        {
            HC_TRACE_INFORMATION(HTTPCLIENT, "PlatformInitialize: Xbox console compression WebSocket provider is disabled by build policy");
        }
        RETURN_IF_FAILED(InitializeGdkWebSocketProviders(components, enableCompressionWebSocketProvider));
#endif
    }
    else
    {
        HC_TRACE_INFORMATION(HTTPCLIENT, "PlatformInitialize: Detected non-console platform. Using WinHTTP for HTTP");
        
        // Use WinHTTP for non-console platforms
        auto initWinHttpResult = WinHttpProvider::Initialize();
        RETURN_IF_FAILED(initWinHttpResult.hr);

        auto winHttpProvider = initWinHttpResult.ExtractPayload();

        // Use the same WinHttpProvider instance for both HTTP and the default WebSocket path.
        auto sharedWinHttpProvider = SharedPtr<WinHttpProvider>{ winHttpProvider.release(), std::move(winHttpProvider.get_deleter()), http_stl_allocator<WinHttpProvider>{} };
        
        components.HttpProvider = http_allocate_unique<WinHttp_HttpProvider>(sharedWinHttpProvider);

#ifndef HC_NOWEBSOCKETS
#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
        components.WebSocketProvider = http_allocate_unique<WinHttpHybrid_WebSocketProvider>(sharedWinHttpProvider);
#else
        components.WebSocketProvider = http_allocate_unique<WinHttp_WebSocketProvider>(sharedWinHttpProvider);
#endif
#endif
    }

    return S_OK;
}

// Test hooks for GDK suspend/resume testing. These now notify the built-in
// websocket providers through the provider lifecycle capability rather than
// reaching through NetworkState to a concrete provider type.
STDAPI_(void) HCWinHttpSuspend()
{
    auto httpSingleton = get_http_singleton();
    if (!httpSingleton || !httpSingleton->m_networkState)
    {
        return;
    }
    httpSingleton->m_networkState->NotifyWebSocketSuspending();
}

STDAPI_(void) HCWinHttpResume()
{
    auto httpSingleton = get_http_singleton();
    if (!httpSingleton || !httpSingleton->m_networkState)
    {
        return;
    }
    httpSingleton->m_networkState->NotifyWebSocketResuming();
}

NAMESPACE_XBOX_HTTP_CLIENT_END
