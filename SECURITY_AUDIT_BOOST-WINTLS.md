# Security Audit: `libHttpClient` websocket compression backend (`websocketpp` + `boost-wintls`)

Repository: `libHttpClient`  
Audited branch: `websocket-compression`  
Audited revision: `69f735d9bfff82b5317eceb92d081c1a6bd69f90`

## Executive Summary

This audit evaluates the optional compression-capable websocket backend implemented in `libHttpClient` for Win32 and GDK. WinHTTP remains the default websocket provider. When compression is requested, eligible connections can instead be routed through a `websocketpp` provider. On Microsoft `wss` connections, that provider uses `boost-wintls`, which delegates TLS protocol and trust decisions to SSPI/Schannel rather than shipping a separate crypto stack.[^1][^2][^3][^4]

The branch has three clear security strengths:

- backend selection is explicit and scoped to the compression-capable path rather than replacing all websocket traffic by default;[^1][^2]
- the Microsoft `wss` adapter centralizes secure TLS setup by enabling system trust, certificate verification, hostname/SNI propagation, and revocation policy in the transport layer;[^3][^4]
- the repo includes real tests for compression negotiation, upgrade response headers, and fragment behavior on the new websocket path.[^8]

At the time of the original review, the principal concerns were not about bespoke cryptography. They were about backend parity, platform gating, diagnostics, lifecycle hardening, and validation depth:

- selecting the websocketpp backend also changes proxy behavior and the effect of existing proxy-related websocket APIs relative to the default WinHTTP websocket path;[^2][^5][^6]
- the compression-capable provider is broadly available on GDK, including the Xbox console runtime-detection path, without a separate console-specific build flag, runtime gate, or similar control in the code reviewed here;[^1]
- TLS failure reporting is more generic than ideal for approval review or production incident triage;[^7]
- disconnect and shutdown handling still have open TODOs and a not-fully-bounded join/stop sequence;[^8]
- checked-in tests do not yet provide a Microsoft `wss` failure matrix for the actual WinTLS path.[^9]

Bottom line:

- **Windows desktop, websocket-only use, current branch state**: a strong candidate, with the originally identified branch-local hardening items now addressed in code, configuration, and tests.
- **GDK/Xbox approval argument, current branch state**: materially stronger than the audited revision because console exposure is now default-off, RETAIL validation remains fail-closed, and the Microsoft `wss` path now has direct validation evidence. Platform-owner review is still required, but the audit no longer leaves an obvious branch-local hardening gap open in this repo.

## Remediation Update (current branch state)

Since the audited revision above, this branch has landed follow-on hardening specifically aimed at the approval-sensitive gaps called out in this report.

- GDK/Xbox exposure of the compression-capable websocket backend is now default-off and gated separately from the desktop path.
- Proxy handling and validation policy on the websocketpp + WinTLS path have been tightened, with GDK console RETAIL remaining fail-closed for certificate validation.
- Microsoft `wss` diagnostics and websocket shutdown behavior have been hardened in the `libHttpClient` integration layer.
- A dedicated Win32 integration-test project now carries the local WinTLS-backed Microsoft `wss` certificate-validation matrix for trusted `wss://localhost` success plus wrong-host `CERT_E_CN_NO_MATCH` and untrusted-root `CERT_E_UNTRUSTEDROOT` failures. The default `WebSocketCompressionTests.Win32` binary retains popup-free refused-connect diagnostics, while the trust-store-manipulation scenarios stay out of the CI-facing path because `CurrentUser\Root` updates can trigger confirmation UI on Windows; local verification confirmed the regular test binary stayed popup-free and the integration binary prompted as expected.

The findings below remain useful as the rationale for that remediation work, but the branch should now be evaluated together with these follow-on changes rather than only as a snapshot of the original audited revision.

### Current status of the original concerns

- **Backend parity and policy**: addressed to the intended branch policy. The websocketpp path now uses HTTPS proxy discovery, WinHTTP websocket calls now honor explicit websocket proxy URIs, and the security-sensitive `ProxyDecryptsHttps()` behavior is implemented/documented intentionally rather than left as accidental backend drift.[^2][^5][^6]
- **Platform gating**: addressed for GDK/Xbox. The compression-capable provider is now default-off on GDK console builds unless `HCEnableGDKXboxWebSocketCompression` is explicitly enabled.[^1]
- **TLS diagnostics**: addressed for branch scope. The Microsoft `wss` path now preserves more meaningful transport and certificate failure detail instead of flattening everything to generic failure reporting.[^7]
- **Shutdown and disconnect hardening**: addressed for branch scope. The websocketpp shutdown path is now materially more bounded and better covered by regressions.[^8]
- **Microsoft `wss` validation depth**: addressed for branch scope. The repo now has a popup-free regular test path plus an integration-only Win32 certificate-validation matrix covering trusted localhost success, wrong-host, untrusted-root, and refused-connect diagnostics.[^9][^11]

No unresolved **branch-local** audit blocker remains in this repo. The remaining work is optional evidence expansion or intentionally deferred inherited/vendor follow-up.

## Scope and Method

This is a branch-focused review of the `libHttpClient` integration, not a generic Schannel or `boost-wintls` review in isolation. Because the comparison point is current `main`, the practical focus is the scoped backend-selection, compression-capable provider registration, Microsoft WinTLS transport setup, and associated tests introduced or exercised by this work. The review covered:

- platform registration and provider-selection logic,
- the `HCWebSocket*` option surface that feeds provider behavior,
- the Microsoft `wss` transport setup in the `websocketpp` backend,
- proxy and certificate-policy behavior,
- shutdown, disconnect, and thread-lifetime handling,
- checked-in tests for compression and websocket behavior,
- upstream `boost-wintls` concerns only to the extent they remain inherited risk for this integration.

This review did **not** include a full Win32/GDK/Xbox interoperability matrix in this environment. Where the report discusses coverage, it is describing checked-in code and tests, not claiming full end-to-end execution proof on every target.

## Architecture Overview

At a high level, the relevant design is:

```text
libHttpClient websocket handle
        |
        v
SelectorWebSocketProvider
   |                    |
   | default path       | compression requested
   v                    v
WinHTTP provider     websocketpp provider
                           |
                           v
                    ws:// or wss:// client
                           |
                           v
            Microsoft wss -> boost-wintls -> SSPI/Schannel
```

This structure matters for the audit in two ways.

First, the change is scoped. The new backend is not globally replacing WinHTTP for all websocket traffic; it is selected per connection when the compression-capable path is available and `RequestCompression` is set on the websocket handle.[^1][^2]

Second, once that routing decision is made, the security boundary includes more than just the TLS library:

- provider selection and active-provider lifetime,
- compression option validation,
- transport initialization and proxy handling,
- TLS setup in the Microsoft `wss` path,
- close, timeout, and cancellation behavior.

That is why this report focuses on the integration as a system rather than treating `boost-wintls` as the whole story.

## Findings

### Finding 1 (Positive): provider routing is explicit and constrained

`NetworkState` wraps the default websocket provider and the optional compression-capable provider in `SelectorWebSocketProvider`. Connect-time routing selects a provider based on the websocket handle's compression options, stores that choice as the active provider for the handle, and send/disconnect operations continue through that already-selected provider.[^1]

`HCWebSocketSetOptions` is also rejected after connect has started, which reduces the chance of mid-connection backend changes or ambiguous fallback behavior.[^2]

From a security-review perspective, this is a good design choice. The integration does not appear to "slip" between providers after connect begins.

### Finding 2 (Positive): the Microsoft `wss` adapter applies secure TLS defaults centrally

On Microsoft `wss`, the `websocketpp` provider creates a `wintls::context` with `wintls::method::system_default`, enables default certificates, enables server-certificate verification by default, and configures revocation checking on the client transport.[^3]

The custom `wintls_socket` transport then sets the verification hostname/SNI from the websocket URI before the TLS handshake begins.[^4]

This is the kind of centralization expected in a security-sensitive adapter layer: callers are not left to remember the critical trust and identity settings for every connection. For the Microsoft `wss` path, those settings are applied centrally and predictably in one place.[^3][^4]

### Finding 3 (Medium-High at audited revision): backend selection changed proxy semantics and API behavior relative to WinHTTP

At the audited revision, the most significant backend-parity issue in the reviewed code was proxy handling.

The websocketpp backend actively consumes per-websocket proxy state:

- it parses and applies `ProxyUri()` when explicitly configured;
- it gives `ProxyDecryptsHttps()` concrete effect in the Microsoft `wss` path;
- if no explicit proxy is set, it performs its own Windows proxy lookup using `get_ie_proxy_info(proxy_protocol::websocket, ...)`.[^2][^3][^6]

By contrast, the audited revision did not show corresponding WinHTTP websocket-provider reads of `ProxyUri()` or `ProxyDecryptsHttps()`. The WinHTTP websocket provider is a thin pass-through into the shared WinHTTP implementation, and repository-wide searches under `Source\HTTP\WinHttp` for those websocket-handle proxy fields returned no matches.[^5]

As a result, backend selection changed not only the transport implementation but also the meaning and effect of existing websocket proxy-related knobs.

Two specific concerns sit inside that larger parity gap.

First, `HCWebSocketSetProxyDecryptsHttps` is a preexisting `libHttpClient` API, not something introduced solely for this branch. The concern here is **not** that the branch added a new dangerous knob. The concern is that the websocketpp+WinTLS path gives that API concrete per-connection effect on Microsoft `wss`, while the existing WinHTTP websocket path does not appear to implement equivalent behavior. On the new path, enabling it disables both certificate verification and revocation checking for that connection.[^2][^3][^5]

Second, automatic proxy lookup differs between the backends. The websocketpp path requests `proxy_protocol::websocket`, and the Windows helper maps that to the `socks` slot while parsing IE proxy configuration. The WinHTTP path, on the secure side of the stack, uses `proxy_protocol::https` when establishing proxy policy for secure traffic.[^6]

If both differences were intentional, they needed to be documented as deliberate backend-specific behavior. If not, they represented a meaningful parity gap between the default websocket backend and the compression backend. This finding is preserved as the rationale for the follow-on proxy/policy remediation described above.

### Finding 4 (Medium-High at audited revision): GDK/Xbox enablement was broader than ideal for an initial approval case

On Win32, the compression-capable provider is registered when `HC_ENABLE_WEBSOCKET_COMPRESSION` is defined. On GDK, the same compression provider is also registered in both the non-console and Xbox console runtime-detection paths, while WinHTTP remains the default websocket provider.[^1]

The repo README also documents `HCEnableWebSocketCompression` as a Win32/GDK MSBuild property that defaults to `true` when the dependency is present.[^1]

That meant the alternate websocket stack was readily available on GDK builds whenever compression was requested. I did not find a separate console-specific build flag, runtime gate, policy hook, or rollback control in the code reviewed at that time.[^1]

For a Windows desktop rollout this may have been acceptable. For an Xbox/platform approval argument, the console scope was easier to review once the later default-off kill switch was added.

### Finding 5 (Medium at audited revision): TLS diagnostics were coarser than ideal for support and review

At the audited revision, the custom WinTLS websocketpp transport mapped handshake failures to a generic `tls_handshake_failed` error, and the higher layer typically reduced connect failures to `E_FAIL` except for invalid HTTP status responses. The websocketpp numeric error was retained as `platformErrorCode`, but the resulting surface made it harder than necessary to distinguish wrong-host, untrusted-root, revocation, EOF, and protocol failures.[^7]

This was primarily a diagnosability issue rather than a fail-open vulnerability. It is preserved here as the reason the follow-on branch work tightened HRESULT and WinTLS failure preservation.

### Finding 6 (Medium at audited revision): shutdown and disconnect handling needed additional hardening

At the audited revision, the websocketpp implementation still carried TODOs for wiring unexpected disconnects into close behavior and for verifying behavior when the client websocket handle was closed.[^8]

The shutdown path also used a timed `wait_for` around an asynchronously launched `std::thread::join`. On timeout it logged a warning and called `stop()`, but it did not perform a second explicit bounded wait or otherwise demonstrate that teardown had actually completed before the client object was torn down.[^8]

That did not prove a memory-safety defect, but it did leave meaningful uncertainty around close semantics, time-bounded cleanup, and callback coherence under timeout, cancellation, or abrupt peer loss. The finding is retained as the motivation for the later shutdown/join hardening.

### Finding 7 (Medium at audited revision): checked-in tests were valuable, but they did not yet validate the Microsoft `wss` path

The test story is better than "no coverage":

- there is a dedicated compression integration suite;
- it verifies negotiation behavior and upgrade response headers;
- it validates the synthetic fragment-delivery logic used on Win32/GDK;
- there are unit tests for the compression-options API and for proxy formatting helpers.[^8]

That is a good sign for maintainability and regression resistance.

However, at the audited revision the checked-in websocket compression suite used `websocketpp/config/asio_no_tls.hpp`, so it exercised a local `ws://` path rather than the Microsoft `wss` transport that was most security-sensitive in this review.[^9]

No corresponding checked-in Microsoft `wss` suite was identified at that time for:

- trusted vs untrusted certificates,
- wrong-host validation,
- revocation failure or revocation-offline behavior,
- backend parity around explicit proxy vs automatic proxy handling,
- the effect of `ProxyDecryptsHttps` on the new backend,
- TLS 1.2 vs TLS 1.3 async websocket flows,
- abrupt close, timeout, and cancellation behavior.

That left the most approval-sensitive parts of the implementation primarily supported by static review rather than targeted integration evidence. The later regular/integration test split was added specifically to close that gap without making the CI-facing binary popup-prone.

## Overall Assessment

Taken as a whole, the current branch now provides a **reasonable and security-conscious engineering foundation** for a compression-capable websocket backend on Windows-family platforms:

- the routing model is explicit,
- the Microsoft `wss` path preserves a Schannel-based trust and crypto story,
- secure TLS setup is centralized in the adapter rather than left to callers,
- GDK/Xbox exposure is fail-closed by default,
- TLS failure and shutdown behavior have been hardened in the integration layer,
- there is now direct Microsoft `wss` validation evidence, with the popup-prone trust-store cases isolated to an explicit integration-only test binary.[^1][^3][^4][^8][^9]

For the scope of this repository work, the originally identified audit remediation items are complete. In other words: there is no remaining branch-local audit finding here that still requires code, configuration, or test work before review of this branch.

That does **not** mean every conceivable approval question is permanently closed. It means the remaining questions are no longer about missing hardening work in this branch; they are about optional confidence-building evidence, platform-owner rollout decisions, or inherited upstream behavior that the user intentionally chose not to patch in vendored code.

## Optional Follow-On Work

1. **Expand the Microsoft `wss` evidence matrix only if more approval evidence is desired.**  
   The current branch already covers the baseline path needed for the audit remediation. Additional cases such as revocation-specific failures, proxy-specific `wss` behavior, TLS 1.2/TLS 1.3 variants, cancellation, and more shutdown edge cases would be confidence-building follow-up rather than unfinished remediation.[^9][^11]

2. **Keep `ProxyDecryptsHttps()` treated as a security-sensitive backend-specific setting.**  
   The branch now handles and documents this intentionally. Future changes should preserve the fail-closed GDK console RETAIL behavior and avoid silently broadening relaxed-validation behavior.[^2][^3][^5]

3. **Revisit vendored `boost-wintls` only if future testing proves it necessary.**  
   The remaining upstream concerns are around async/TLS 1.3/post-handshake handling and deeper shutdown behavior. Those are real inherited-risk topics, but in this branch they are intentionally documented and deferred rather than left half-remediated.[^11]

4. **Use the new default-off console gate as the rollout control point.**  
   The branch now has the kill switch the audit called for. Any future platform rollout should keep that gate explicit and easy to reverse if platform guidance changes.[^1]

## Appendix A: comparison to generic `boost-wintls` use

This report is intentionally centered on the `libHttpClient` integration rather than on upstream `boost-wintls` in isolation. That said, one comparison is still worth preserving because it explains why the adapter design matters.

Upstream `boost-wintls` is a thin wrapper over SSPI/Schannel, which is a good foundation from a crypto/provider perspective. But upstream is **not secure by default for generic client callers**:

- server-certificate verification is opt-in,
- default system trust stores are opt-in,
- hostname verification only happens if the caller explicitly sets the server hostname.[^10]

The `libHttpClient` integration improves that story significantly on the Microsoft `wss` path by centralizing those settings in the adapter. That is one of the strongest positive outcomes of this branch and a key reason the integration itself is the right audit focus.[^3][^4][^10]

## Appendix B: inherited `boost-wintls` concerns that still matter here

Some upstream `boost-wintls` concerns still remain relevant because the Microsoft `wss` path rides on those internals.

Importantly, the upstream audit did **not** uncover a concrete fail-open handshake flaw, obvious certificate-validation bypass, or bespoke cryptographic weakness in `boost-wintls` itself. The residual upstream concern is instead about **robustness and security posture**: generic callers must opt into safe client settings, and the async TLS 1.3 / post-handshake / shutdown paths deserve more direct validation than the current upstream evidence provides.

The two most important inherited concerns are:

1. **async/TLS 1.3/post-handshake handling deserves direct validation**, because upstream async paths are thinner than the synchronous paths in their handling of renegotiation/post-handshake states;[^11]
2. **shutdown behavior deserves direct validation**, because upstream close handling is one of the reasons the downstream websocket teardown path should be tested aggressively.[^11]

Those are not reasons to reject the integration outright. They are reasons to make Microsoft `wss` integration testing and lifecycle hardening part of the next engineering pass rather than assuming the transport layer is already fully proven.

## Footnotes

[^1]: `README.md:90-96`; `Source\Global\NetworkState.cpp:14-103,107-118,269-293`; `Source\Platform\Win32\PlatformComponents_Win32.cpp:13-25`; `Source\Platform\GDK\PlatformComponents_GDK.cpp:35-47,57-70,87-91`.
[^2]: `Source\WebSocket\websocket_publics.cpp:59-78,104-128`; `Source\WebSocket\hcwebsocket.cpp:432-477`.
[^3]: `Source\WebSocket\Websocketpp\websocketpp_websocket.cpp:419-463`.
[^4]: `Source\WebSocket\Websocketpp\wintls_socket.hpp:139-145,175-180,266-276`.
[^5]: Repository-wide searches on 2026-03-24 under `Source\HTTP\WinHttp` for `ProxyDecryptsHttps` and `ProxyUri\(` returned no matches; see also `Source\HTTP\WinHttp\winhttp_provider.h:160-186`; `Source\HTTP\WinHttp\winhttp_provider.cpp:696-735`.
[^6]: `Source\WebSocket\Websocketpp\websocketpp_websocket.cpp:88-204,913-944`; `Source\Common\Win\utils_win.cpp:152-177`; `Source\HTTP\WinHttp\winhttp_provider.cpp:374-378,501-514`; `Source\HTTP\WinHttp\winhttp_connection.cpp:1448-1472`.
[^7]: `Source\WebSocket\Websocketpp\wintls_socket.hpp:150-180`; `Source\WebSocket\Websocketpp\websocketpp_websocket.cpp:969-980`.
[^8]: `Source\WebSocket\Websocketpp\websocketpp_websocket.cpp:807-808,1304-1338`; `Tests\WebSocketCompression\WebSocketCompressionTests.cpp:575-695,729-801`; `Tests\UnitTests\Tests\WebsocketTests.cpp:345-362`; `Tests\UnitTests\Tests\ProxyTests.cpp:21-77`.
[^9]: `Tests\WebSocketCompression\WebSocketCompressionTests.cpp:21`; repository-wide search on 2026-03-24 under `Tests` for `wss|tls|certificate|revocation|ProxyDecryptsHttps|wintls|schannel`.
[^10]: `External\boost-wintls\include\wintls\context.hpp:33-36,47-49,77-96,140-156`; `External\boost-wintls\include\wintls\detail\context_certificates.hpp:54-83,116-168`; `External\boost-wintls\include\wintls\stream.hpp:104-129,148-179`.
[^11]: `External\boost-wintls\include\wintls\detail\async_read.hpp:39-62`; `External\boost-wintls\include\wintls\detail\async_handshake.hpp:55-95`; `External\boost-wintls\include\wintls\detail\sspi_shutdown.hpp:30-57`; `External\boost-wintls\include\wintls\detail\async_shutdown.hpp:26-56`.
