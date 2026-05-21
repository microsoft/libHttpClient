## Welcome!

libHttpClient provides a platform abstraction layer for HTTP and WebSocket, and is designed for use by the Microsoft Xbox Live Service API [(XSAPI)](https://github.com/Microsoft/xbox-live-api), [PlayFab SDK](https://github.com/PlayFab/PlayFabCSdk), and game devs.  If you want to contribute to the project, please talk to us to avoid overlap.

## Goals

- libHttpClient provides a **platform abstraction layer** for **HTTP** and **WebSocket**
- Stock implementations that call **native platform HTTP / WebSocket APIs** on GDK, XDK ERA, Win32 Win7+, UWP, iOS, Android, Linux 
- Caller can add support for **other platforms via callback** API
- Sample showing off an [**HTTP implementation via Curl**](https://github.com/curl/curl) via this callback
- Designed around the needs of **professional game developers** that use Xbox Live and PlayFab
- **used by** the Microsoft Xbox Live Service API [(XSAPI)](https://github.com/Microsoft/xbox-live-api) and [PlayFab SDK](https://github.com/PlayFab/PlayFabCSdk)
- Builds for **GDK, XDK ERA, UWP, Win32 Win7+, iOS, Android, and Linux**
- Public API is a **flat C API**
- **Asynchronous** API
- Public API **supports simple P/Invoke** without needing to use the ["C#/.NET P/Invoke Interop SDK" or C++/CLI](https://en.wikipedia.org/wiki/Platform_Invocation_Services#C.23.2F.NET_P.2FInvoke_Interop_SDK)
- Public APIs to **manage async tasks** 
- Async data can be returned to a specific game thread so the **game doesn't need to marshal the data between threads**
- **No dependencies** on PPL or Boost
- **Does not throw exceptions** as a means of non-fatal error reporting
- Caller controlled **memory allocation** via callback API (similar to GDK's XMemAlloc)
- Built-in **logging** support to either debug output and/or callback
- **Built in retry** support according to Xbox Live best practices (obey Retry-After header, jitter wait, etc) according to https://docs.microsoft.com/en-us/windows/uwp/xbox-live/using-xbox-live/best-practices/best-practices-for-calling-xbox-live#retry-logic-best-practices
- **Xbox Live throttle** handling logic
- Built-in API support to switch to **mock layer**
- **Open source** project on GitHub
- Unit tests via TAEF
- End to end samples for UWP C++, XDK ERA, Win32, iOS, and Android

## HTTP API Usage

[See public header](../../tree/master/Include/httpClient/httpClient.h)

1. Optionally call HCMemSetFunctions() to control memory allocations
1. Call HCInitialize()
1. Optionally call HCSettingsSet*()
1. Call HCHttpCallCreate() to create a new HCCallHandle
1. Call HCHttpCallRequestSet*() to prepare the HCCallHandle
1. Call HCHttpCallPerform() to perform an HTTP call using the HCCallHandle.  
1. The perform call is asynchronous, so the work will be done on a background thread which calls DispatchAsyncQueue( ..., AsyncQueueCallbackType_Work ).  The results will return to the callback on the thread that calls DispatchAsyncQueue( ..., AsyncQueueCallbackType_Completion ).
1. Call HCHttpCallResponseGet*() to get the HTTP response of the HCCallHandle
1. Call HCHttpCallCloseHandle() to cleanup the HCCallHandle
1. Repeat 4-8 for each new HTTP call
1. Call HCCleanup() at shutdown before your memory manager set in step 1 is shutdown

## WebSocket API Usage

[See public header](../../tree/master/Include/httpClient/httpClient.h) and [Win32 WebSocket sample](../../tree/master/Samples/Win32WebSocket)

1. Follow steps 1-3 from HTTP API setup above
1. Call HCWebSocketCreate() to create a new HCWebsocketHandle with message/binary message/close event callbacks
1. Optionally call HCWebSocketSetOptions() before connect to explicitly preserve legacy behavior or select deterministic behavior and compression requests
1. **On Win32 and GDK legacy behavior, for payloads that may exceed the receive buffer (>20KB by default)**: Call HCWebSocketSetBinaryMessageFragmentEventFunction() to handle oversized incoming payloads
1. Optionally call HCWebSocketSetMaxReceiveBufferSize() before connect to adjust the deterministic inbound message-size limit, or on Win32 / GDK legacy behavior the fragment threshold
1. Optionally call HCWebSocketSetPingInterval() to adjust keepalive behavior
1. Call HCWebSocketConnectAsync() to connect to the WebSocket server
1. Call HCWebSocketSendMessageAsync() or HCWebSocketSendBinaryMessageAsync() to send messages
1. Handle incoming messages via your registered callbacks
1. Call HCWebSocketDisconnect() or HCWebSocketDisconnectWithStatus() when done
1. Call HCWebSocketCloseHandle() to cleanup
1. Call HCCleanup() at shutdown

### Important WebSocket Notes

- **No call to HCWebSocketSetOptions()**: The socket uses the platform's legacy behavior.
- **Legacy Win32 / GDK default buffer**: The legacy receive buffer defaults to 20KB (20,480 bytes).
- **Legacy Win32 / GDK oversized payload path**: When an incoming payload exceeds the configured receive buffer, it is surfaced through HCWebSocketSetBinaryMessageFragmentEventFunction() as raw bytes.
- **Legacy Win32 / GDK text overflow behavior**: Oversized UTF-8 payloads use that same raw-byte fragment callback path.
- **Legacy Win32 / GDK without a fragment handler**: Oversized incoming payloads are not surfaced through the public whole-message callbacks unless a fragment handler is installed.
- **Legacy macOS / iOS and Linux max**: On macOS / iOS and Linux legacy behavior, the provider continues using its configured `32,000,000`-byte maximum; `HCWebSocketSetMaxReceiveBufferSize()` does not become a caller-controlled hard cap there.
- **Deterministic behavior**: Calling HCWebSocketSetOptions(HCWebSocketOptions::None) or any non-legacy compression flag selects deterministic behavior on supported built-in implementations. Fragment callbacks are not supported there.
- **Deterministic inbound limit**: HCWebSocketSetMaxReceiveBufferSize() becomes a hard inbound message-size cap for deterministic behavior. If not set before connect, the deterministic default is `32,000,000` bytes, and oversized messages close the socket with `HCWebSocketCloseStatus::TooLarge`.

To replace the built-in WebSocket implementation entirely, call `HCSetWebSocketFunctions()`.

#### Compression

When compression is requested, built-in compression support is available on Linux, macOS, iOS, Win32, GDK PC, and optionally GDK console when enabled with `HC_ENABLE_GDK_XBOX_WEBSOCKET_COMPRESSION`.

Android manages compression internally and does not expose `HCWebSocketSetOptions()`.

UWP / WinRT does not support built-in compression negotiation.

Compression support can be compiled out entirely by omitting the `HC_ENABLE_WEBSOCKET_COMPRESSION` build flag.
Compression for GDK Console can be enabled with the `HC_ENABLE_GDK_XBOX_WEBSOCKET_COMPRESSION` build flag.

Call `HCWebSocketSetOptions()` on a handle before `HCWebSocketConnectAsync()` to control the built-in WebSocket behavior for that connection. `LegacySemantics` explicitly preserves the existing legacy behavior. `None` selects deterministic behavior without requesting compression. `RequestCompression` selects deterministic behavior and requests `permessage-deflate` compression. Combine `RequestCompression` with `CompressionServerNoContextTakeover` and/or `CompressionClientNoContextTakeover` to request fresh zlib state per message in the corresponding direction. These flags require `RequestCompression`; setting them alone returns `E_INVALIDARG`.

In deterministic behavior, fragment callbacks are not supported and the inbound message-size limit becomes a hard cap. `HCWebSocketSetMaxReceiveBufferSize()` overrides that cap if called before connect; otherwise the deterministic default is `32,000,000` bytes. When `HCWebSocketSetOptions()` is not called, Win32 and GDK remain on legacy fragment-callback behavior, while macOS, iOS, and Linux remain on the provider's configured `32,000,000`-byte maximum.

#### Windows proxy and TLS notes

On Win32 and GDK, `HCWebSocketSetProxyUri()` applies the explicit proxy URI to built-in WebSocket requests. Embedded proxy credentials are passed through but not pre-authenticated.

`HCWebSocketSetProxyDecryptsHttps()` disables TLS server certificate validation for a WebSocket connection. It is a debugging-only setting for use with HTTPS-intercepting proxies (Fiddler, Charles, etc.) and should never be enabled in production. Available on Win32 and GDK. On GDK console, TLS validation is always enforced regardless of this setting.

## Behavior control

* On GDK, XDK ERA, UWP, iOS, and Android, HCHttpCallPerform() will call native platform APIs
* Optionally call HCSetHttpCallPerformFunction() to do your own HTTP handling using HCHttpCallRequestGet*(), HCHttpCallResponseSet*(), and HCSettingsGet*()
* See sample CustomHttpImplWithCurl for an example of how to use this callback to make your own HTTP implementation.
* Optionally call HCSetWebSocketFunctions() to replace the built-in WebSocket implementation with your own connect / send / disconnect callbacks.

## Build customization

If you are building libHttpClient from source, you can provide an hc_settings.props file with specific MSBuild properties to customize how the library gets built. When built, the libHttpClient projects look for an hc_settings.props file in any directory above the the repository root. Currently, the following build customizations are available:
* Defining HCNoWebSockets will exclude WebSocket APIs (and all their dependencies) from the libHttpClient library
* Defining HCNoZlib will exclude compression APIs and prevent libHttpClient from defining Zlib symbols within libHttpClient
* Defining HCExternalOpenSSL will prevent libHttpClient from referencing our private OpenSSL projects. If this is defined, you will need to manually include your own (compatible) version of OpenSSL when linking.
* Setting `HCEnableWebSocketCompression` to `true` or `false` controls whether optional built-in WebSocket compression support is compiled into the Win32 and GDK MSBuild builds. When enabled and the required WinTLS dependency is present, the build defines `HC_ENABLE_WEBSOCKET_COMPRESSION`. This property defaults to `true`.
* Setting `HCEnableGDKXboxWebSocketCompression` to `true` enables that same built-in compression support on GDK Xbox console builds. This property defaults to `false`.
* The Win32 certificate-validation integration tests are compiled only in `Tests\WebSocketCompression\WebSocketCompressionIntegrationTests.Win32.vcxproj`, which defines `HC_ENABLE_WSS_CERT_STORE_TESTS=1`. Running that integration binary may prompt for Windows confirmation when it adds or removes the temporary test certificate from `CurrentUser\Root`, so it is intended for manual/integration use rather than CI. The default `Tests\WebSocketCompression\WebSocketCompressionTests.Win32.vcxproj` intentionally omits that define and remains popup-free.

For Linux CMake builds, `HC_ENABLE_WEBSOCKET_COMPRESSION` is enabled by default. The helper script in `Build\libHttpClient.Linux\libHttpClient_Linux.bash` keeps that default and exposes `-nwc|--no-websocket-compression` as an opt-out, while still accepting `-wc|--websocket-compression` for explicit enablement.

For Apple Xcode builds, the websocket-enabled `libHttpClient_iOS` and `libHttpClient_macOS` targets define `HC_ENABLE_WEBSOCKET_COMPRESSION` and include the vendored `External/zlib` headers by default, so built-in compression support is enabled on iOS and macOS as well.

An example customization file hc_settings.props.example can be found at the root of the repository.

## How to clone repo

This repo contains submodules.  There are two ways to make sure you get submodules.

When initially cloning, make sure you use the "--recursive" option. i.e.:

    git clone --recursive https://github.com/Microsoft/libHttpClient.git

If you already cloned the repo, you can initialize submodules with:

    git submodule sync
    git submodule update --init --recursive

Note that using GitHub's feature to "Download Zip" does not contain the submodules and will not properly build.  Please clone recursively instead.

## Contribute Back!

Is there a feature missing that you'd like to see, or have you found a bug that you have a fix for? Or do you have an idea or just interest in helping out in building the library? Let us know and we'd love to work with you. For a good starting point on where we are headed and feature ideas, take a look at our [requested features and bugs](../../issues).  

Big or small we'd like to take your contributions back to help improve the libHttpClient for game devs.

## Having Trouble?

We'd love to get your review score, whether good or bad, but even more than that, we want to fix your problem. If you submit your issue as a Review, we won't be able to respond to your problem and ask any follow-up questions that may be necessary. The most efficient way to do that is to open a an issue in our [issue tracker](../../issues).  

### Xbox Live GitHub projects
*   [Xbox Live Service API for C++](https://github.com/Microsoft/xbox-live-api)
*   [Xbox Live Samples](https://github.com/Microsoft/xbox-live-samples)
*   [Xbox Live Resiliency Fiddler Plugin](https://github.com/Microsoft/xbox-live-resiliency-fiddler-plugin)
*   [Xbox Live Trace Analyzer](https://github.com/Microsoft/xbox-live-trace-analyzer)
*   [libHttpClient](https://github.com/Microsoft/libHttpClient)

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/). For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

