// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"


static bool g_gotCall = false;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(MockTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(MockTests);

    HCCallHandle CreateMockCall(CHAR* strResponse, bool makeSpecificUrl, bool makeSpecificBody)
    {
        HCCallHandle mockCall;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&mockCall));
        if (makeSpecificUrl)
        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(mockCall, "1", "2"));
        }
        if (makeSpecificBody)
        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(mockCall, "requestBody"));
        }
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetNetworkErrorCode(mockCall, E_OUTOFMEMORY, 300));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetStatusCode(mockCall, 400));
        std::string s1 = strResponse;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetResponseBodyBytes(mockCall, (uint8_t*)&s1[0], s1.length()));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(mockCall, "mockHeader", "mockValue"));
        return mockCall;
    }

    DEFINE_TEST_CASE(ExampleSingleGenericMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleGenericMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));

        HCCallHandle mockCall = CreateMockCall("Mock1", false, false);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall, "", "", nullptr, 0));

        XTaskQueueHandle queue;
        XTaskQueueCreate(
            XTaskQueueDispatchMode::Manual,
            XTaskQueueDispatchMode::Manual,
            &queue);

        XAsyncBlock* asyncBlock = new XAsyncBlock;
        ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
        asyncBlock->context = call;
        asyncBlock->queue = queue;
        asyncBlock->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock));

        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);

        XTaskQueueCloseHandle(queue);
        HCCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCCallHandle mockCall = CreateMockCall("Mock1", true, false);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall, nullptr, nullptr, nullptr, 0));

        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));
        g_gotCall = false;

        XTaskQueueHandle queue;
        XTaskQueueCreate(
            XTaskQueueDispatchMode::Manual,
            XTaskQueueDispatchMode::Manual,
            &queue);

        XAsyncBlock* asyncBlock = new XAsyncBlock;
        ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
        asyncBlock->context = call;
        asyncBlock->queue = queue;
        asyncBlock->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "10", "20"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));

        XAsyncBlock* asyncBlock2 = new XAsyncBlock;
        ZeroMemory(asyncBlock2, sizeof(XAsyncBlock));
        asyncBlock2->context = call;
        asyncBlock2->queue = queue;
        asyncBlock2->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock2));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        XTaskQueueCloseHandle(queue);
        HCCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlBodyMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCCallHandle mockCall = CreateMockCall("Mock1", true, true);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall, nullptr, nullptr, nullptr, 0));

        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        g_gotCall = false;

        XTaskQueueHandle queue;
        XTaskQueueCreate(
            XTaskQueueDispatchMode::Manual,
            XTaskQueueDispatchMode::Manual,
            &queue);

        XAsyncBlock* asyncBlock = new XAsyncBlock;
        ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
        asyncBlock->context = call;
        asyncBlock->queue = queue;
        asyncBlock->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));

        XAsyncBlock* asyncBlock2 = new XAsyncBlock;
        ZeroMemory(asyncBlock2, sizeof(XAsyncBlock));
        asyncBlock2->context = call;
        asyncBlock2->queue = queue;
        asyncBlock2->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCloseHandle(call);
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock2));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(S_OK, HCMockClearMocks());

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

        XAsyncBlock* asyncBlock3 = new XAsyncBlock;
        ZeroMemory(asyncBlock3, sizeof(XAsyncBlock));
        asyncBlock3->context = call;
        asyncBlock3->queue = queue;
        asyncBlock3->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock3));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCMockClearMocks();

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

        XAsyncBlock* asyncBlock4 = new XAsyncBlock;
        ZeroMemory(asyncBlock4, sizeof(XAsyncBlock));
        asyncBlock4->context = call;
        asyncBlock4->queue = queue;
        asyncBlock4->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCloseHandle(call);
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock4));

        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        g_gotCall = false;

        XTaskQueueCloseHandle(queue);
        HCCleanup();
    }


    DEFINE_TEST_CASE(ExampleMultiSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleMultiSpecificUrlBodyMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCCallHandle mockCall1 = CreateMockCall("Mock1", true, true);
        HCCallHandle mockCall2 = CreateMockCall("Mock2", true, true);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall1, nullptr, nullptr, nullptr, 0));
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall2, nullptr, nullptr, nullptr, 0));

        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        g_gotCall = false;

        XTaskQueueHandle queue;
        XTaskQueueCreate(
            XTaskQueueDispatchMode::Manual,
            XTaskQueueDispatchMode::Manual,
            &queue);

        XAsyncBlock* asyncBlock = new XAsyncBlock;
        ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
        asyncBlock->context = call;
        asyncBlock->queue = queue;
        asyncBlock->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

        XAsyncBlock* asyncBlock2 = new XAsyncBlock;
        ZeroMemory(asyncBlock2, sizeof(XAsyncBlock));
        asyncBlock2->context = call;
        asyncBlock2->queue = queue;
        asyncBlock2->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock2));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        // Call 3 should repeat mock 2
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

        XAsyncBlock* asyncBlock3 = new XAsyncBlock;
        ZeroMemory(asyncBlock3, sizeof(XAsyncBlock));
        asyncBlock3->context = call;
        asyncBlock3->queue = queue;
        asyncBlock3->callback = [](XAsyncBlock* asyncBlock)
        {
            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock3));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        XTaskQueueCloseHandle(queue);
        HCCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
