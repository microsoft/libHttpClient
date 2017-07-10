// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "singleton.h"


static bool g_gotCall = false;

class MockTests : public UnitTestBase
{
public:
    TEST_CLASS(MockTests);
    DEFINE_TEST_CLASS_SETUP();

    HC_CALL_HANDLE CreateMockCall(WCHAR* strResponse, bool makeSpecificUrl, bool makeSpecificBody)
    {
        HC_CALL_HANDLE mockCall;
        HCHttpCallCreate(&mockCall);
        if (makeSpecificUrl)
        {
            HCHttpCallRequestSetUrl(mockCall, L"1", L"2");
        }
        if (makeSpecificBody)
        {
            HCHttpCallRequestSetRequestBodyString(mockCall, L"requestBody");
        }
        HCHttpCallResponseSetErrorCode(mockCall, 300);
        HCHttpCallResponseSetStatusCode(mockCall, 400);
        HCHttpCallResponseSetResponseString(mockCall, strResponse);
        HCHttpCallResponseSetErrorMessage(mockCall, L"mockErrorMessage");
        HCHttpCallResponseSetHeader(mockCall, L"mockHeader", L"mockValue");
        return mockCall;
    }

    TEST_METHOD(ExampleSingleGenericMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleGenericMock);

        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        HC_CALL_HANDLE mockCall = CreateMockCall(L"Mock1", false, false);
        HCSettingsAddMockCall(mockCall);

        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"3");
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                uint32_t errCode = 0;
                uint32_t statusCode = 0;
                PCSTR_T responseStr;
                HCHttpCallResponseGetErrorCode(call, &errCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(300, errCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR(L"Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        HCGlobalCleanup();
    }

    TEST_METHOD(ExampleSingleSpecificUrlMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlMock);

        HCGlobalInitialize();

        HC_CALL_HANDLE mockCall = CreateMockCall(L"Mock1", true, false);
        HCSettingsAddMockCall(mockCall);

        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"3");
        g_gotCall = false;
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                uint32_t errCode = 0;
                uint32_t statusCode = 0;
                PCSTR_T responseStr;
                HCHttpCallResponseGetErrorCode(call, &errCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(300, errCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR(L"Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        g_gotCall = false;

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, L"10", L"20");
        HCHttpCallRequestSetRequestBodyString(call, L"3");
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            uint32_t errCode = 0;
            uint32_t statusCode = 0;
            PCSTR_T responseStr;
            HCHttpCallResponseGetErrorCode(call, &errCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR(L"", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        g_gotCall = false;

        HCGlobalCleanup();
    }

    TEST_METHOD(ExampleSingleSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlBodyMock);

        HCGlobalInitialize();

        HC_CALL_HANDLE mockCall = CreateMockCall(L"Mock1", true, true);
        HCSettingsAddMockCall(mockCall);

        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"requestBody");
        g_gotCall = false;
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                uint32_t errCode = 0;
                uint32_t statusCode = 0;
                PCSTR_T responseStr;
                HCHttpCallResponseGetErrorCode(call, &errCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(300, errCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR(L"Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        g_gotCall = false;

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"3");
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            uint32_t errCode = 0;
            uint32_t statusCode = 0;
            PCSTR_T responseStr;
            HCHttpCallResponseGetErrorCode(call, &errCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR(L"", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        g_gotCall = false;

        HCGlobalCleanup();
    }


    TEST_METHOD(ExampleMultiSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleMultiSpecificUrlBodyMock);

        HCGlobalInitialize();

        HC_CALL_HANDLE mockCall1 = CreateMockCall(L"Mock1", true, true);
        HC_CALL_HANDLE mockCall2 = CreateMockCall(L"Mock2", true, true);
        HCSettingsAddMockCall(mockCall1);
        HCSettingsAddMockCall(mockCall2);

        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"requestBody");
        g_gotCall = false;
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                uint32_t errCode = 0;
                uint32_t statusCode = 0;
                PCSTR_T responseStr;
                HCHttpCallResponseGetErrorCode(call, &errCode);
                HCHttpCallResponseGetStatusCode(call, &statusCode);
                HCHttpCallResponseGetResponseString(call, &responseStr);
                VERIFY_ARE_EQUAL(300, errCode);
                VERIFY_ARE_EQUAL(400, statusCode);
                VERIFY_ARE_EQUAL_STR(L"Mock1", responseStr);
                HCHttpCallCleanup(call);
                g_gotCall = true;
            });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        g_gotCall = false;

        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"requestBody");
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            uint32_t errCode = 0;
            uint32_t statusCode = 0;
            PCSTR_T responseStr;
            HCHttpCallResponseGetErrorCode(call, &errCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(300, errCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR(L"Mock2", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        g_gotCall = false;

        // Call 3 should repeat mock 2
        HCHttpCallCreate(&call);
        HCHttpCallRequestSetUrl(call, L"1", L"2");
        HCHttpCallRequestSetRequestBodyString(call, L"requestBody");
        HCHttpCallPerform(call, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            uint32_t errCode = 0;
            uint32_t statusCode = 0;
            PCSTR_T responseStr;
            HCHttpCallResponseGetErrorCode(call, &errCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &responseStr);
            VERIFY_ARE_EQUAL(300, errCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR(L"Mock2", responseStr);
            HCHttpCallCleanup(call);
            g_gotCall = true;
        });

        while (!g_gotCall)
        {
            Sleep(50);
        }
        g_gotCall = false;

        HCGlobalCleanup();
    }

};

