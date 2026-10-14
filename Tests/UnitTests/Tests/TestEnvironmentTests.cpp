// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// Tests for the per-test isolation fixture (TestEnvironment, wired in through
// DEFINE_TEST_CLASS_PROPS). They check that every test starts from library defaults and that
// TestEnvironment::ResetLibraryState puts every piece of process-wide state back, whatever a test
// left behind.
//

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include "Logger/trace_internal.h"
#include "Platform/ExternalHttpProvider.h"
#include "Platform/ExternalWebSocketProvider.h"
#include <httpClient/httpProvider.h>

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

// The default request limit is device-dependent: unlimited everywhere except Xbox consoles. This
// test project builds for Win32/UWP, so the expected default here is unlimited.
constexpr uint32_t c_defaultRequestLimit = UINT32_MAX;

void CALLBACK DirtyPerform(HCCallHandle, XAsyncBlock* asyncBlock, void*, HCPerformEnv) noexcept
{
    XAsyncComplete(asyncBlock, E_FAIL, 0);
}

HRESULT CALLBACK DirtyWebSocketConnect(PCSTR, PCSTR, HCWebsocketHandle, XAsyncBlock*, void*, HCPerformEnv) noexcept
{
    return E_NOTIMPL;
}

HRESULT CALLBACK DirtyWebSocketSend(HCWebsocketHandle, PCSTR, XAsyncBlock*, void*) noexcept
{
    return E_NOTIMPL;
}

HRESULT CALLBACK DirtyWebSocketSendBinary(HCWebsocketHandle, const uint8_t*, uint32_t, XAsyncBlock*, void*) noexcept
{
    return E_NOTIMPL;
}

HRESULT CALLBACK DirtyWebSocketDisconnect(HCWebsocketHandle, HCWebSocketCloseStatus, void*) noexcept
{
    return E_NOTIMPL;
}

void CALLBACK DirtyTraceCallback(const char*, HCTraceLevel, uint64_t, uint64_t, const char*) noexcept
{
}

bool Contains(std::vector<std::string> const& items, char const* text)
{
    for (auto const& item : items)
    {
        if (item.find(text) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

bool MemoryFunctionsAreDefault()
{
    HCMemAllocFunction allocCurrent{ nullptr };
    HCMemFreeFunction freeCurrent{ nullptr };
    VERIFY_SUCCEEDED(HCMemGetFunctions(&allocCurrent, &freeCurrent));

    // HCMemSetFunctions(nullptr, nullptr) installs the defaults; reinstall whatever was there.
    VERIFY_SUCCEEDED(HCMemSetFunctions(nullptr, nullptr));
    HCMemAllocFunction allocDefault{ nullptr };
    HCMemFreeFunction freeDefault{ nullptr };
    VERIFY_SUCCEEDED(HCMemGetFunctions(&allocDefault, &freeDefault));
    VERIFY_SUCCEEDED(HCMemSetFunctions(allocCurrent, freeCurrent));

    return allocCurrent == allocDefault && freeCurrent == freeDefault;
}

// Everything the fixture promises about the state a test starts in.
void VerifyLibraryStateIsDefault()
{
    VERIFY_IS_FALSE(HCIsInitialized());
    VERIFY_IS_FALSE(ExternalHttpProvider::Get().HasCallback());
    VERIFY_IS_FALSE(ExternalWebSocketProvider::Get().HasCallbacks());
    VERIFY_IS_TRUE(MemoryFunctionsAreDefault());
    VERIFY_IS_FALSE(LeakCheckAllocator::IsInstalled());

    uint32_t requestLimit{ 0 };
    VERIFY_SUCCEEDED(HCSettingsGetGlobalRequestLimit(&requestLimit));
    VERIFY_ARE_EQUAL_UINT(c_defaultRequestLimit, requestLimit);

    HCTraceLevel traceLevel{ HCTraceLevel::Off };
    VERIFY_SUCCEEDED(HCSettingsGetTraceLevel(&traceLevel));
    VERIFY_IS_TRUE(traceLevel == HCTraceLevel::Information);

    TraceState& traceState = GetTraceState();
    VERIFY_IS_FALSE(traceState.IsSetup());
    VERIFY_IS_FALSE(traceState.HasClientCallbacks());
    VERIFY_IS_FALSE(traceState.GetTraceToDebugger());
    VERIFY_IS_FALSE(traceState.GetEtwEnabled());
    VERIFY_IS_NULL(GetThreadIdInfo().callback);
    VERIFY_IS_NULL(GetWriteToDebuggerInfo().callback);

    // Per-singleton settings come back at their defaults on the next HCInitialize.
    VERIFY_SUCCEEDED(HCInitialize(nullptr));
    uint32_t timeout{ 0 };
    VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeout(nullptr, &timeout));
    VERIFY_ARE_EQUAL_UINT(DEFAULT_HTTP_TIMEOUT_IN_SECONDS, timeout);
    uint32_t retryDelay{ 0 };
    VERIFY_SUCCEEDED(HCHttpCallRequestGetRetryDelay(nullptr, &retryDelay));
    VERIFY_ARE_EQUAL_UINT(DEFAULT_RETRY_DELAY_IN_SECONDS, retryDelay);
    uint32_t timeoutWindow{ 0 };
    VERIFY_SUCCEEDED(HCHttpCallRequestGetTimeoutWindow(nullptr, &timeoutWindow));
    VERIFY_ARE_EQUAL_UINT(DEFAULT_TIMEOUT_WINDOW_IN_SECONDS, timeoutWindow);
    bool retryAllowed{ false };
    VERIFY_SUCCEEDED(HCHttpCallRequestGetRetryAllowed(nullptr, &retryAllowed));
    VERIFY_IS_TRUE(retryAllowed);
    HCCleanup();
    VERIFY_IS_FALSE(HCIsInitialized());
}

// Changes every piece of process-wide state the fixture is responsible for.
void DirtyEverything()
{
    VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
    VERIFY_SUCCEEDED(HCSetHttpCallPerformFunction(&DirtyPerform, nullptr));
    VERIFY_SUCCEEDED(HCSetWebSocketFunctions(DirtyWebSocketConnect, DirtyWebSocketSend, DirtyWebSocketSendBinary, DirtyWebSocketDisconnect, nullptr));
    VERIFY_SUCCEEDED(HCSettingsSetGlobalRequestLimit(3));
    VERIFY_SUCCEEDED(HCTraceSetPlatformCallbacks(
        [](void*) noexcept -> uint64_t { return 1; }, nullptr,
        [](char const*, HCTraceLevel, char const*, void*) noexcept {}, nullptr));
    VERIFY_SUCCEEDED(HCSettingsSetTraceLevel(HCTraceLevel::Verbose));
    HCTraceSetTraceToDebugger(true);
#if HC_PLATFORM_IS_MICROSOFT
    HCTraceSetEtwEnabled(true);
#endif

    // Nested initialization: the reset has to unwind both.
    VERIFY_SUCCEEDED(HCInitialize(nullptr));
    VERIFY_SUCCEEDED(HCInitialize(nullptr));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeout(nullptr, 7));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(nullptr, 9));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetTimeoutWindow(nullptr, 11));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(nullptr, false));

    HCMockCallHandle mock{ nullptr };
    VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
    VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 503));
    VERIFY_SUCCEEDED(HCMockAddMock(mock, nullptr, nullptr, nullptr, 0));
    VERIFY_SUCCEEDED(HCMockCallCloseHandle(mock));

    // An unbalanced HCTraceInit, as left by a test that failed between Init and Cleanup.
    HCTraceInit();
}

} // namespace

DEFINE_TEST_CLASS(TestEnvironmentTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(TestEnvironmentTests);

    DEFINE_TEST_CASE(TestEachTestStartsWithDefaultLibraryState)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestEachTestStartsWithDefaultLibraryState);
        VerifyLibraryStateIsDefault();
    }

    DEFINE_TEST_CASE(TestResetLibraryStateRestoresDefaults)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResetLibraryStateRestoresDefaults);

        DirtyEverything();
        VERIFY_IS_TRUE(HCIsInitialized());

        TestEnvironment::ResetResult result = TestEnvironment::ResetLibraryState();

        // Things a test must clean up itself are reported as leaks...
        VERIFY_IS_TRUE(Contains(result.leaks, "still initialized"));
        VERIFY_IS_TRUE(Contains(result.leaks, "HCTraceInit"));
        VERIFY_ARE_EQUAL_UINT(2u, result.leaks.size());

        // ...and process-wide settings are restored.
        VERIFY_IS_TRUE(Contains(result.restored, "HCSetHttpCallPerformFunction"));
        VERIFY_IS_TRUE(Contains(result.restored, "HCSetWebSocketFunctions"));
        VERIFY_IS_TRUE(Contains(result.restored, "request limit"));
        VERIFY_IS_TRUE(Contains(result.restored, "HCTraceSetPlatformCallbacks"));
        VERIFY_IS_TRUE(Contains(result.restored, "HCTraceSetTraceToDebugger"));
#if HC_PLATFORM_IS_MICROSOFT
        VERIFY_IS_TRUE(Contains(result.restored, "HCTraceSetEtwEnabled"));
#endif
        VERIFY_IS_TRUE(Contains(result.restored, "trace level"));
        VERIFY_IS_TRUE(Contains(result.restored, "HCMemSetFunctions"));

        // The mock and per-singleton settings went with the cleanup, and the leak-check allocator
        // saw every allocation freed (otherwise it would have been reported as a leak above).
        VerifyLibraryStateIsDefault();

        // A second reset finds nothing to do.
        VERIFY_IS_TRUE(TestEnvironment::ResetLibraryState().IsClean());
    }

    DEFINE_TEST_CASE(TestResetLibraryStateClearsTraceClientCallbacks)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResetLibraryStateClearsTraceClientCallbacks);

        // HCCleanup drops client callbacks, but one registered without ever initializing survives
        // until something else does.
        VERIFY_IS_TRUE(HCTraceSetClientCallback(&DirtyTraceCallback));
        VERIFY_IS_TRUE(GetTraceState().HasClientCallbacks());

        TestEnvironment::ResetResult result = TestEnvironment::ResetLibraryState();
        VERIFY_IS_TRUE(result.leaks.empty());
        VERIFY_IS_TRUE(Contains(result.restored, "HCTraceSetClientCallback"));
        VerifyLibraryStateIsDefault();
    }

    DEFINE_TEST_CASE(TestResetLibraryStateReportsLeakedAllocations)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResetLibraryStateReportsLeakedAllocations);

        VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        // An HTTP call handle the test never closes.
        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        HCCleanup();

        TestEnvironment::ResetResult result = TestEnvironment::ResetLibraryState(TestEnvironment::c_defaultCleanupTimeoutMs);
        VERIFY_IS_TRUE(Contains(result.leaks, "not freed"));
        VERIFY_IS_FALSE(LeakCheckAllocator::IsInstalled());

        // The handle outlives the singleton; closing it frees the allocations (through the default
        // free function now, which is compatible with the leak-check allocator's malloc).
        VERIFY_SUCCEEDED(HCHttpCallCloseHandle(call));
        VERIFY_IS_TRUE(TestEnvironment::ResetLibraryState().IsClean());
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
