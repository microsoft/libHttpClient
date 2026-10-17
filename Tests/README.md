# libHttpClient tests

| Suite | What it covers | Where | Runs in CI |
|---|---|---|---|
| **Unit tests** | The public C API and library core (calls, retry/Retry-After, compression, mocks, WebSocket API surface, settings, tracing, URI/headers, XAsync/XTaskQueue) against the *Generic* platform with fake providers. No network. | `Tests/UnitTests` | Windows x64/x86 Debug+Release (VSTest, TE flavor), Linux Debug/Release/ASan+UBSan and macOS (ctest `-L unit`) |
| **E2E tests** | The real platform providers (WinHTTP, websocketpp, ...) against a local Python loopback server. Nothing leaves `127.0.0.1`. | `Tests/E2E` ([README](E2E/README.md)) | Windows x64 Debug+Release |
| **Standalone repros** | Regression guards for specific TaskQueue/WinHTTP races | `Tests/StandaloneTests` | Linux (ctest `-L taskqueue`), Windows x64 (`WinHttpProviderLockStallRepro`) |
| **Android instrumentation** | Java/OkHttp request plumbing | `Build/libHttpClient.Android/src/androidTest` | Manual (needs an emulator/device); see [the workspace README](../Build/libHttpClient.Android.Workspace/README.md) |

## Running the unit tests

**Windows (Visual Studio).** Build `Build\libHttpClient.UnitTest.TE\libHttpClient.UnitTest.TE.vcxproj` (it's in
`libHttpClient.vs2022.sln`), then use Test Explorer, or:

```powershell
vstest.console.exe Out\x64\Debug\libHttpClient.UnitTest.TE\libHttpClient.UnitTest.TE.dll /Platform:x64
vstest.console.exe ... /Tests:RetryTests                     # one class or test (substring match)
vstest.console.exe ... /Settings:Tests\UnitTests\coverage.runsettings   # with code coverage (Cobertura)
```

The TAEF flavor (`libHttpClient.UnitTest.TAEF`) compiles the same sources; it needs TAEF installed
(see `Build/libHttpClient.UnitTest.TAEF/readme.md`).

**Linux / macOS (CMake + ctest).** The same sources build with a small header-only runner
(`Tests/UnitTests/Support/Portable`):

```sh
# Linux: build with the test targets on (libHttpClient_Linux.bash -t; see Build/libHttpClient.Linux/README.md)
cd Int/CMake/libHttpClient.Linux && ctest --output-on-failure -L unit
# Standalone (any host, no curl/OpenSSL needed)
cmake -S Build/libHttpClient.UnitTest.CMake -B Int/CMake/ut -DCMAKE_BUILD_TYPE=Debug
cmake --build Int/CMake/ut --parallel && (cd Int/CMake/ut && ctest --output-on-failure -L unit)
```

`libHttpClient_Linux.bash -t -san` builds the tests with AddressSanitizer + UndefinedBehaviorSanitizer.

## Writing a unit test

- Add a `.cpp` under `Tests/UnitTests/Tests/` and list it in
  `Build/libHttpClient.UnitTest/libHttpClient.UnitTest.vcxitems` (+ `.filters`). CMake picks it up
  automatically; Windows-only files go on the exclude list in `Build/libHttpClient.CMake/UnitTests.cmake`.
- Use `DEFINE_TEST_CLASS` / `DEFINE_TEST_CLASS_PROPS` / `DEFINE_TEST_CASE` and the `VERIFY_*` macros
  from `Support/DefineTestMacros.h` (never raw `TEST_METHOD`), so the test builds for TE, TAEF and the
  portable runner.
- **Isolation is automatic.** `DEFINE_TEST_CLASS_PROPS` installs a per-test fixture
  (`Support/TestEnvironment.h`) that, after every test, cleans up libHttpClient if it's still
  initialized, removes installed perform/WebSocket callbacks, mocks, trace callbacks and test hooks,
  and restores global settings and the memory functions. A test that leaves libHttpClient initialized
  or leaks allocations under `LeakCheckAllocator` fails. Still clean up explicitly: close every handle
  (including mock handles after `HCMockAddMock`) and fetch async results.
- Unit tests have no network: drive responses with mocks (`HCMockCallCreate` + `HCMockResponseSet*`)
  or a fake provider installed with `HCSetHttpCallPerformFunction` / `HCSetWebSocketFunctions`.
- Don't sleep through retry backoff: `RetryTests.cpp` shows the internal clock and retry-delay hooks
  (`HC_CALL::SetClockTestHook`, `SetPerformDelayTestHook`, unit-test builds only).
- Keep tests portable: guard Win32-only APIs with `#if HC_PLATFORM_IS_MICROSOFT`, use lowercase
  include names (`utils.h`, `../Global/global.h`), and qualify `xbox::httpclient::Uri`.
- Tests must pass in any order: run your class alone and the full suite in **Debug and Release**
  (they run in different orders).

## E2E tests

See [E2E/README.md](E2E/README.md) for running the suite, the test server's routes, and how to add a test.
