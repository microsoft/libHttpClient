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

    HC_CALL_HANDLE CreateMockCall(CHAR* strResponse, bool makeSpecificUrl, bool makeSpecificBody)
    {
        HC_CALL_HANDLE mockCall;
        HCHttpCallCreate(&mockCall);
        if (makeSpecificUrl)
        {
            HCHttpCallRequestSetUrl(mockCall, "1", "2");
        }
        if (makeSpecificBody)
        {
            HCHttpCallRequestSetRequestBodyString(mockCall, "requestBody");
        }
        HCHttpCallResponseSetNetworkErrorCode(mockCall, HC_E_OUTOFMEMORY, 300);
        HCHttpCallResponseSetStatusCode(mockCall, 400);
        HCHttpCallResponseSetResponseString(mockCall, strResponse);
        HCHttpCallResponseSetHeader(mockCall, "mockHeader", "mockValue");
        return mockCall;
    }

    DEFINE_TEST_CASE(ExampleSingleGenericMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleGenericMock);

        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        HC_CALL_HANDLE mockCall = CreateMockCall("Mock1", false, false);
        HCMockAddMock(mockCall, "1", "2", "3");

        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode); 
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlMock);

        HCGlobalInitialize();

        HC_CALL_HANDLE mockCall = CreateMockCall("Mock1", true, false);
        HCMockAddMock(mockCall, nullptr, nullptr, nullptr);

        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "3");
        g_gotCall = false;
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "10", "20");
        HCHttpCallRequestSetRequestBodyString(call, "3");
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlBodyMock);

        HCGlobalInitialize();

        HC_CALL_HANDLE mockCall = CreateMockCall("Mock1", true, true);
        HCMockAddMock(mockCall, nullptr, nullptr, nullptr);

        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "requestBody");
        g_gotCall = false;
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "3");
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCMockClearMocks();

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "requestBody");
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCMockClearMocks();

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "requestBody");
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        g_gotCall = false;

        HCGlobalCleanup();
    }


    DEFINE_TEST_CASE(ExampleMultiSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleMultiSpecificUrlBodyMock);

        HCGlobalInitialize();

        HC_CALL_HANDLE mockCall1 = CreateMockCall("Mock1", true, true);
        HC_CALL_HANDLE mockCall2 = CreateMockCall("Mock2", true, true);
        HCMockAddMock(mockCall1, nullptr, nullptr, nullptr);
        HCMockAddMock(mockCall2, nullptr, nullptr, nullptr);

        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "requestBody");
        g_gotCall = false;
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "requestBody");
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        // Call 3 should repeat mock 2
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, "1", "2");
        HCHttpCallRequestSetRequestBodyString(call, "requestBody");
        HCHttpCallPerform(nullptr, 0, call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        VERIFY_ARE_EQUAL(false, g_gotCall);
        HCTaskProcessNextPendingTask();
        HCTaskProcessNextCompletedTask(0);
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCGlobalCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
