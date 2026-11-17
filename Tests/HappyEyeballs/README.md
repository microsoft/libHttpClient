# Address-family fallback

Happy Eyeballs selects a working TCP connection before HTTP request data or a
WebSocket upgrade is sent. IPv6 failures therefore do not depend on the HTTP
retry policy. The hostname remains unchanged for Host, TLS SNI and certificate
validation. IPv4 is not forced: IPv6-only and DNS64/NAT64 networks remain valid.

| Provider / platforms | Behavior |
|---|---|
| Curl HTTP (Linux and builds using this provider) | `CURL_IPRESOLVE_WHATEVER` preserves both families; `CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS=250` explicitly configures the race on curl 7.59+. Older curl uses its built-in delay; the pinned curl is 7.81.0. |
| Android HTTP and WebSocket | OkHttp 5.5.1, `fastFallback(true)` on both shared clients. HTTP retains `retryOnConnectionFailure(false)`. Minimum runtime remains API 21. Building requires SDK 37, JDK 17, AGP 9.2.1 / Gradle 9.4.1. |
| websocketpp (Linux, Apple, Windows/GDK compression paths) | Internal TCP race: alternate the resolver's address families, preserving preference and within-family ordering. Launch the next candidate after 250 ms, or immediately on a definitive failure. Keep the first successful socket, close all losers, then let the existing transport perform TLS / proxy CONNECT / upgrade. DNS and TCP retain separate provider deadlines. |
| WinHTTP HTTP and WebSocket (Win32 / GDK) | `WINHTTP_OPTION_IPV6_FAST_FALLBACK` is applied to every session, including HTTP and reopened HTTPS sessions. Windows 10 1903+ supports this option. Unsupported OS versions keep WinHTTP's native sequential fallback; rejecting an optional setting does not fail initialization. Console behavior depends on its supplied WinHTTP implementation. |
| NSURLSession HTTP (macOS / iOS) | Native CFNetwork address selection and Happy Eyeballs. No application DNS override or IPv4-only configuration. |
| XMLHttp / WinRT (UWP and legacy native builds) | The native stack owns connection establishment. These APIs do not expose the WinHTTP session option or a TCP race hook. Their fallback timing depends on the OS. |
| Generic / caller-supplied HTTP or WebSocket callbacks | Connection establishment belongs to the supplied provider; that provider must implement address fallback. |

This does not promise a successful connection on every OS/network. Fallback needs
a usable address and route, a DNS result containing the alternate family (or
platform translation), and enough caller timeout to try it. An IPv6 literal has
no alternate hostname to resolve. A proxy's own outbound address selection is
controlled by the proxy. Certificate and HTTP application errors are not reasons
to replay a request through another address.

References: [WinHTTP option support](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_ipv6_fast_fallback),
[curl Happy Eyeballs](https://curl.se/libcurl/c/CURLOPT_HAPPY_EYEBALLS_TIMEOUT_MS.html),
[OkHttp fast fallback](https://lysine.dev/okhttp/features/connections/#fast-fallback),
[Apple hostname / NAT64 guidance](https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/NetworkingOverview/UnderstandingandPreparingfortheIPv6Transition/UnderstandingandPreparingfortheIPv6Transition.html).

## Regression tests

The standalone tests use real Asio timers with simulated sockets to reproduce a
black-holed IPv6 connect without firewall changes. They cover immediate failure,
stalled connects, family interleaving, either family winning, single-family hosts,
exhaustion, empty DNS results, cancellation before/after a second attempt,
single completion, loser cleanup, multiple I/O threads, and custom-allocator
allocation/release. The shipping provider passes libHttpClient's allocator to the
race and its containers. A real IPv4 loopback
listener also checks fallback from a refused IPv6 endpoint. An object-only target
instantiates the OpenSSL transport so its different socket wrapper is checked
even on Windows, whose shipping TLS WebSocket transport uses WinTLS.

```sh
cmake -S Tests/HappyEyeballs -B Int/HappyEyeballsTests
cmake --build Int/HappyEyeballsTests --config Debug
ctest --test-dir Int/HappyEyeballsTests -C Debug --output-on-failure
```

Repeat for Release. The tests are included in the Linux/macOS `unit` CTest label
and the Windows x64 CI legs. They require loopback socket access.

`Tests/E2E` adds HTTP and WebSocket hostname tests against its IPv4-only server,
including the compression transport. `localhost` is intentionally used to
exercise the real resolver, but may prefer IPv4 on some hosts; deterministic
stalled IPv6 coverage comes from the standalone tests.

Android's `HappyEyeballsTest` reads the production shared-client policy, injects
IPv6-first DNS and a socket that stalls IPv6, and verifies HTTP POST reaches the
IPv4-only MockWebServer once with HTTP retries disabled. The WebSocket test
verifies fallback before upgrade and cancellation of the losing IPv6 socket.
Run through `connectedDebugAndroidTest` on an API 21+ emulator/device. The same
class can run with host JUnit against OkHttp's JVM artifact and an Android API
jar; that validates TCP behavior and Java compatibility, not Android packaging,
TLS platform integration, or device-specific network behavior.
