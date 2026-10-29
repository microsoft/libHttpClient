# libHttpClient end-to-end tests

End-to-end (E2E) tests that drive the **real platform providers** against a local loopback
server. Unlike the unit tests (`Tests/UnitTests`, Generic platform, no network), these exercise
the shipping network stacks. Nothing leaves `127.0.0.1`, so they run on CI agents without
internet access. One set of test sources builds for every platform:

| Platform | HTTP provider | WebSocket provider | Build | CI |
|---|---|---|---|---|
| Win32 x64 | WinHTTP | WinHTTP (websocketpp when compression is requested) | `libHttpClient.E2E.Win32.vcxproj` | `vs2022-build.yml`, x64 Debug + Release |
| Linux | curl | websocketpp | `libHttpClient.E2E` in `Build/libHttpClient.Linux` (`HC_BUILD_E2E_TESTS`, opt-in: `libHttpClient_Linux.bash -t`), ctest label `e2e` | `linux-build.yml`, Debug + Release + ASan/UBSan |
| macOS | NSURLSession | websocketpp | `libHttpClient.E2E` in `Build/libHttpClient.UnitTest.CMake` (`-DHC_BUILD_E2E_TESTS=ON`, opt-in) | `macos-build.yml`, Debug + Release |

| Piece | Location |
|---|---|
| Test server (Python 3.8+, **standard library only**) | `server/lhc_test_server.py` |
| TLS certificate | none in the repo: the server generates a throwaway one at startup (see below) |
| Runner + framework | `E2ETestMain.cpp`, `E2ETestFramework.h`, `E2EHelpers.*`, `TestServer.*` (portable) |
| OS-specific harness code (processes, pipes, env, sockets, crash handler) | `E2EPlatform.h/.cpp` (`_WIN32` / POSIX) |
| Tests | `HttpE2ETests.cpp`, `WebSocketE2ETests.cpp` |
| CMake target definition (shared by Linux and macOS) | `Build/libHttpClient.CMake/E2ETests.cmake` |

## Running locally

Prerequisites: Python 3 on `PATH` (`python` / `py -3` on Windows, `python3` / `python`
elsewhere; no pip packages), and the `openssl` CLI for the HTTPS tests (on `PATH`, or from Git for
Windows; without it they skip).

**Windows:** build `libHttpClient.vs2022.sln` for **x64** (the project is x64-only in the
solution), or just the project:
`msbuild Tests\E2E\libHttpClient.E2E.Win32.vcxproj /p:Platform=x64 /p:Configuration=Debug`,
then run `Out\x64\Debug\libHttpClient.E2E.Win32\libHttpClient.E2E.Win32.exe`.

**Linux:** build OpenSSL and curl first (`Build/libHttpClient.Linux/openssl_Linux.bash -c Debug`,
`curl_Linux.bash -c Debug`), then `Build/libHttpClient.Linux/libHttpClient_Linux.bash -c Debug -nc -ns -st -t`
and `cd Int/CMake/libHttpClient.Linux && ctest --output-on-failure -L e2e` (or run
`Out/x64/Debug/libHttpClient.Linux/libHttpClient.E2E` directly).

**macOS:** see the `HC_BUILD_E2E_TESTS` block in `Build/libHttpClient.UnitTest.CMake/CMakeLists.txt`.

The exe starts the server itself, reads the ports it prints, runs the tests and stops it. If the
exe dies without stopping it, the server exits anyway (Windows: kill-on-close job object; POSIX:
the server's `--parent-pid` watchdog). The exit code is the number of failed tests; skips don't
count. On POSIX a fatal signal prints the running test and a backtrace before the process dies.
Options:

| Option | Meaning |
|---|---|
| `--filter <substring>` | Run tests whose name contains the substring (repeatable) |
| `--exact <name>` | Run exactly one test |
| `--list` | List test names and exit |
| `--ports <http>,<http2>,<https>` (or env `LHC_E2E_PORTS`) | Use an already-running server instead of starting one (`https` = 0 skips the HTTPS tests) |
| `--server-script <path>` (or env `LHC_E2E_SERVER_SCRIPT`) | Server script location (default: found by walking up from the exe) |
| `--python <exe>` (or env `LHC_E2E_PYTHON`) | Python interpreter to use |
| `--test-timeout <seconds>` | Per-test watchdog (default 120) |
| `--trace` | Send libHttpClient verbose tracing to stderr |
| `--server-verbose` | Make the server log every request to stderr |

To debug against a long-running server:

```
python Tests\E2E\server\lhc_test_server.py --verbose
{"http": 50001, "http2": 50002, "https": 50003, "https_error": null, "pid": 1234}
libHttpClient.E2E.Win32.exe --ports 50001,50002,50003 --filter ws_
```

## Server contract

`lhc_test_server.py [--timeout <seconds>] [--verbose] [--openssl <exe>] [--no-https]` binds three
listeners on `127.0.0.1` with OS-assigned ports, prints **one JSON line** to stdout when ready,
then serves until killed (or `--timeout` elapses). It never reads stdin.

```
{"http": <port>, "http2": <port>, "https": <port> | null, "https_error": null | "<why>", "pid": <pid>}
```

- `http`: plain HTTP, and WebSockets (`ws://`) via `Upgrade` on the same port.
- `http2`: a second plain-HTTP origin, the target of cross-origin redirects.
- `https`: TLS with a throwaway self-signed certificate for `127.0.0.1` (SAN `IP:127.0.0.1`,
  `DNS:localhost`, valid 2 days). The server creates it at startup with the **`openssl` CLI** in a
  temp directory, loads it, and deletes the files immediately, so no key material is ever
  committed or left on disk. It looks for `openssl` on `PATH`, then in a Git for Windows install
  (`<git>\usr\bin\openssl.exe` or `<git>\mingw64\bin\openssl.exe`, found from `git` on `PATH`,
  `git --exec-path` or `%ProgramFiles%\Git`); hosted Windows agents have Git, so it's there. Use
  `--openssl <exe>` to pick one. If it can't make a certificate, the ready line has
  `"https": null` and `"https_error": "<why>"`, the runner prints a warning, and the HTTPS tests
  **skip** with that reason instead of failing. `--no-https` simulates this.

The certificate is never trusted anywhere; `https_self_signed_rejected_by_default` relies on
WinHTTP rejecting it, and `https_ssl_validation_disabled` turns validation off with
`HCHttpCallRequestSetSSLValidation(call, false)`.

JSON responses are canonical (`sort_keys=True`, no spaces, UTF-8), so tests can find
`"key":value` pairs with `e2e::JsonField`. Response bodies from `/bytes` and `/stream` are the
deterministic pattern *byte i = i % 251* (`e2e::IsPattern`).

### HTTP routes (all methods)

| Route | Behaviour |
|---|---|
| `/echo` | 200 JSON: `method`, `path`, `port`, `server` (`http`/`http2`/`https`), `headers` (lower-cased names; values decoded as UTF-8 if valid, else ISO-8859-1), `non_ascii_header_bytes` (hex of raw bytes for non-ASCII header values), `body` (if ≤ 4 KB), `body_length`, `body_crc32` (after gunzipping a `Content-Encoding: gzip` request), `wire_body_length`, `content_encoding`. Response headers include `X-Lhc-Echo: 1` and `X-Lhc-Multi` sent twice (`a`, `b`). Accepts `Content-Length` and chunked request bodies |
| `/status/{n}` | Status `n` with body `status n` |
| `/retry-after/{n}?status=503&after=1&id=X` | The first `n` requests for `id` return `status` with `Retry-After: after` and body `attempt k`; then 200 `{"attempts":k}` |
| `/retry-count?id=X` | `{"count":k}`: requests `/retry-after` has seen for `id` |
| `/delay/{ms}` | Sleeps, then 200 `delayed {ms}` |
| `/bytes/{n}?chunk=&delay_ms=` | `n` pattern bytes with `Content-Length`; optionally written `chunk` bytes at a time with `delay_ms` between writes |
| `/stream/{n}?chunk=4096&delay_ms=` | `n` pattern bytes with `Transfer-Encoding: chunked` |
| `/gzip` | A gzip-compressed text body with `Content-Encoding: gzip`, `X-Lhc-Plain-Length` and `X-Lhc-Plain-Crc32` |
| `/redirect/same?code=302` | Redirect to `http://127.0.0.1:<http>/echo` |
| `/redirect/cross?code=302` | Redirect to `http://127.0.0.1:<http2>/echo` (a different origin) |
| `/concurrency/{ms}?group=G` | Holds the request for `ms` while counting concurrent requests in `G` |
| `/concurrency-stats?group=G` | `{"active":…,"max":…,"total":…}` for `G` |
| `/ws-log?id=X` | What the server saw on WebSocket `?id=X`: `connected`, `deflate`, `offered_extensions`, `client_close_code`, `closed`, `stats` |
| anything else | 404 JSON |

### WebSocket routes (`ws://127.0.0.1:<http>/…`)

| Route | Behaviour |
|---|---|
| `/ws/echo?id=X&ping_ms=N` | Echoes text and binary messages (reassembling fragmented ones). Negotiates **permessage-deflate** when offered (honouring `server_no_context_takeover` / `client_no_context_takeover`), echoes the first `Sec-WebSocket-Protocol`, adds `X-Lhc-Ws: 1` to the 101. With `ping_ms`, sends a ping every N ms. Answers pings, echoes the client's close code |
| `/ws/close/{code}?after_ms=N` | Accepts, waits `after_ms`, then closes with `code` |
| `/ws/stall?after_ms=N` | Accepts, never reads, then after `after_ms` (default 1000) resets the TCP connection, so client sends stay in flight (issue #1057) |
| `/ws/bad-upgrade` | Replies 101 with an invalid `Sec-WebSocket-Accept`, then holds the socket open (issue #909) |

`/ws/echo`, `/ws/close` and `/ws/stall` also take `upgrade_delay_ms=N`: the server waits N ms
before sending the 101, so a test can act while the client's connect is still in flight (issue #1058).

Text commands on `/ws/echo` (not echoed):

| Message | Effect |
|---|---|
| `__stats__` | Replies with JSON: `deflate`, `messages_received`, `compressed_messages_received`, `fragmented_messages_received`, `pings_received`, `pongs_received`, `pings_sent`, `request_headers` |
| `__ping__` | Server sends a ping |
| `__cmd:binary:<n>` | Server sends an `n`-byte pattern binary message |
| `__cmd:text:<n>` | Server sends an `n`-byte text message |
| `__cmd:fragmented:<frames>,<n>` | Server sends an `n`-byte text message split into `frames` frames |
| `__cmd:close:<code>` | Server closes with `code` |

## Writing a test

Add an `E2E_TEST(name) { … }` to `HttpE2ETests.cpp` or `WebSocketE2ETests.cpp` (or a new `.cpp`
in the project). The runner calls `HCInitialize` before the test and `HCCleanupAsync` after it,
and fails the test if cleanup doesn't complete within 20 s, so a test must leave nothing running.

```cpp
E2E_TEST(http_example)
{
    Call call;                                     // RAII HCCallHandle
    call.Url("POST", HttpUrl("/echo")).NoRetry().Body("hello");
    HRESULT hr = call.Perform();                   // waits, with a timeout
    E2E_CHECK_MSG(SUCCEEDED(hr) && call.Status() == 200, "%s", call.Describe().c_str());
    E2E_CHECK_EQ(std::string{ "hello" }, JsonFieldOr(call.ResponseString(), "body"));
}
```

- Helpers (`E2EHelpers.h`): `Call` (HTTP), `WebSocket` (records texts, binaries, fragments and
  the close status; waits are bounded), `FetchString`, `HttpUrl` / `Http2Url` / `HttpsUrl` /
  `WsUrl`, `UniqueId` (to key per-test server state), `PatternBytes` / `IsPattern` / `Crc32`.
- Assertions (`E2ETestFramework.h`): `E2E_CHECK`, `E2E_CHECK_MSG`, `E2E_CHECK_HR`, `E2E_CHECK_EQ`.
  `Info(...)` prints an indented note; `Skip(reason)` skips.
- `E2E_TEST_SELF_INIT(name)`: the test calls `HCInitialize` / `HCCleanupAsync` itself (e.g. to
  set something before init, or to test cleanup).
- `E2E_TEST_ISOLATED(name)`: runs the test in a child process (self-init). Use it when a known
  bug can leave libHttpClient unusable, e.g. `HCCleanupAsync` never completing; mark that case
  with `PoisonProcess(...)` + `Skip(...)` and a TODO naming the bug, so the rest of the suite
  still runs.
- Never wait unboundedly: use `WaitAsync(async, timeout)`, allocate `XAsyncBlock`s on the heap
  and leak them (after `PoisonProcess`) if they don't complete, so a hang becomes a failure
  rather than a crash.
- Keep tests fast (the whole suite runs in well under a minute) and independent of order.
- Portability: no OS APIs in tests; use `platform::` (`E2EPlatform.h`) for sleeps, sockets, etc.
  Where providers legitimately differ, branch on `E2E_HTTP_WINHTTP` / `E2E_HTTP_CURL` /
  `E2E_HTTP_APPLE` or `HC_PLATFORM` and state each provider's expectation (don't weaken an
  existing one), or `Skip` with the reason. APIs that only exist on some platforms
  (`HCHttpCallRequestSetSSLValidation`, the WebSocket fragment callback) need `#if` guards.

## Per-provider expectations

| Test | WinHTTP (Win32) | curl / websocketpp (Linux) |
|---|---|---|
| `http_redirect_*` | Redirects are followed | The curl provider doesn't set `CURLOPT_FOLLOWLOCATION`: the 302 and its `Location` are returned to the caller (asserted) |
| `http_progress_callbacks` | First report is > 0 | curl's first report is 0 (before any bytes move) |
| `http_request_header_outside_latin1` | Header sent as ISO-8859-1; above U+00FF → `E_INVALIDARG` | Header sent as raw UTF-8 |
| `https_ssl_validation_disabled` | Runs | Skipped: `HCHttpCallRequestSetSSLValidation` is Windows-only |
| `wss_self_signed_rejected_by_default`, `wss_ssl_validation_disabled` | Run; the second covers the WinHTTP WebSocket and, when compression is compiled in, the websocketpp + wintls client | Not built: `HCWebSocketSetSSLValidation` is Windows-only |
| `http_global_request_limit_queues` | Runs | Skipped: `HCSettingsSetGlobalRequestLimit` is `E_NOTIMPL` off WinHTTP (by design) |
| `ws_inflight_send_completes_on_disconnect`, `winhttp_provider_teardown_survives_allocation_failure` | Run | Not built: they cover the WinHTTP provider (#1057, #1054) |
| `ws_legacy_oversized_message_fragments` | Fragment callback | No fragment API; the message arrives whole (legacy 32 MB cap) |
| `ws_deterministic_too_large_closes`, `ws_compression_negotiated` | Skipped if `HCWebSocketSetOptions` returns `E_NOT_SUPPORTED` (compression not compiled in, e.g. the first CI build of a fresh clone; #1034) | Compression test skipped likewise when built with `HC_ENABLE_WEBSOCKET_COMPRESSION=OFF` |

NSURLSession (macOS) differs from WinHTTP in two more places, besides the two Windows-only skips above:

- `http_redirect_same_origin_keeps_authorization`: redirects are followed, but NSURLSession treats
  `Authorization` as a reserved header and drops it on **every** redirect, same-origin included.
  The test records that instead of asserting the header is kept. This is a known gap for callers
  that rely on a redirect keeping their token.
- `http_progress_callbacks`: on loopback NSURLSession can send the whole 1 MB upload in one write
  before its first `didSendBodyData`, so the first upload report may already be 100%. The test
  requires the last upload report to equal the total instead. Download progress must still start
  partial.

## Known issues recorded by the suite

- `ws_bad_upgrade_fails_and_cleanup_completes` is **skipped** while `HCCleanupAsync` hangs after
  a rejected WebSocket upgrade (#909, fixed by PR #1033).
- `ws_server_close_immediately_after_upgrade` is **skipped** when a server close that races the
  connect completion leaves the socket in `NetworkState::m_connectedWebSockets` and
  `HCCleanupAsync` never completes. It depends on timing, so the test passes on runs where the
  close loses the race.
- `http_redirect_cross_origin` only records whether `Authorization` is forwarded across origins
  (TODO: assert it's stripped once PR #1032 is in).
- `http_connection_refused` doesn't check the platform error message (TODO once PR #1031 is in).
- `http_request_header_outside_latin1`: WinHTTP sends header values as ISO-8859-1; a UTF-8 value
  with a code point above U+00FF fails the request with `E_INVALIDARG`.
- `ws_many_queued_sends` also guards a fixed websocketpp bug: a send queued behind another used
  to report `S_OK` from `XAsyncGetStatus` before its operation had started, so a caller that
  waited on and freed it caused a use-after-free when the provider later began it.