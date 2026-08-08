#include "pch.h"
#include "Platform/PlatformComponents.h"
#include "HTTP/WinHttp/winhttp_provider.h"
#if !defined(HC_NOWEBSOCKETS) && defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
#include "HTTP/WinHttp/winhttp_websocket_hybrid.h"
#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#ifndef HC_NOWEBSOCKETS
static bool IsGdkXboxCompressionWebSocketProviderEnabled() noexcept
{
#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION) && defined(HC_ENABLE_GDK_XBOX_WEBSOCKET_COMPRESSION)
    return true;
#else
    return false;
#endif
}

// Selects the default WebSocket provider. Shares the caller's WinHttpProvider instance so the
// process has exactly one provider, and therefore one PLM registration and one suspend drain.
static HRESULT InitializeGdkWebSocketProviders(
    PlatformComponents& components,
    bool enableCompressionWebSocketProvider,
    SharedPtr<WinHttpProvider> const& sharedWinHttpProvider)
{
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

    HC_TRACE_INFORMATION(HTTPCLIENT, "PlatformInitialize: Using WinHTTP for HTTP");

    auto initWinHttpResult = WinHttpProvider::Initialize();
    RETURN_IF_FAILED(initWinHttpResult.hr);

    auto winHttpProvider = initWinHttpResult.ExtractPayload();

    // Use the same WinHttpProvider instance for both HTTP and the default WebSocket path.
    auto sharedWinHttpProvider = SharedPtr<WinHttpProvider>{ winHttpProvider.release(), std::move(winHttpProvider.get_deleter()), http_stl_allocator<WinHttpProvider>{} };

    components.HttpProvider = http_allocate_unique<WinHttp_HttpProvider>(sharedWinHttpProvider);

#ifndef HC_NOWEBSOCKETS
    RETURN_IF_FAILED(InitializeGdkWebSocketProviders(components, IsGdkXboxCompressionWebSocketProviderEnabled(), sharedWinHttpProvider));
#endif

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
