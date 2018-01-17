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
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&mockCall));
        if (makeSpecificUrl)
        {
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(mockCall, "1", "2"));
        }
        if (makeSpecificBody)
        {
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(mockCall, "requestBody"));
        }
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetNetworkErrorCode(mockCall, HC_E_OUTOFMEMORY, 300));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetStatusCode(mockCall, 400));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetResponseString(mockCall, strResponse));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetHeader(mockCall, "mockHeader", "mockValue"));
        return mockCall;
    }

    DEFINE_TEST_CASE(ExampleSingleGenericMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleGenericMock);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));

        HC_CALL_HANDLE mockCall = CreateMockCall("Mock1", false, false);
        VERIFY_ARE_EQUAL(HC_OK, HCMockAddMock(mockCall, "", "", nullptr, 0));

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode); 
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
                g_gotCall = true;
            }));

        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlMock);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());

        HC_CALL_HANDLE mockCall = CreateMockCall("Mock1", true, false);
        VERIFY_ARE_EQUAL(HC_OK, HCMockAddMock(mockCall, nullptr, nullptr, nullptr, 0));

        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));
        g_gotCall = false;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
                g_gotCall = true;
            }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "10", "20"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
        }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlBodyMock);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());

        HC_CALL_HANDLE mockCall = CreateMockCall("Mock1", true, true);
        VERIFY_ARE_EQUAL(HC_OK, HCMockAddMock(mockCall, nullptr, nullptr, nullptr, 0));

        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        g_gotCall = false;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
                g_gotCall = true;
            }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCloseHandle(call);
            g_gotCall = true;
        }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(HC_OK, HCMockClearMocks());

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
        }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCMockClearMocks();

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            HCHttpCallCloseHandle(call);
            g_gotCall = true;
        }));

        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        g_gotCall = false;

        HCGlobalCleanup();
    }


    DEFINE_TEST_CASE(ExampleMultiSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleMultiSpecificUrlBodyMock);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());

        HC_CALL_HANDLE mockCall1 = CreateMockCall("Mock1", true, true);
        HC_CALL_HANDLE mockCall2 = CreateMockCall("Mock2", true, true);
        VERIFY_ARE_EQUAL(HC_OK, HCMockAddMock(mockCall1, nullptr, nullptr, nullptr, 0));
        VERIFY_ARE_EQUAL(HC_OK, HCMockAddMock(mockCall2, nullptr, nullptr, nullptr, 0));

        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        g_gotCall = false;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                uint32_t statusCode = 0;
                PCSTR responseStr;
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
                VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
                VERIFY_ARE_EQUAL(300, platErrCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
                g_gotCall = true;
            }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
        }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        // Call 3 should repeat mock 2
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
            g_gotCall = true;
        }));

        VERIFY_ARE_EQUAL(false, g_gotCall);
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0));
        VERIFY_ARE_EQUAL(true, g_gotCall);
        g_gotCall = false;

        HCGlobalCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
