#pragma once

#include "httpcall.h"
#include "hcwebsocket.h"

#if HC_PLATFORM == HC_PLATFORM_WIN32 
#include "WinHttp/winhttp_provider.h"
#elif HC_PLATFORM == HC_PLATFORM_GDK
#include "Curl/CurlProvider.h"
#if !HC_NOWEBSOCKETS
#include "WinHttp/winhttp_provider.h"
#endif
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
#include "HTTP/Android/android_platform_context.h"
#endif

// Global context passed to HTTP/WebSocket hooks. Will be opaque to client providers, but contains needed context for default providers.
struct HC_PERFORM_ENV
{
public:
    // Called during static initialization to get default hooks for the platform. These can be overridden by clients with
    // HCSetHttpCallPerformFunction and HCSetWebSocketFunctions
    static HttpPerformInfo GetPlatformDefaultHttpHandlers();
#if !HC_NOWEBSOCKETS
    static WebSocketPerformInfo GetPlatformDefaultWebSocketHandlers();
#endif

    // Called during HCInitialize. HC_PERFORM_ENV will be passed to Http/WebSocket hooks and is needed by default providers
    static Result<HC_UNIQUE_PTR<HC_PERFORM_ENV>> Initialize(HCInitArgs* args) noexcept;

    HC_PERFORM_ENV(const HC_PERFORM_ENV&) = delete;
    HC_PERFORM_ENV(HC_PERFORM_ENV&&) = delete;
    HC_PERFORM_ENV& operator=(const HC_PERFORM_ENV&) = delete;
    virtual ~HC_PERFORM_ENV() = default;

#if HC_PLATFORM == HC_PLATFORM_WIN32
    std::shared_ptr<xbox::httpclient::WinHttpProvider> winHttpProvider;
#elif HC_PLATFORM == HC_PLATFORM_GDK
    std::shared_ptr<xbox::httpclient::CurlProvider> curlProvider;
#if !HC_NOWEBSOCKETS
    std::shared_ptr<xbox::httpclient::WinHttpProvider> winHttpProvider;
#endif
#elif HC_PLATFORM == HC_PLATFORM_ANDROID
    std::shared_ptr<AndroidPlatformContext> androidPlatformContext;
#endif
private:
    HC_PERFORM_ENV() = default;
};

using PerformEnv = HC_UNIQUE_PTR<HC_PERFORM_ENV>;
