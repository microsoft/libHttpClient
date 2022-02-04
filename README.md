## Welcome!

libHttpClient provides a platform abstraction layer for HTTP and WebSocket, and is designed for use by the Microsoft Xbox Live Service API [(XSAPI)](https://github.com/Microsoft/xbox-live-api), PlayFab SDKs, and game devs.  If you want to contribute to the project, please talk to us to avoid overlap.

## Goals

- libHttpClient provides a **platform abstraction layer** for **HTTP** and **WebSocket**
- Stock implementations that call **native platform HTTP / WebSocket APIs** on GDK, XDK ERA, Win32 Win7+, UWP, iOS, Android 
- Caller can add support for **other platforms via callback** API
- Sample showing off an [**HTTP implementation via Curl**](https://github.com/curl/curl) via this callback
- Designed around the needs of **professional game developers** that use Xbox Live and PlayFab
- **used by** the Microsoft Xbox Live Service API [(XSAPI)](https://github.com/Microsoft/xbox-live-api) and PlayFab SDKs
- Builds for **GDK, XDK ERA, UWP, Win32 Win7+, iOS, and Android**
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

## Behavior control

* On GDK, XDK ERA, UWP, iOS, and Android, HCHttpCallPerform() will call native platform APIs
* Optionally call HCSetHttpCallPerformFunction() to do your own HTTP handling using HCHttpCallRequestGet*(), HCHttpCallResponseSet*(), and HCSettingsGet*()
* See sample CustomHttpImplWithCurl for an example of how to use this callback to make your own HTTP implementation.

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

