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

        constexpr char mockUrl[]{ "www.bing.com" };

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

        constexpr char mockUrl[]{ "www.bing.com" };

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

    DEFINE_TEST_CASE(TestHttpCallCompletionCallbackIsNotCleanupCancelable)
    {
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        PumpedTaskQueue pumpedQueue;

        constexpr char mockUrl[]{ "www.bing.com" };

        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 200));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", mockUrl, nullptr, 0));

        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "GET", mockUrl));

        bool callbackSawCleanupCancelableRequest{ false };
        HANDLE httpCallCompleteEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        XAsyncThunk httpPerformThunk{ [&](XAsyncBlock* async)
        {
            auto httpSingleton = get_http_singleton();
            VERIFY_IS_NOT_NULL(httpSingleton.get());
            callbackSawCleanupCancelableRequest = httpSingleton->m_networkState->CanCleanupCancelHttpRequest(async);
            SetEvent(httpCallCompleteEvent);
        }, pumpedQueue.queue };

        VERIFY_SUCCEEDED(HCHttpCallPerformAsync(call, &httpPerformThunk.asyncBlock));
        VERIFY_ARE_EQUAL((DWORD)WAIT_OBJECT_0, WaitForSingleObject(httpCallCompleteEvent, INFINITE));

        VERIFY_IS_FALSE(callbackSawCleanupCancelableRequest);

        CloseHandle(httpCallCompleteEvent);
        VERIFY_SUCCEEDED(HCHttpCallCloseHandle(call));
        HCCleanup();
    }

    // Race B reproduction: an in-flight API caller (e.g. HCHttpCallPerformAsync) takes a strong
    // singleton reference via get_http_singleton() and has not yet dereferenced m_networkState.
    // Cleanup running concurrently must not detach/destroy NetworkState out from under that
    // reference, otherwise the caller null-derefs the moved-from m_networkState.
    DEFINE_TEST_CASE(TestCleanupKeepsNetworkStateForInFlightSingletonRef)
    {
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        PumpedTaskQueue pumpedQueue;

        auto inFlightSingletonRef = get_http_singleton();
        VERIFY_IS_NOT_NULL(inFlightSingletonRef.get());

        XAsyncBlock cleanupAsyncBlock{ pumpedQueue.queue };
        VERIFY_SUCCEEDED(HCCleanupAsync(&cleanupAsyncBlock));
        // XAsyncBegin dispatches the cleanup Begin op synchronously on this thread, so by the time
        // HCCleanupAsync returns the singleton has been detached. A pre-fix build has already
        // std::move'd m_networkState out from under our still-live reference at this point.
        bool networkStateStillValid = (inFlightSingletonRef->m_networkState.get() != nullptr);

        // Release our reference and drain cleanup to completion BEFORE asserting, so a failing
        // assertion never unwinds the test with an in-flight cleanup still referencing the stack
        // async block.
        inFlightSingletonRef.reset();
        VERIFY_SUCCEEDED(XAsyncGetStatus(&cleanupAsyncBlock, true));

        VERIFY_IS_TRUE(networkStateStillValid);
    }

    // Race A reproduction: once cleanup has begun on NetworkState, a perform whose Begin op runs
    // afterwards must be rejected rather than inserted into m_activeHttpRequests. Otherwise it is
    // orphaned past the cleanup snapshot (never canceled/awaited) and can run against a torn-down
    // provider.
    DEFINE_TEST_CASE(TestHttpPerformRejectedAfterCleanupStarted)
    {
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        XTaskQueueHandle queue{ nullptr };
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        auto cleanupProvider = [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
            {
                return S_OK;
            }
            case XAsyncOp::DoWork:
            {
                XAsyncComplete(data->async, S_OK, 0);
                return E_PENDING;
            }
            default:
            {
                return S_OK;
            }
            }
        };

        XAsyncBlock cleanupAsyncBlock{ queue };
        VERIFY_SUCCEEDED(XAsyncBegin(&cleanupAsyncBlock, nullptr, nullptr, nullptr, cleanupProvider));

        constexpr char mockUrl[]{ "www.bing.com" };
        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 200));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", mockUrl, nullptr, 0));

        HCCallHandle call{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "GET", mockUrl));

        auto httpSingleton = get_http_singleton();
        VERIFY_IS_NOT_NULL(httpSingleton.get());

        // Simulate cleanup having begun on NetworkState after its tracking-set snapshot. The
        // cleanup async block is intentionally real and unscheduled so a rejected perform must not
        // consume cleanup's one allowed schedule.
        httpSingleton->m_networkState->TestSetCleanupStarted(true, &cleanupAsyncBlock);

        XAsyncBlock performAsyncBlock{ queue };
        VERIFY_SUCCEEDED(httpSingleton->m_networkState->HttpCallPerformAsync(call, &performAsyncBlock));

        HRESULT performStatus = XAsyncGetStatus(&performAsyncBlock, true);
        VERIFY_ARE_EQUAL(E_HC_NOT_INITIALISED, performStatus);
        VERIFY_IS_FALSE(httpSingleton->m_networkState->CanCleanupCancelHttpRequest(&performAsyncBlock));

        HRESULT cleanupScheduleHr = XAsyncSchedule(&cleanupAsyncBlock, 0);
        VERIFY_IS_TRUE(XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&cleanupAsyncBlock, true));
        VERIFY_SUCCEEDED(cleanupScheduleHr);

        httpSingleton->m_networkState->TestSetCleanupStarted(false);
        httpSingleton.reset();

        VERIFY_SUCCEEDED(HCHttpCallCloseHandle(call));
        HCCleanup();
        XTaskQueueCloseHandle(queue);
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
