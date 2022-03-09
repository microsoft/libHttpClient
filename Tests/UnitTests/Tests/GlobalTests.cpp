// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../Common/Win/utils_win.h"
#include "PumpedTaskQueue.h"
#include "CallbackThunk.h"

class XAsyncThunk
{
public:
    XAsyncThunk(std::function<void(XAsyncBlock*)> func, XTaskQueueHandle queue = nullptr) :
        asyncBlock{ queue, this, Callback },
        _func(func)
    {
    }

    XAsyncBlock asyncBlock;

private:
    static void CALLBACK Callback(XAsyncBlock* asyncBlock)
    {
        XAsyncThunk* pthis = static_cast<XAsyncThunk*>(asyncBlock->context);
        pthis->_func(asyncBlock);
    }

    std::function<void(XAsyncBlock*)> _func;
};

using namespace xbox::httpclient;
static bool g_gotCall = false;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(GlobalTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(GlobalTests);

    DEFINE_TEST_CASE(TestFns)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestFns);

        PCSTR ver;
        HCGetLibVersion(&ver);
        VERIFY_ARE_EQUAL_STR("1.0.0.0", ver);

#pragma warning(disable: 4800)
        http_internal_wstring utf16 = utf16_from_utf8("test");
        VERIFY_ARE_EQUAL_STR(L"test", utf16.c_str());
        http_internal_string utf8 = utf8_from_utf16(L"test");
        VERIFY_ARE_EQUAL_STR("test", utf8.c_str());
    }

    DEFINE_TEST_CASE(TestAsyncCleanup)
    {
        VERIFY_ARE_EQUAL(HCIsInitialized(), false);
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        VERIFY_ARE_EQUAL(HCIsInitialized(), true);

        PumpedTaskQueue pumpedQueue;
        XAsyncBlock cleanupAsyncBlock{ pumpedQueue.queue };
        VERIFY_SUCCEEDED(HCCleanupAsync(&cleanupAsyncBlock));

        VERIFY_SUCCEEDED(XAsyncGetStatus(&cleanupAsyncBlock, true));
    }

    DEFINE_TEST_CASE(TestAsyncCleanupWithHttpCall)
    {
        HCSettingsSetTraceLevel(HCTraceLevel::Verbose);
        HCTraceSetTraceToDebugger(true);
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        PumpedTaskQueue pumpedQueue;

        constexpr char* mockUrl{ "www.bing.com" };

        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 500));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", mockUrl, nullptr, 0));

        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "GET", mockUrl));

        bool httpCallComplete{ false };
        bool cleanupComplete{ false };
        HANDLE cleanupCompleteEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        XAsyncThunk httpPerformThunk{ [&](XAsyncBlock* async)
        {
            httpCallComplete = true;
            VERIFY_IS_TRUE(!cleanupComplete);
            VERIFY_IS_TRUE(FAILED(XAsyncGetStatus(async, false)));
        }, pumpedQueue.queue };

        XAsyncThunk hcCleanupThunk{ [&](XAsyncBlock* async)
        {
            cleanupComplete = true;
            VERIFY_IS_TRUE(httpCallComplete);
            VERIFY_SUCCEEDED(XAsyncGetStatus(async, false));
            SetEvent(cleanupCompleteEvent);
        }, pumpedQueue.queue };

        VERIFY_SUCCEEDED(HCHttpCallPerformAsync(call, &httpPerformThunk.asyncBlock));
        VERIFY_SUCCEEDED(HCCleanupAsync(&hcCleanupThunk.asyncBlock));

        VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(cleanupCompleteEvent, INFINITE));
        CloseHandle(cleanupCompleteEvent);
    }

    DEFINE_TEST_CASE(TestAsyncCleanupWithHttpCallPendingRetry)
    {
        HCSettingsSetTraceLevel(HCTraceLevel::Verbose);
        HCTraceSetTraceToDebugger(true);
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        PumpedTaskQueue pumpedQueue;

        constexpr char* mockUrl{ "www.bing.com" };

        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 500));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", mockUrl, nullptr, 0));

        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "GET", mockUrl));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(call, 5));

        bool httpCallComplete{ false };
        bool cleanupComplete{ false };
        HANDLE cleanupCompleteEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        XAsyncThunk httpPerformThunk{ [&](XAsyncBlock* async)
        {
            httpCallComplete = true;
            VERIFY_IS_TRUE(!cleanupComplete);
            VERIFY_ARE_EQUAL(E_ABORT, XAsyncGetStatus(async, false));
        }, pumpedQueue.queue };

        XAsyncThunk hcCleanupThunk{ [&](XAsyncBlock* async)
        {
            cleanupComplete = true;
            VERIFY_IS_TRUE(httpCallComplete);
            VERIFY_SUCCEEDED(XAsyncGetStatus(async, false));
            SetEvent(cleanupCompleteEvent);
        }, pumpedQueue.queue };

        VERIFY_SUCCEEDED(HCHttpCallPerformAsync(call, &httpPerformThunk.asyncBlock));
        HCHttpCallCloseHandle(call); // Closing handle before perform async operation complete should not cause crash
        Sleep(2000);
        VERIFY_SUCCEEDED(HCCleanupAsync(&hcCleanupThunk.asyncBlock));

        VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(cleanupCompleteEvent, INFINITE));
        CloseHandle(cleanupCompleteEvent);
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
