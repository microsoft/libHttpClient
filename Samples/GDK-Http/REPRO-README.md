# GDK console suspend-hang repro (Curl provider) — bug 63050439

This sample has been adapted into a **runnable Xbox-console repro** for a suspend/quiesce hang in
libHttpClient's **Curl provider** (used on GDK console → xCurl).

## What it demonstrates

On a real Xbox console, the GDK build of libHttpClient auto-selects the **Curl provider (xCurl)** —
see `Source/Platform/GDK/PlatformComponents_GDK.cpp` (*"Detected Xbox console, using XCurl"*). On
Desktop/PC it uses the **WinHttp provider** instead.

- The **WinHttp provider already handles PLM suspend**: `winhttp_provider.cpp` registers
  `RegisterAppStateChangeNotification` and, on suspend, calls `Suspend()` → `CloseAllConnections`.
- The **Curl provider has no suspend handling.** `curl_multi_perform` is driven by callbacks dispatched
  on a title-owned `XTaskQueue`. When a title parks that queue during suspend (normal PLM behavior),
  the in-flight request stops being driven, and xCurl's suspend handler
  (`Curl_multi::WaitForActiveHandles`) blocks **INFINITE** waiting for it to drain → the suspend
  handler times out → the watchdog terminates the title.

**This repro only reproduces on a real Xbox console**, because the Curl provider is only selected
there. A Desktop/PC run uses WinHttp and will *not* hang.

## How the repro works (code)

- `Game::Update()` keeps one long-running HTTP call to `c_reproUrl`
  (`https://httpbin.org/delay/30`) **in-flight at all times** — starting a new one whenever none is
  pending. Swap `c_reproUrl` for any endpoint that holds its response long enough (a large download or
  a local slow endpoint) if httpbin isn't reachable in your lab.
- `Game::Initialize()` creates a **Manual/Manual** `XTaskQueue` and starts two background threads that
  pump its Work/Completion ports (`StartBackgroundThreads`). This is the title-owned queue the Curl
  provider drives.
- `Game::OnSuspending()` calls `ShutdownBackgroundThreads()` — **parking the queue pump** exactly as a
  title does when it quiesces during suspend. With a request in-flight, the Curl provider stalls and
  xCurl's `WaitForActiveHandles` hangs.
- Suspend is delivered by the standard GDK console PLM path in `Main.cpp`
  (`RegisterAppStateChangeNotification` + deferral via `WaitForSingleObject(g_plmSuspendComplete, …)`).

The sample links the **real GDK-shipped `libHttpClient.lib` / `XCurl.lib`** (via the cross-platform
GDK extension libs), so it exercises the shipping Curl provider — not a local build.

## Build (Xbox console)

Requires the Microsoft GDK with Xbox extensions (built/verified with GDK **260400**). From a VS2022
developer environment:

```
MSBuild.exe Samples\GDK-Http\GDKHttp.vcxproj /p:Configuration=Debug /p:Platform=Gaming.Xbox.Scarlett.x64
```

- Platforms available: `Gaming.Xbox.Scarlett.x64` and `Gaming.Xbox.XboxOne.x64` (Debug/Profile/Release).
- Output loose layout (Scarlett/Debug):
  `Samples\GDK-Http\Gaming.Xbox.Scarlett.x64\Debug\` — contains `GDKHttp.exe`, `MicrosoftGame.Config`,
  `gameos.xvd`, and the extension DLLs `libHttpClient.dll` + `XCurl.dll`.

## Deploy, run, and suspend (Xbox console)

Use a devkit whose recovery is compatible with the GDK you built with, in a network/sandbox where the
endpoint is reachable.

```
:: Deploy the loose layout
xbapp deploy /x:<consoleIp> Samples\GDK-Http\Gaming.Xbox.Scarlett.x64\Debug

:: Launch
xbapp launch /x:<consoleIp> GDKHttpSample_...!Game        (use the AUMID printed by deploy)

:: While "REPRO: starting long-running call ..." is printing (a call is in-flight), suspend:
xbapp suspend /x:<consoleIp> <AUMID>
::   or, on a quick-resume console:  xbapp save /x:<consoleIp> <AUMID>
```

**Expected result:** with a request in-flight when the queue is parked, the console **fails to suspend**
— the suspend handler times out / the title is terminated by the watchdog — the LHC-level repro of the
Curl-provider quiesce hang.

## Where the fix belongs

Give the **Curl provider** suspend handling analogous to the WinHttp provider: register for app state
change notifications and, on suspend, stop/close the in-flight multi handles (or otherwise unblock
`WaitForActiveHandles`) rather than relying on the title's (now-parked) task queue to drive
`curl_multi_perform`. Relevant files: `Source/HTTP/Curl/CurlProvider.cpp`, `Source/HTTP/Curl/CurlMulti.cpp`
(compare with `Source/HTTP/WinHttp/winhttp_provider.cpp`).

## Notes

- The `Gaming.Xbox.Scarlett.x64\` build-output folder is disposable and should not be committed.
- This sample was converted from the Desktop GDK template to the **console** GDK template
  (`d3d12game_gx`): `pch.h` (`gxdk.h` / `d3d12_xs.h`, no dxgi), console `DeviceResources.*`, and a
  console `Main.cpp` (PLM suspend). The libHttpClient repro logic lives in `Game.cpp` / `Game.h`.
