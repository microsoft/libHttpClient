# Copilot Instructions — libHttpClient

## Project Overview

libHttpClient is a **cross-platform C/C++ library** providing a platform abstraction layer for **HTTP** and **WebSocket** communication. It is used by Xbox Live Service API (XSAPI) and the PlayFab SDK.

- **Language:** C/C++ (C++17 standard required)
- **Public API:** Flat C API — all public functions use `HC*` prefix with C linkage (`STDAPI`)
- **Platforms:** Win32, UWP, GDK (Xbox/PC gaming), XDK (legacy Xbox One), iOS, macOS, Android, Linux
- **Build systems:** MSBuild (Windows), CMake (Linux/Android), Xcode (iOS/macOS)
- **CI:** Azure DevOps (see `Utilities/Pipelines/libHttpClient.CI.yml`)

## Architecture

```
Include/httpClient/    — Public C API headers (httpClient.h, httpProvider.h, mock.h, trace.h, async.h)
Include/               — XAsync.h, XAsyncProvider.h, XTaskQueue.h (async task queue API)
Source/
  Common/              — Shared utilities, types, Result<T>, error macros, pch, memory allocator
  Global/              — Global state, singleton, custom memory (mem.h)
  HTTP/                — Core HTTP call logic, retry, compression
    WinHttp/           — Win32/GDK HTTP provider (WinHTTP)
    XMLHttp/           — UWP HTTP provider
    Curl/              — Linux HTTP provider (libcurl)
    Android/           — Android HTTP provider (JNI bridge)
    Apple/             — iOS/macOS HTTP provider
  WebSocket/           — Core WebSocket logic
    Websocketpp/       — Linux WebSocket provider (websocketpp + asio)
    Android/           — Android WebSocket provider
  Mock/                — Mock HTTP/WebSocket layer for testing
  Platform/            — Platform initialization (PlatformComponents pattern)
    Win32/, UWP/, GDK/, XDK/, Android/, Apple/, Linux/, Generic/
  SSL/                 — SSL/TLS support
  Task/                — Async task infrastructure
  Logger/              — Logging/tracing
Build/                 — Platform-specific build projects (.vcxproj, CMakeLists.txt, .xcworkspace)
Tests/UnitTests/       — TAEF/TE unit tests
External/              — Git submodules: openssl, curl, websocketpp, asio, zlib
Samples/               — Sample apps (Win32, UWP, GDK)
Utilities/Pipelines/   — Azure DevOps CI pipeline definitions
```

### Platform Abstraction Pattern

Each platform implements `PlatformInitialize()` in `Source/Platform/<Platform>/PlatformComponents_<Platform>.cpp`, which creates platform-specific `IHttpProvider` and `IWebSocketProvider` implementations. Platform selection is controlled by:

- **MSBuild:** `platform_select.props` auto-detects based on `ApplicationType`/`Platform` → sets `HC_PLATFORM_MSBUILD_GUESS`
- **Code:** `HC_PLATFORM` preprocessor constant (e.g., `HC_PLATFORM_WIN32`, `HC_PLATFORM_GDK`, `HC_PLATFORM_ANDROID`)
- **Feature flags:** `HC_NOZLIB`, `HC_NOWEBSOCKETS` to exclude optional features

## Coding Conventions

### Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Public C API functions | `HC` prefix, PascalCase | `HCHttpCallCreate()`, `HCWebSocketConnectAsync()` |
| Internal functions | camelCase | `ShouldRetry()`, `ResetResponseProperties()` |
| Member variables | `m_` prefix | `m_provider`, `m_refCount` |
| Types/Classes | PascalCase | `HC_CALL`, `WinHttpProvider`, `Result<T>` |
| Constants/Macros | UPPER_SNAKE_CASE | `MAX_DELAY_TIME_IN_SEC`, `HC_PLATFORM_WIN32` |
| File names | PascalCase or descriptive | `PlatformComponents_Win32.cpp`, `httpcall.h` |

### Namespaces

```cpp
NAMESPACE_XBOX_HTTP_CLIENT_BEGIN   // namespace xbox { namespace httpclient {
NAMESPACE_XBOX_HTTP_CLIENT_END     // }}
```

Sub-namespaces: `xbox::httpclient::log`, `xbox::httpclient::detail`, `xbox::httpclient::test`

### Error Handling

- All functions return `HRESULT` (S_OK on success)
- **No exceptions thrown** from public API — all functions are `noexcept` with `try {} CATCH_RETURN()` wrapping
- Use error macros from `Source/Common/ResultMacros.h`:
  - `RETURN_IF_FAILED(hr)` — early return on failure
  - `RETURN_HR_IF(hr, condition)` — conditional return
  - `RETURN_IF_NULL_ALLOC(ptr)` — returns E_OUTOFMEMORY if null
  - `LOG_IF_FAILED(hr)` — log without returning
- Internal result type: `Result<T>` (Source/Common/Result.h) — wraps HRESULT + optional payload + error message

### Memory Management

- Caller-controlled allocation via `HCMemSetFunctions()` callback
- Use custom allocator types from `Source/Common/Types.h`:
  - `http_internal_string`, `http_internal_vector<T>`, `http_internal_map<K,V>`
  - `HC_UNIQUE_PTR<T>`, `SharedPtr<T>`, `UniquePtr<T>`
  - `http_allocate_unique<T>(...)` for creating unique_ptr with custom allocator
- **Never use raw `new`/`delete`** — use `Make<T>()`/`Delete<T>()` from `Source/Global/mem.h`
- RAII patterns for all resource management

### Headers

- Use `#pragma once` (no traditional include guards)
- Include order: `pch.h` → own header → internal headers (quoted) → platform headers (angle brackets) → STL
- Public API headers use angle brackets: `#include <httpClient/httpClient.h>`
- Internal headers use quoted relative paths: `#include "HTTP/httpcall.h"`

### Other Patterns

- Atomic reference counting for handle types (`std::atomic<int> refCount`)
- Copy/move constructors deleted on handle types
- Static `CALLBACK` functions for C-style callback bridges
- Conditional compilation via `#if HC_PLATFORM == HC_PLATFORM_*`
- Compiler warnings: Level 4, warnings as errors
- Security: SDL checks, Control Flow Guard (`/guard:cf`), ASLR

## Build Commands

### Windows (MSBuild)

```powershell
# VS2022 — Win32 x64 Debug
msbuild libHttpClient.vs2022.sln /p:Configuration=Debug /p:Platform=x64

# VS2022 — Win32 x64 Release
msbuild libHttpClient.vs2022.sln /p:Configuration=Release /p:Platform=x64

# VS2022 — ARM64
msbuild libHttpClient.vs2022.sln /p:Configuration=Debug /p:Platform=ARM64

# GDK target
msbuild libHttpClient.vs2022.sln /p:Configuration=Debug /p:Platform=Gaming.Desktop.x64
```

Or open `libHttpClient.vs2022.sln` in Visual Studio and build from the IDE.

**Build configuration flags** (set in `hc_settings.props`, copy from `hc_settings.props.example`):
- `HCNoZlib=true` — exclude zlib/compression
- `HCNoWebSockets=true` — exclude WebSocket APIs
- `HCExternalOpenSSL=true` — use external OpenSSL binaries instead of bundled

### Linux (CMake)

```bash
# Uses build scripts in Utilities/Pipelines/Scripts/
# Build OpenSSL, curl, then libHttpClient:
bash Utilities/Pipelines/Scripts/openssl_Linux.bash -c Debug
bash Utilities/Pipelines/Scripts/curl_Linux.bash -c Debug
bash Utilities/Pipelines/Scripts/libHttpClient_Linux.bash -c Debug -st  # static lib
bash Utilities/Pipelines/Scripts/libHttpClient_Linux.bash -c Debug      # shared lib
```

### iOS/macOS (Xcode)

```bash
# Workspace: Build/libHttpClient.Apple.C/libHttpClient.xcworkspace
# Schemes: libHttpClient, libHttpClient_NOWEBSOCKETS
xcodebuild -workspace Build/libHttpClient.Apple.C/libHttpClient.xcworkspace \
  -scheme libHttpClient -sdk iphoneos -configuration Debug clean build
```

### Android (Gradle + NDK)

```bash
cd Build/libHttpClient.Android.Workspace
./gradlew assembleDebug
```

## Testing

### Unit Tests (TAEF/TE)

Test files are in `Tests/UnitTests/Tests/`:
- `HttpTests.cpp`, `WebsocketTests.cpp`, `MockTests.cpp`, `GlobalTests.cpp`
- `TaskQueueTests.cpp`, `AsyncBlockTests.cpp`, `LocklessQueueTests.cpp`, `ProxyTests.cpp`
- `BufferSize/` — buffer size unit and E2E tests

**Test frameworks:** Both TAEF and TE (Visual Studio CppUnitTest) are supported via macro abstraction in `Tests/UnitTests/Support/DefineTestMacros.h`.

**Running tests:**

```powershell
# TAEF from command line (after building the test DLL)
te.exe Out\x64\Debug\libHttpClient.UnitTest.TAEF\libHttpClient.UnitTest.TAEF.dll

# Or use Visual Studio Test Explorer with the TE project
```

**Test patterns:**
- Use `DEFINE_TEST_CLASS(Name)` / `DEFINE_TEST_CASE(Name)` macros (not raw TEST_CLASS/TEST_METHOD)
- Use `VERIFY_ARE_EQUAL`, `VERIFY_SUCCEEDED(hr)`, `VERIFY_IS_TRUE` assertions
- Async tests use `PumpedTaskQueue` helper (creates manual-dispatch XTaskQueue with worker threads)
- Mock HTTP responses via `HCMockCallCreate()` + `HCMockResponseSet*()` APIs
- Callback bridging via `CallbackThunk<T, R>` template

### CI Pipeline

Azure DevOps pipeline at `Utilities/Pipelines/libHttpClient.CI.yml`:
- **Triggers:** Push to `main`, PRs to `main`/`releases/*`, nightly at 8am UTC
- **Matrix:** Win32/UWP VS2022 (x86/x64/ARM64 × Debug/Release), iOS (Debug/Release), Linux (Debug/Release)

## External Dependencies

All managed as git submodules in `External/`:
- **openssl** — SSL/TLS
- **curl** — HTTP for Linux
- **websocketpp** — WebSocket for Linux
- **asio** — Async I/O for websocketpp
- **zlib** — Compression

After cloning, run: `git submodule update --init --recursive`

## Key Rules

1. **All public API functions must be `noexcept`** with `try {} CATCH_RETURN()` wrapping
2. **Never throw exceptions** from public API — use HRESULT error codes
3. **Use custom allocators** — never raw `new`/`delete`; use `http_internal_*` types and `Make<T>`/`Delete<T>`
4. **Platform code stays isolated** — platform-specific logic goes in `Source/Platform/<Platform>/` or `Source/HTTP/<Provider>/`
5. **All new functionality needs unit tests** — use DEFINE_TEST_CLASS/DEFINE_TEST_CASE macros
6. **Submit PRs against the development branch**, not main
7. **Keep changes small** — avoid unnecessary deltas
