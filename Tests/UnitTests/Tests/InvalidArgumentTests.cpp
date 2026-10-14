// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "utils.h"
#include <httpClient/httpProvider.h>
#include <httpClient/mock.h>

#include <functional>
#include <string>
#include <vector>

// Invalid-argument matrix for the public flat C API (httpClient.h, httpProvider.h, mock.h).
//
// Every public HC* function is called with a null handle and with each null pointer argument that
// its SAL annotation marks as required (_In_, _In_z_, _Out_, _Outptr_, _Inout_). The call must
// return the documented error (normally E_INVALIDARG) and must not crash. _opt_ arguments must
// accept nullptr. Where an implementation intentionally deviates from that rule the row asserts
// the actual behaviour and says why.
//
// A separate pair of tests covers APIs that require HCInitialize: they must fail with
// E_HC_NOT_INITIALISED both before HCInitialize and after HCCleanup.
//
// Rows are table driven. Failures are collected rather than asserted immediately so that a
// failing row does not skip HCCleanup and leak global state into later tests, and so that the
// failure message names every offending row at once.

namespace
{

class ArgCheckResults
{
public:
    void Expect(const char* name, HRESULT expected, HRESULT actual)
    {
        if (expected != actual)
        {
            char buf[512];
            snprintf(buf, sizeof(buf), "%s: expected 0x%08X, got 0x%08X\n", name, static_cast<unsigned>(expected), static_cast<unsigned>(actual));
            m_failures += buf;
        }
    }

    void ExpectTrue(const char* name, bool condition)
    {
        if (!condition)
        {
            m_failures += name;
            m_failures += ": condition was false\n";
        }
    }

    const std::string& Failures() const
    {
        return m_failures;
    }

private:
    std::string m_failures;
};

struct GlobalCase
{
    const char* name;
    HRESULT expected;
    std::function<HRESULT()> invoke;
};

struct CallCase
{
    const char* name;
    HRESULT expected;
    std::function<HRESULT(HCCallHandle)> invoke;
};

struct MockCase
{
    const char* name;
    HRESULT expected;
    std::function<HRESULT(HCMockCallHandle)> invoke;
};

#ifndef HC_NOWEBSOCKETS
struct WebSocketCase
{
    const char* name;
    HRESULT expected;
    std::function<HRESULT(HCWebsocketHandle)> invoke;
};
#endif

// Rows that do not need a live handle.
#define GLOBAL_ROW(expected, expr) GlobalCase{ #expr, (expected), [&]() -> HRESULT { return (expr); } }
// Rows that receive a freshly created handle named call / mock / ws.
#define CALL_ROW(expected, expr) CallCase{ #expr, (expected), [&](HCCallHandle call) -> HRESULT { return (expr); } }
#define MOCK_ROW(expected, expr) MockCase{ #expr, (expected), [&](HCMockCallHandle mock) -> HRESULT { return (expr); } }
#define WS_ROW(expected, expr) WebSocketCase{ #expr, (expected), [&](HCWebsocketHandle ws) -> HRESULT { return (expr); } }

void RunGlobalCases(ArgCheckResults& results, const std::vector<GlobalCase>& cases)
{
    for (const auto& c : cases)
    {
        results.Expect(c.name, c.expected, c.invoke());
    }
}

// Each row gets its own call handle so that rows which legitimately mutate the call (for example
// replacing the response body write function) cannot affect later rows.
void RunCallCases(ArgCheckResults& results, const std::vector<CallCase>& cases)
{
    for (const auto& c : cases)
    {
        HCCallHandle call = nullptr;
        HRESULT hr = HCHttpCallCreate(&call);
        results.Expect("HCHttpCallCreate (row setup)", S_OK, hr);
        if (FAILED(hr))
        {
            return;
        }
        results.Expect(c.name, c.expected, c.invoke(call));
        HCHttpCallCloseHandle(call);
    }
}

void RunMockCases(ArgCheckResults& results, const std::vector<MockCase>& cases)
{
    for (const auto& c : cases)
    {
        HCMockCallHandle mock = nullptr;
        HRESULT hr = HCMockCallCreate(&mock);
        results.Expect("HCMockCallCreate (row setup)", S_OK, hr);
        if (FAILED(hr))
        {
            return;
        }
        results.Expect(c.name, c.expected, c.invoke(mock));
        HCMockCallCloseHandle(mock);
    }
}

#ifndef HC_NOWEBSOCKETS
void RunWebSocketCases(ArgCheckResults& results, const std::vector<WebSocketCase>& cases)
{
    for (const auto& c : cases)
    {
        HCWebsocketHandle ws = nullptr;
        HRESULT hr = HCWebSocketCreate(&ws, nullptr, nullptr, nullptr, nullptr);
        results.Expect("HCWebSocketCreate (row setup)", S_OK, hr);
        if (FAILED(hr))
        {
            return;
        }
        results.Expect(c.name, c.expected, c.invoke(ws));
        HCWebSocketCloseHandle(ws);
    }
}
#endif

// Valid callbacks used where a row needs a non-null function pointer for the other arguments.
void STDAPIVCALLTYPE TestCallRoutedHandler(HCCallHandle, void*) {}

HRESULT CALLBACK TestRequestBodyRead(HCCallHandle, size_t, size_t, void*, uint8_t*, size_t* bytesWritten)
{
    *bytesWritten = 0;
    return S_OK;
}

HRESULT CALLBACK TestProgressReport(HCCallHandle, uint64_t, uint64_t, void*)
{
    return S_OK;
}

HRESULT CALLBACK TestResponseBodyWrite(HCCallHandle, const uint8_t*, size_t, void*)
{
    return S_OK;
}

void CALLBACK TestPerform(HCCallHandle, XAsyncBlock* asyncBlock, void*, HCPerformEnv)
{
    XAsyncComplete(asyncBlock, E_UNEXPECTED, 0);
}

void CALLBACK TestMockMatched(HCMockCallHandle, const char*, const char*, const uint8_t*, uint32_t, void*) {}

#ifndef HC_NOWEBSOCKETS
void STDAPIVCALLTYPE TestWebSocketRoutedHandler(HCWebsocketHandle, bool, const char*, const uint8_t*, size_t, void*) {}

HRESULT CALLBACK TestWsConnect(const char*, const char*, HCWebsocketHandle, XAsyncBlock*, void*, HCPerformEnv)
{
    return E_UNEXPECTED;
}

HRESULT CALLBACK TestWsSend(HCWebsocketHandle, const char*, XAsyncBlock*, void*)
{
    return E_UNEXPECTED;
}

HRESULT CALLBACK TestWsSendBinary(HCWebsocketHandle, const uint8_t*, uint32_t, XAsyncBlock*, void*)
{
    return E_UNEXPECTED;
}

HRESULT CALLBACK TestWsDisconnect(HCWebsocketHandle, HCWebSocketCloseStatus, void*)
{
    return E_UNEXPECTED;
}
#endif

} // namespace

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(InvalidArgumentTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(InvalidArgumentTests);

    // Global / provider-registration APIs that take no HC handle: HCMem*, HCGetLibVersion,
    // HCSettings*, HCSet/GetHttpCallPerformFunction, HCSet/GetWebSocketFunctions, HCCleanupAsync,
    // routed handlers, plus the obviously-required XTaskQueue output pointer.
    DEFINE_TEST_CASE(GlobalApisRejectNullArguments)
    {
        DEFINE_TEST_CASE_PROPERTIES(GlobalApisRejectNullArguments);

        // Several rows below are only meaningful before HCInitialize.
        VERIFY_IS_FALSE(HCIsInitialized());

        ArgCheckResults results;

        HCMemAllocFunction allocFn = nullptr;
        HCMemFreeFunction freeFn = nullptr;
        HCCallPerformFunction performFn = nullptr;
        void* context = nullptr;
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
        HRESULT const expectedGetLimitNull = E_INVALIDARG;
#else
        // No admission control on this platform; the API is not implemented regardless of arguments.
        HRESULT const expectedGetLimitNull = E_NOTIMPL;
#endif

        XTaskQueueHandle queue = nullptr;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        std::vector<GlobalCase> cases
        {
            GLOBAL_ROW(E_INVALIDARG, HCMemGetFunctions(nullptr, &freeFn)),
            GLOBAL_ROW(E_INVALIDARG, HCMemGetFunctions(&allocFn, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCMemGetFunctions(nullptr, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCGetLibVersion(nullptr)),
            GLOBAL_ROW(expectedGetLimitNull, HCSettingsGetGlobalRequestLimit(nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCSetHttpCallPerformFunction(nullptr, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCGetHttpCallPerformFunction(nullptr, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCGetHttpCallPerformFunction(&performFn, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCCleanupAsync(nullptr)),
            // XTaskQueue reports a null output pointer as E_POINTER.
            GLOBAL_ROW(E_POINTER, XTaskQueueDuplicateHandle(queue, nullptr)),
        };

#ifndef HC_NOWEBSOCKETS
        HCWebSocketConnectFunction connectFn = nullptr;
        HCWebSocketSendMessageFunction sendFn = nullptr;
        HCWebSocketSendBinaryMessageFunction sendBinaryFn = nullptr;
        HCWebSocketDisconnectFunction disconnectFn = nullptr;
        std::vector<GlobalCase> wsCases
        {
            // Each callback is required; a single null rejects the whole set without installing anything.
            GLOBAL_ROW(E_INVALIDARG, HCSetWebSocketFunctions(nullptr, TestWsSend, TestWsSendBinary, TestWsDisconnect, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCSetWebSocketFunctions(TestWsConnect, nullptr, TestWsSendBinary, TestWsDisconnect, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCSetWebSocketFunctions(TestWsConnect, TestWsSend, nullptr, TestWsDisconnect, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCSetWebSocketFunctions(TestWsConnect, TestWsSend, TestWsSendBinary, nullptr, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCGetWebSocketFunctions(nullptr, &sendFn, &sendBinaryFn, &disconnectFn, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCGetWebSocketFunctions(&connectFn, nullptr, &sendBinaryFn, &disconnectFn, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCGetWebSocketFunctions(&connectFn, &sendFn, nullptr, &disconnectFn, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCGetWebSocketFunctions(&connectFn, &sendFn, &sendBinaryFn, nullptr, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCGetWebSocketFunctions(&connectFn, &sendFn, &sendBinaryFn, &disconnectFn, nullptr)),
        };
        cases.insert(cases.end(), wsCases.begin(), wsCases.end());
#endif

        RunGlobalCases(results, cases);
        XTaskQueueCloseHandle(queue);

        // _In_opt_: HCMemSetFunctions(nullptr, nullptr) restores the default allocator. Save and
        // restore whatever was installed so the global state is unchanged.
        HCMemAllocFunction savedAlloc = nullptr;
        HCMemFreeFunction savedFree = nullptr;
        results.Expect("HCMemGetFunctions(&savedAlloc, &savedFree)", S_OK, HCMemGetFunctions(&savedAlloc, &savedFree));
        results.Expect("HCMemSetFunctions(nullptr, nullptr)", S_OK, HCMemSetFunctions(nullptr, nullptr));
        results.Expect("HCMemGetFunctions(&allocFn, &freeFn)", S_OK, HCMemGetFunctions(&allocFn, &freeFn));
        results.ExpectTrue("HCMemSetFunctions(nullptr, nullptr) installs non-null defaults", allocFn != nullptr && freeFn != nullptr);
        results.Expect("HCMemSetFunctions(savedAlloc, savedFree)", S_OK, HCMemSetFunctions(savedAlloc, savedFree));

        // _In_opt_ HCInitArgs.
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        // Routed handlers return an id rather than an HRESULT; a null handler yields -1.
        results.ExpectTrue("HCAddCallRoutedHandler(nullptr, nullptr) == -1", HCAddCallRoutedHandler(nullptr, nullptr) == -1);
        int32_t const callHandlerId = HCAddCallRoutedHandler(TestCallRoutedHandler, nullptr); // _In_opt_ context
        results.ExpectTrue("HCAddCallRoutedHandler(handler, nullptr) >= 0", callHandlerId >= 0);
        HCRemoveCallRoutedHandler(callHandlerId);
        HCRemoveCallRoutedHandler(-1); // Unknown id is ignored.
#ifndef HC_NOWEBSOCKETS
        results.ExpectTrue("HCAddWebSocketRoutedHandler(nullptr, nullptr) == -1", HCAddWebSocketRoutedHandler(nullptr, nullptr) == -1);
        int32_t const wsHandlerId = HCAddWebSocketRoutedHandler(TestWebSocketRoutedHandler, nullptr);
        results.ExpectTrue("HCAddWebSocketRoutedHandler(handler, nullptr) >= 0", wsHandlerId >= 0);
        HCRemoveWebSocketRoutedHandler(wsHandlerId);
        HCRemoveWebSocketRoutedHandler(-1);
#endif

        // Provider/allocator registration is refused once initialized, regardless of arguments.
        std::vector<GlobalCase> initializedCases
        {
            GLOBAL_ROW(E_HC_ALREADY_INITIALISED, HCMemSetFunctions(nullptr, nullptr)),
            GLOBAL_ROW(E_HC_ALREADY_INITIALISED, HCSetHttpCallPerformFunction(nullptr, nullptr)),
            GLOBAL_ROW(E_HC_ALREADY_INITIALISED, HCSetHttpCallPerformFunction(TestPerform, nullptr)),
#ifndef HC_NOWEBSOCKETS
            GLOBAL_ROW(E_HC_ALREADY_INITIALISED, HCSetWebSocketFunctions(TestWsConnect, TestWsSend, TestWsSendBinary, TestWsDisconnect, nullptr)),
#endif
        };
        RunGlobalCases(results, initializedCases);

        HCCleanup();
        VERIFY_IS_FALSE(HCIsInitialized());

        VERIFY_ARE_EQUAL_STR("", results.Failures().c_str());
    }

    // Every API taking an HCCallHandle, called with a null handle.
    DEFINE_TEST_CASE(HttpCallApisRejectNullHandle)
    {
        DEFINE_TEST_CASE_PROPERTIES(HttpCallApisRejectNullHandle);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        ArgCheckResults results;

        const char* str = nullptr;
        const char* str2 = nullptr;
        const uint8_t* bytesOut = nullptr;
        uint32_t u32 = 0;
        size_t sz = 0;
        size_t sz2 = 0;
        void* context = nullptr;
        HRESULT hrOut = S_OK;
        uint8_t buffer[16]{};
        HCHttpCallRequestBodyReadFunction readFn = nullptr;
        HCHttpCallProgressReportFunction progressFn = nullptr;
        HCHttpCallResponseBodyWriteFunction writeFn = nullptr;
        XAsyncBlock async{};
        HCCallHandle const nullCall = nullptr;

        std::vector<GlobalCase> cases
        {
            // httpClient.h
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallPerformAsync(nullCall, &async)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallCloseHandle(nullCall)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallSetTracing(nullCall, true)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallGetRequestUrl(nullCall, &str)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallGetPerformCount(nullCall, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetUrl(nullCall, "GET", "https://example.com")),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetDynamicSize(nullCall, 10)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestAddDynamicBytesWritten(nullCall, 10)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetRequestBodyBytes(nullCall, buffer, sizeof(buffer))),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetRequestBodyString(nullCall, "body")),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestEnableGzipCompression(nullCall, HCCompressionLevel::None)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetGzipCompressed(nullCall, true)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetRequestBodyReadFunction(nullCall, TestRequestBodyRead, 0, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetProgressReportFunction(nullCall, TestProgressReport, true, 1, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetHeader(nullCall, "name", "value", true)),
            // SAL marks the call _In_opt_ and the docs say "pass nullptr to set the default", but
            // the implementation has no global retry-cache-id default and rejects nullptr.
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetRetryCacheId(nullCall, 1001)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetMaxReceiveBufferSize(nullCall, 1024)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetMaxReceiveBufferSize(nullCall, &sz)),
#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestSetSSLValidation(nullCall, true)),
#endif
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetResponseBodyWriteFunction(nullCall, TestResponseBodyWrite, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseString(nullCall, &str)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseBodyBytesSize(nullCall, &sz)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseBodyBytes(nullCall, sizeof(buffer), buffer, &sz)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetStatusCode(nullCall, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetNetworkErrorCode(nullCall, &hrOut, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetPlatformNetworkErrorMessage(nullCall, &str)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetHeader(nullCall, "name", &str)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetNumHeaders(nullCall, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetHeaderAtIndex(nullCall, 0, &str, &str2)),

            // httpProvider.h
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallGetContext(nullCall, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallSetContext(nullCall, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetUrl(nullCall, &str, &str2)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyBytes(nullCall, &bytesOut, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyString(nullCall, &str)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyReadFunction(nullCall, &readFn, &sz, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetDynamicBytesWritten(nullCall, &sz, &sz2)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(nullCall, true, &progressFn, &sz, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetHeader(nullCall, "name", &str)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetNumHeaders(nullCall, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetHeaderAtIndex(nullCall, 0, &str, &str2)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetRetryCacheId(nullCall, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseBodyWriteFunction(nullCall, &writeFn, &context)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseGetDynamicBytesWritten(nullCall, &sz, &sz2)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetDynamicSize(nullCall, 10)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseAddDynamicBytesWritten(nullCall, 10)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetResponseBodyBytes(nullCall, buffer, sizeof(buffer))),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseAppendResponseBodyBytes(nullCall, buffer, sizeof(buffer))),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetStatusCode(nullCall, 200)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetNetworkErrorCode(nullCall, E_FAIL, 0)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetPlatformNetworkErrorMessage(nullCall, "message")),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetHeader(nullCall, "name", "value")),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallResponseSetHeaderWithLength(nullCall, "name", 4, "value", 5)),
        };
        RunGlobalCases(results, cases);

        // Non-HRESULT accessors degrade to a neutral value instead of failing.
        results.ExpectTrue("HCHttpCallDuplicateHandle(nullptr) == nullptr", HCHttpCallDuplicateHandle(nullptr) == nullptr);
        results.ExpectTrue("HCHttpCallGetId(nullptr) == 0", HCHttpCallGetId(nullptr) == 0);

        // These take an _In_opt_ call: nullptr reads or writes the global default for future calls.
        // Each setter writes back the value just read so the defaults are unchanged.
        uint32_t timeout = 0;
        uint32_t retryDelay = 0;
        uint32_t timeoutWindow = 0;
        bool retryAllowed = false;
        std::vector<GlobalCase> optCases
        {
            GLOBAL_ROW(S_OK, HCHttpCallRequestGetTimeout(nullCall, &timeout)),
            GLOBAL_ROW(S_OK, HCHttpCallRequestSetTimeout(nullCall, timeout)),
            GLOBAL_ROW(S_OK, HCHttpCallRequestGetRetryDelay(nullCall, &retryDelay)),
            GLOBAL_ROW(S_OK, HCHttpCallRequestSetRetryDelay(nullCall, retryDelay)),
            GLOBAL_ROW(S_OK, HCHttpCallRequestGetTimeoutWindow(nullCall, &timeoutWindow)),
            GLOBAL_ROW(S_OK, HCHttpCallRequestSetTimeoutWindow(nullCall, timeoutWindow)),
            GLOBAL_ROW(S_OK, HCHttpCallRequestGetRetryAllowed(nullCall, &retryAllowed)),
            GLOBAL_ROW(S_OK, HCHttpCallRequestSetRetryAllowed(nullCall, retryAllowed)),
            // Even with a null call the required _Out_ pointer is still validated.
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetTimeout(nullCall, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetRetryDelay(nullCall, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetTimeoutWindow(nullCall, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCHttpCallRequestGetRetryAllowed(nullCall, nullptr)),
        };
        RunGlobalCases(results, optCases);

        HCCleanup();
        VERIFY_ARE_EQUAL_STR("", results.Failures().c_str());
    }

    // Valid call handle, each required pointer / string argument null in turn.
    DEFINE_TEST_CASE(HttpCallApisRejectNullRequiredPointers)
    {
        DEFINE_TEST_CASE_PROPERTIES(HttpCallApisRejectNullRequiredPointers);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        ArgCheckResults results;

        const char* str = nullptr;
        const uint8_t* bytesOut = nullptr;
        uint32_t u32 = 0;
        size_t sz = 0;
        void* context = nullptr;
        HRESULT hrOut = S_OK;
        uint8_t buffer[16]{};
        HCHttpCallRequestBodyReadFunction readFn = nullptr;
        HCHttpCallProgressReportFunction progressFn = nullptr;
        HCHttpCallResponseBodyWriteFunction writeFn = nullptr;

        std::vector<CallCase> cases
        {
            // httpClient.h
            CALL_ROW(E_INVALIDARG, HCHttpCallPerformAsync(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallGetRequestUrl(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallGetPerformCount(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetUrl(call, nullptr, "https://example.com")),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetUrl(call, "GET", nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetRequestBodyBytes(call, nullptr, 4)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetRequestBodyString(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetRequestBodyReadFunction(call, nullptr, 0, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetProgressReportFunction(call, nullptr, true, 1, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetHeader(call, nullptr, "value", true)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestSetHeader(call, "name", nullptr, true)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetMaxReceiveBufferSize(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseSetResponseBodyWriteFunction(call, nullptr, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseString(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseBodyBytesSize(call, nullptr)),
            // SAL marks buffer _Out_writes_bytes_to_opt_, but the implementation always requires it
            // (use HCHttpCallResponseGetResponseBodyBytesSize to query the size).
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseBodyBytes(call, 0, nullptr, &sz)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetStatusCode(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetNetworkErrorCode(call, nullptr, &u32)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetNetworkErrorCode(call, &hrOut, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetPlatformNetworkErrorMessage(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetHeader(call, nullptr, &str)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetHeader(call, "name", nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetNumHeaders(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetHeaderAtIndex(call, 0, nullptr, &str)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetHeaderAtIndex(call, 0, &str, nullptr)),

            // httpProvider.h
            CALL_ROW(E_INVALIDARG, HCHttpCallGetContext(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetUrl(call, nullptr, &str)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetUrl(call, &str, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyBytes(call, nullptr, &u32)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyBytes(call, &bytesOut, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyString(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyReadFunction(call, nullptr, &sz, &context)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyReadFunction(call, &readFn, nullptr, &context)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRequestBodyReadFunction(call, &readFn, &sz, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetDynamicBytesWritten(call, nullptr, &sz)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetDynamicBytesWritten(call, &sz, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(call, true, nullptr, &sz, &context)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(call, true, &progressFn, nullptr, &context)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetProgressReportFunction(call, true, &progressFn, &sz, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetHeader(call, nullptr, &str)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetHeader(call, "name", nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetNumHeaders(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetHeaderAtIndex(call, 0, nullptr, &str)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetHeaderAtIndex(call, 0, &str, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRetryAllowed(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRetryCacheId(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetTimeout(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetRetryDelay(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallRequestGetTimeoutWindow(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseBodyWriteFunction(call, nullptr, &context)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetResponseBodyWriteFunction(call, &writeFn, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetDynamicBytesWritten(call, nullptr, &sz)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseGetDynamicBytesWritten(call, &sz, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseSetResponseBodyBytes(call, nullptr, 4)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseAppendResponseBodyBytes(call, nullptr, 4)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseSetPlatformNetworkErrorMessage(call, nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseSetHeader(call, nullptr, "value")),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseSetHeader(call, "name", nullptr)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseSetHeaderWithLength(call, nullptr, 4, "value", 5)),
            CALL_ROW(E_INVALIDARG, HCHttpCallResponseSetHeaderWithLength(call, "name", 4, nullptr, 5)),

            // _opt_ arguments accept nullptr.
            CALL_ROW(S_OK, HCHttpCallSetContext(call, nullptr)),
            CALL_ROW(S_OK, HCHttpCallRequestSetRequestBodyReadFunction(call, TestRequestBodyRead, 0, nullptr)),
            CALL_ROW(S_OK, HCHttpCallRequestSetProgressReportFunction(call, TestProgressReport, false, 1, nullptr)),
            CALL_ROW(S_OK, HCHttpCallResponseSetResponseBodyWriteFunction(call, TestResponseBodyWrite, nullptr)),
            CALL_ROW(S_OK, HCHttpCallResponseGetResponseBodyBytes(call, sizeof(buffer), buffer, nullptr)),
        };
        RunCallCases(results, cases);

        HCCleanup();
        VERIFY_ARE_EQUAL_STR("", results.Failures().c_str());
    }

#ifndef HC_NOWEBSOCKETS
    // Every API taking an HCWebsocketHandle: null handle, then each required pointer null in turn.
    DEFINE_TEST_CASE(WebSocketApisRejectNullArguments)
    {
        DEFINE_TEST_CASE_PROPERTIES(WebSocketApisRejectNullArguments);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        ArgCheckResults results;

        const char* str = nullptr;
        const char* str2 = nullptr;
        uint32_t u32 = 0;
        uint8_t payload[4]{ 1, 2, 3, 4 };
        XAsyncBlock async{};
        WebSocketCompletionResult completionResult{};
        HCWebsocketHandle const nullWs = nullptr;

        std::vector<GlobalCase> nullHandleCases
        {
            // httpClient.h
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketCreate(nullptr, nullptr, nullptr, nullptr, nullptr)),
#if HC_PLATFORM != HC_PLATFORM_ANDROID
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSetOptions(nullWs, HCWebSocketOptions::LegacySemantics)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSetMaxReceiveBufferSize(nullWs, 1024)),
#endif
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSetProxyUri(nullWs, "http://127.0.0.1:8888")),
#if HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK)
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSetProxyDecryptsHttps(nullWs, true)),
#endif
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSetHeader(nullWs, "name", "value")),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetResponseHeader(nullWs, "name", &str)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetNumResponseHeaders(nullWs, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetResponseHeaderAtIndex(nullWs, 0, &str, &str2)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSetPingInterval(nullWs, 10)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetEventFunctions(nullWs, nullptr, nullptr, nullptr, nullptr)),
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetBinaryMessageFragmentEventFunction(nullWs, nullptr, nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSetBinaryMessageFragmentEventFunction(nullWs, nullptr)),
#endif
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketConnectAsync("wss://example.com", "", nullWs, &async)),
            GLOBAL_ROW(E_INVALIDARG, HCGetWebSocketConnectResult(nullptr, &completionResult)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSendMessageAsync(nullWs, "message", &async)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketSendBinaryMessageAsync(nullWs, payload, sizeof(payload), &async)),
            GLOBAL_ROW(E_INVALIDARG, HCGetWebSocketSendMessageResult(nullptr, &completionResult)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketDisconnect(nullWs)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketDisconnectWithStatus(nullWs, HCWebSocketCloseStatus::Normal)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketCloseHandle(nullWs)),

            // httpProvider.h
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetProxyUri(nullWs, &str)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetHeader(nullWs, "name", &str)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetNumHeaders(nullWs, &u32)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetHeaderAtIndex(nullWs, 0, &str, &str2)),
            GLOBAL_ROW(E_INVALIDARG, HCWebSocketGetPingInterval(nullWs, &u32)),
        };
        RunGlobalCases(results, nullHandleCases);
        results.ExpectTrue("HCWebSocketDuplicateHandle(nullptr) == nullptr", HCWebSocketDuplicateHandle(nullptr) == nullptr);

#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
        HCWebSocketBinaryMessageFragmentFunction fragmentFn = nullptr;
        void* context = nullptr;
#endif

        std::vector<WebSocketCase> cases
        {
            // httpClient.h
            WS_ROW(E_INVALIDARG, HCWebSocketSetProxyUri(ws, nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketSetHeader(ws, nullptr, "value")),
            WS_ROW(E_INVALIDARG, HCWebSocketSetHeader(ws, "name", nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetResponseHeader(ws, nullptr, &str)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetResponseHeader(ws, "name", nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetNumResponseHeaders(ws, nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetResponseHeaderAtIndex(ws, 0, nullptr, &str)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetResponseHeaderAtIndex(ws, 0, &str, nullptr)),
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
            WS_ROW(E_INVALIDARG, HCWebSocketGetBinaryMessageFragmentEventFunction(ws, nullptr, &context)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetBinaryMessageFragmentEventFunction(ws, &fragmentFn, nullptr)),
            // Documented: "or a null pointer to remove".
            WS_ROW(S_OK, HCWebSocketSetBinaryMessageFragmentEventFunction(ws, nullptr)),
#endif
            WS_ROW(E_INVALIDARG, HCWebSocketConnectAsync(nullptr, "", ws, &async)),
            WS_ROW(E_INVALIDARG, HCWebSocketConnectAsync("wss://example.com", nullptr, ws, &async)),
            WS_ROW(E_INVALIDARG, HCWebSocketConnectAsync("wss://example.com", "", ws, nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketSendMessageAsync(ws, nullptr, &async)),
            WS_ROW(E_INVALIDARG, HCWebSocketSendMessageAsync(ws, "message", nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketSendBinaryMessageAsync(ws, nullptr, sizeof(payload), &async)),
            WS_ROW(E_INVALIDARG, HCWebSocketSendBinaryMessageAsync(ws, payload, sizeof(payload), nullptr)),

            // httpProvider.h
            WS_ROW(E_INVALIDARG, HCWebSocketGetProxyUri(ws, nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetHeader(ws, nullptr, &str)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetHeader(ws, "name", nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetNumHeaders(ws, nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetHeaderAtIndex(ws, 0, nullptr, &str)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetHeaderAtIndex(ws, 0, &str, nullptr)),
            WS_ROW(E_INVALIDARG, HCWebSocketGetPingInterval(ws, nullptr)),

            // _Out_opt_ outputs accept nullptr.
            WS_ROW(S_OK, HCWebSocketGetEventFunctions(ws, nullptr, nullptr, nullptr, nullptr)),
        };
        RunWebSocketCases(results, cases);

        // _In_opt_ callbacks and context.
        HCWebsocketHandle ws = nullptr;
        results.Expect("HCWebSocketCreate(&ws, nullptr, nullptr, nullptr, nullptr)", S_OK, HCWebSocketCreate(&ws, nullptr, nullptr, nullptr, nullptr));
        if (ws != nullptr)
        {
            HCWebSocketCloseHandle(ws);
        }

        HCCleanup();
        VERIFY_ARE_EQUAL_STR("", results.Failures().c_str());
    }
#endif // !HC_NOWEBSOCKETS

    // Every mock.h API: null handle, then each required pointer null in turn.
    DEFINE_TEST_CASE(MockApisRejectNullArguments)
    {
        DEFINE_TEST_CASE_PROPERTIES(MockApisRejectNullArguments);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        ArgCheckResults results;

        uint8_t body[4]{ 1, 2, 3, 4 };
        HCMockCallHandle const nullMock = nullptr;

        std::vector<GlobalCase> nullHandleCases
        {
            GLOBAL_ROW(E_INVALIDARG, HCMockCallCreate(nullptr)),
            GLOBAL_ROW(E_INVALIDARG, HCMockAddMock(nullMock, "GET", "https://example.com", nullptr, 0)),
            GLOBAL_ROW(E_INVALIDARG, HCMockSetMockMatchedCallback(nullMock, TestMockMatched, nullptr)),
            // A null mock is never in the mock list, so it is reported as "not found".
            GLOBAL_ROW(E_INVALIDARG, HCMockRemoveMock(nullMock)),
            GLOBAL_ROW(E_INVALIDARG, HCMockCallCloseHandle(nullMock)),
            GLOBAL_ROW(E_INVALIDARG, HCMockResponseSetResponseBodyBytes(nullMock, body, sizeof(body))),
            GLOBAL_ROW(E_INVALIDARG, HCMockResponseSetStatusCode(nullMock, 200)),
            GLOBAL_ROW(E_INVALIDARG, HCMockResponseSetNetworkErrorCode(nullMock, E_FAIL, 0)),
            GLOBAL_ROW(E_INVALIDARG, HCMockResponseSetHeader(nullMock, "name", "value")),
        };
        RunGlobalCases(results, nullHandleCases);
        results.ExpectTrue("HCMockCallDuplicateHandle(nullptr) == nullptr", HCMockCallDuplicateHandle(nullptr) == nullptr);

        std::vector<MockCase> cases
        {
            MOCK_ROW(E_INVALIDARG, HCMockResponseSetResponseBodyBytes(mock, nullptr, 4)),
            MOCK_ROW(E_INVALIDARG, HCMockResponseSetHeader(mock, nullptr, "value")),
            MOCK_ROW(E_INVALIDARG, HCMockResponseSetHeader(mock, "name", nullptr)),
            // A mock that was never added is "not found".
            MOCK_ROW(E_INVALIDARG, HCMockRemoveMock(mock)),

            // _opt_ arguments accept nullptr. A mock with no method/url/body matches every call.
            MOCK_ROW(S_OK, HCMockAddMock(mock, nullptr, nullptr, nullptr, 0)),
            MOCK_ROW(S_OK, HCMockAddMock(mock, "GET", "https://example.com", nullptr, 0)),
            // The callback is marked _In_ but nullptr is accepted and clears a previously set callback.
            MOCK_ROW(S_OK, HCMockSetMockMatchedCallback(mock, nullptr, nullptr)),
            MOCK_ROW(S_OK, HCMockSetMockMatchedCallback(mock, TestMockMatched, nullptr)),
        };
        RunMockCases(results, cases);

        results.Expect("HCMockClearMocks()", S_OK, HCMockClearMocks());
        HCCleanup();
        VERIFY_ARE_EQUAL_STR("", results.Failures().c_str());
    }

    // APIs that need the library initialized must fail with E_HC_NOT_INITIALISED before HCInitialize.
    DEFINE_TEST_CASE(ApisRequiringInitFailBeforeInitialize)
    {
        DEFINE_TEST_CASE_PROPERTIES(ApisRequiringInitFailBeforeInitialize);

        VERIFY_IS_FALSE(HCIsInitialized());
        ArgCheckResults results;

        HCCallHandle call = nullptr;
        HCMockCallHandle mock = nullptr;
        uint32_t u32 = 0;
        bool b = false;

        std::vector<GlobalCase> cases
        {
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallCreate(&call)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCMockCallCreate(&mock)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCMockClearMocks()),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCMockRemoveMock(nullptr)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCSetGlobalProxy("127.0.0.1:8888")),
            // Global defaults (null call) live in the singleton.
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRetryAllowed(nullptr, true)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetRetryAllowed(nullptr, &b)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetTimeout(nullptr, 30)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetTimeout(nullptr, &u32)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRetryDelay(nullptr, 2)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetRetryDelay(nullptr, &u32)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetTimeoutWindow(nullptr, 20)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetTimeoutWindow(nullptr, &u32)),
#if HC_PLATFORM == HC_PLATFORM_GDK || defined(HC_WINHTTP_WIN32_NOXASYNC)
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpDisableAssertsForSSLValidationInDevSandboxes(HCConfigSetting::SSLValidationEnforcedInRetailSandbox)),
#endif
        };

#ifndef HC_NOWEBSOCKETS
        HCWebsocketHandle ws = nullptr;
        cases.push_back(GLOBAL_ROW(E_HC_NOT_INITIALISED, HCWebSocketCreate(&ws, nullptr, nullptr, nullptr, nullptr)));
#endif
        RunGlobalCases(results, cases);
        results.ExpectTrue("no call handle created", call == nullptr);
        results.ExpectTrue("no mock handle created", mock == nullptr);
#ifndef HC_NOWEBSOCKETS
        results.ExpectTrue("no websocket handle created", ws == nullptr);
#endif

        // Routed-handler registration returns an id, not an HRESULT; before init the id slot carries
        // E_HC_NOT_INITIALISED (a negative value, so it can never collide with a real id).
        results.ExpectTrue("HCAddCallRoutedHandler before init", HCAddCallRoutedHandler(TestCallRoutedHandler, nullptr) == static_cast<int32_t>(E_HC_NOT_INITIALISED));
        HCRemoveCallRoutedHandler(0); // No-op when not initialized.
#ifndef HC_NOWEBSOCKETS
        results.ExpectTrue("HCAddWebSocketRoutedHandler before init", HCAddWebSocketRoutedHandler(TestWebSocketRoutedHandler, nullptr) == static_cast<int32_t>(E_HC_NOT_INITIALISED));
        HCRemoveWebSocketRoutedHandler(0);
#endif

        // HCCleanupAsync accepts the block and completes it with E_HC_NOT_INITIALISED.
        XTaskQueueHandle queue = nullptr;
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Immediate, XTaskQueueDispatchMode::Immediate, &queue));
        XAsyncBlock async{};
        async.queue = queue;
        results.Expect("HCCleanupAsync(&async) before init", S_OK, HCCleanupAsync(&async));
        results.Expect("XAsyncGetStatus(HCCleanupAsync before init)", E_HC_NOT_INITIALISED, XAsyncGetStatus(&async, true));
        XTaskQueueCloseHandle(queue);

        // HCCleanup is a safe no-op.
        HCCleanup();
        results.ExpectTrue("HCIsInitialized() after no-op HCCleanup", !HCIsInitialized());

        VERIFY_ARE_EQUAL_STR("", results.Failures().c_str());
    }

    // Handles created while initialized must not be usable for init-dependent APIs after HCCleanup.
    DEFINE_TEST_CASE(ApisRequiringInitFailAfterCleanup)
    {
        DEFINE_TEST_CASE_PROPERTIES(ApisRequiringInitFailAfterCleanup);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCCallHandle call = nullptr;
        HCMockCallHandle mock = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "GET", "https://example.com"));
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mock));
#ifndef HC_NOWEBSOCKETS
        HCWebsocketHandle ws = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&ws, nullptr, nullptr, nullptr, nullptr));
#endif

        HCCleanup();
        VERIFY_IS_FALSE(HCIsInitialized());

        ArgCheckResults results;
        const char* str = nullptr;
        const char* str2 = nullptr;
        uint8_t body[4]{ 1, 2, 3, 4 };
        uint32_t u32 = 0;
        bool b = false;
        HCCallHandle newCall = nullptr;
        HCMockCallHandle newMock = nullptr;
        XAsyncBlock async{};

        std::vector<GlobalCase> cases
        {
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallCreate(&newCall)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallPerformAsync(call, &async)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetUrl(call, "GET", "https://example.com")),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetUrl(call, &str, &str2)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetDynamicSize(call, 10)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRequestBodyBytes(call, body, sizeof(body))),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRequestBodyString(call, "body")),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRequestBodyReadFunction(call, TestRequestBodyRead, 0, nullptr)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetProgressReportFunction(call, TestProgressReport, true, 1, nullptr)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::None)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallResponseSetGzipCompressed(call, false)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetMaxReceiveBufferSize(call, 1024)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallResponseSetResponseBodyWriteFunction(call, TestResponseBodyWrite, nullptr)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallResponseSetDynamicSize(call, 10)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRetryAllowed(nullptr, true)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetRetryAllowed(nullptr, &b)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetTimeout(nullptr, 30)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetTimeout(nullptr, &u32)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetRetryDelay(nullptr, 2)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetRetryDelay(nullptr, &u32)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestSetTimeoutWindow(nullptr, 20)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCHttpCallRequestGetTimeoutWindow(nullptr, &u32)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCSetGlobalProxy("127.0.0.1:8888")),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCMockCallCreate(&newMock)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCMockAddMock(mock, "GET", "https://example.com", nullptr, 0)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCMockRemoveMock(mock)),
            GLOBAL_ROW(E_HC_NOT_INITIALISED, HCMockClearMocks()),

            // Pure per-handle state does not depend on the singleton and keeps working.
            GLOBAL_ROW(S_OK, HCHttpCallRequestSetHeader(call, "name", "value", true)),
            GLOBAL_ROW(S_OK, HCHttpCallGetRequestUrl(call, &str)),
        };

#ifndef HC_NOWEBSOCKETS
        HCWebsocketHandle newWs = nullptr;
        cases.push_back(GLOBAL_ROW(E_HC_NOT_INITIALISED, HCWebSocketCreate(&newWs, nullptr, nullptr, nullptr, nullptr)));
        cases.push_back(GLOBAL_ROW(E_HC_NOT_INITIALISED, HCWebSocketConnectAsync("wss://example.com", "", ws, &async)));
        cases.push_back(GLOBAL_ROW(S_OK, HCWebSocketSetHeader(ws, "name", "value")));
#endif
        RunGlobalCases(results, cases);
        results.ExpectTrue("no new call handle created", newCall == nullptr);
        results.ExpectTrue("no new mock handle created", newMock == nullptr);
        results.ExpectTrue("HCAddCallRoutedHandler after cleanup", HCAddCallRoutedHandler(TestCallRoutedHandler, nullptr) == static_cast<int32_t>(E_HC_NOT_INITIALISED));
#ifndef HC_NOWEBSOCKETS
        results.ExpectTrue("no new websocket handle created", newWs == nullptr);
        results.ExpectTrue("HCAddWebSocketRoutedHandler after cleanup", HCAddWebSocketRoutedHandler(TestWebSocketRoutedHandler, nullptr) == static_cast<int32_t>(E_HC_NOT_INITIALISED));
#endif

        // Handles outlive the singleton and can still be released.
        results.Expect("HCHttpCallCloseHandle(call) after cleanup", S_OK, HCHttpCallCloseHandle(call));
        results.Expect("HCMockCallCloseHandle(mock) after cleanup", S_OK, HCMockCallCloseHandle(mock));
#ifndef HC_NOWEBSOCKETS
        results.Expect("HCWebSocketCloseHandle(ws) after cleanup", S_OK, HCWebSocketCloseHandle(ws));
#endif

        VERIFY_ARE_EQUAL_STR("", results.Failures().c_str());
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
