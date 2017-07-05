## Welcome!

libHttpClient is still under construction and is not ready to use.  
You can review the goals below to see what shape the project will have in its final form.
If you want to contribute on the project, please talk to us to avoid overlap.

## Goals

- libHttpClient provides an **platform abstraction layer** for **HTTP** and **WebSocket**
- Stock implementations that call **native platform HTTP / WebSocket APIs** on UWP, XDK ERA, iOS, Android 
- Caller can add support for **other platforms via callback** API
- Sample showing off an **HTTP implementation via Curl** https://github.com/curl/curl via this callback
- Designed around the needs of **professional game developers** that use Xbox Live
- Will be **used by** the Microsoft Xbox Live Service API (XSAPI) [https://github.com/Microsoft/xbox-live-api]
- Builds for **UWP, XDK ERA, Win32, iOS, and Android**
- Public API is a **flat C API**
- **Asynchronous** API
- Public API **supports simple P/Invoke** without needing to use the "C#/.NET P/Invoke Interop SDK" [https://en.wikipedia.org/wiki/Platform_Invocation_Services#C.23.2F.NET_P.2FInvoke_Interop_SDK]
- Public APIs to **manage internal threads**
- **No streams** support
- **No dependencies** on PPL or Boost
- **Does not throw exceptions** as a means of non-fatal error reporting
- Caller controlled **memory allocation** via callback API (similar to XDK's XMemAlloc)
- Built-in **logging** support to either debug output and/or callback
- **Built in retry** support according to Xbox Live best practices (obey Retry-After header, jitter wait, etc) according to https://developer.microsoft.com/en-us/games/xbox/docs/xboxlive/using-xbox-live/best-practices/best-practices-for-calling-xbox-live
- **Xbox Live throttle** handling logic
- Built-in API support to switch to **mock layer**
- Does **not require C++/CX** to build on UWP or ERA
- **Open source** project on GitHub
- Binaries eventually on Nuget.org as **Nuget packages**, and maybe VcPkg
- Unit tests via TAEF
- End to end samples for UWP C++, UWP C# (with P/Invoke layer), XDK ERA, iOS, and Android
- Support for cancellation?

## HTTP API Usage

[See public header](../../tree/master/Include/httpClient/httpClient.h)

1. Optionally call HCMemSetFunctions() to control memory allocations
1. Call HCGlobalInitialize()
1. Optionally call HCSettingsSet*()
1. Call HCHttpCallCreate() to create a new HC_CALL_HANDLE
1. Call HCHttpCallRequestSet*() to prepare the HC_CALL_HANDLE
1. Call HCHttpCallPerform() to perform HTTP call using the HC_CALL_HANDLE.  This call is asynchronous, so the work will be done on task thread and it will return via the callback.
1. Call HCHttpCallResponseGet*() to get the HTTP response of the HC_CALL_HANDLE
1. Call HCHttpCallCleanup() to cleanup the HC_CALL_HANDLE
1. Repeat 4-8 for each new HTTP call
1. Call HCGlobalCleanup() at shutdown before your memory manager set in step 1 is shutdown

## Behavior control

* On UWP, XDK ERA, iOS, and Android, HCHttpCallPerform() will call native platform APIs
* Optionally call HCGlobalSetHttpCallPerformCallback() to create do your own HTTP handling using HCHttpCallRequestGet*(), HCHttpCallResponseSet*(), and HCSettingsGet*()
* See sample CustomHttpImplWithCurl how to use this callback plus Curl https://github.com/curl/curl to make an HTTP implementation using Curl.

## TODO

Big things that still need to be done:

APIs:
* [In progress] Flushout of async API pattern
* [In progress] APIs for controling mock layer
* API for binary response data 
* WebSocket APIs

Implementation:
* Retry logic
* Throttling logic
* [In progress] Flushout of UWP implementation calling UWP platform HTTP / WebSocket APIs
* XDK project & XDK implementation calling XDK platform HTTP / WebSocket APIs
* iOS project & iOS implementation calling iOS platform HTTP / WebSocket APIs
* Android project & Android implementation calling Android platform HTTP / WebSocket APIs

Samples:
* XDK ERA sample
* iOS sample
* Android sample
* P/Invoke layer and UWP C# sample

## How to clone repo

This repo contains submodules.  There are two ways to make sure you get submodules.

When initially cloning, make sure you use the "--recursive" option. IE:

    git clone --recursive https://github.com/Microsoft/libHttpClient.git

If you already cloned the repo, you can initialize submodules with:

    git submodule sync
    git submodule update --init --recursive

Note that using GitHub's feature to "Download Zip" does not contain the submodules and will not properly build.  Please clone recursively instead.

## Contribute Back!

Is there a feature missing that you'd like to see, or found a bug that you have a fix for? Or do you have an idea or just interest in helping out in building the library? Let us know and we'd love to work with you. For a good starting point on where we are headed and feature ideas, take a look at our [requested features and bugs](../../issues).  

Big or small we'd like to take your contributions back to help improve the Xbox Live Service API for everyone.

## Having Trouble?

We'd love to get your review score, whether good or bad, but even more than that, we want to fix your problem. If you submit your issue as a Review, we won't be able to respond to your problem and ask any follow-up questions that may be necessary. The most efficient way to do that is to open a an issue in our [issue tracker](../../issues).  

### Xbox Live GitHub projects
*   [Xbox Live Service API for C++](https://github.com/Microsoft/xbox-live-api)
*   [Xbox Live Samples](https://github.com/Microsoft/xbox-live-samples)
*   [Xbox Live Unity Plugin](https://github.com/Microsoft/xbox-live-unity-plugin)
*   [Xbox Live Resiliency Fiddler Plugin](https://github.com/Microsoft/xbox-live-resiliency-fiddler-plugin)
*   [Xbox Live Trace Analyzer](https://github.com/Microsoft/xbox-live-trace-analyzer)
*   [Xbox Live Powershell Cmdlets](https://github.com/Microsoft/xbox-live-powershell-module)

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/). For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.
