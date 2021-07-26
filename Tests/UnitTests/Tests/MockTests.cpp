// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"

#pragma warning(disable:4389)

static bool g_gotCall = false;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(MockTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(MockTests);

    HCMockCallHandle CreateMockCall(CHAR* strResponse, bool makeSpecificUrl, bool makeSpecificBody)
    {
        UNREFERENCED_PARAMETER(makeSpecificUrl);
        UNREFERENCED_PARAMETER(makeSpecificBody);
        HCMockCallHandle mockCall;
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mockCall));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetNetworkErrorCode(mockCall, E_OUTOFMEMORY, 300));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetStatusCode(mockCall, 400));
        std::string s1 = strResponse;
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetResponseBodyBytes(mockCall, (uint8_t*)&s1[0], (uint32_t)s1.length()));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetHeader(mockCall, "mockHeader", "mockValue"));
        return mockCall;
    }

    DEFINE_TEST_CASE(ExampleSingleGenericMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleGenericMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));

        HCMockCallHandle mockCall = CreateMockCall("Mock1", false, false);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall, "", "", nullptr, 0));

        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));

        HRESULT errCode = S_OK;
        uint32_t platErrCode = 0;
        uint32_t statusCode = 0;
        PCSTR responseStr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
        VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
        VERIFY_ARE_EQUAL(300, platErrCode);
        VERIFY_ARE_EQUAL(400, statusCode);
        VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));

        HCCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCMockCallHandle mockCall = CreateMockCall("Mock1", true, false);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall, nullptr, nullptr, nullptr, 0));

        HCCallHandle call = nullptr;

        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));

            XAsyncBlock asyncBlock{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        }

        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "10", "20"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "3"));

            XAsyncBlock asyncBlock2{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock2));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock2, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        }

        HCCleanup();
    }

    DEFINE_TEST_CASE(ExampleSingleSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleSingleSpecificUrlBodyMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCMockCallHandle mockCall = CreateMockCall("Mock1", true, true);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall, nullptr, nullptr, nullptr, 0));

        HCCallHandle call = nullptr;

        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

            XAsyncBlock asyncBlock{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        }

        VERIFY_ARE_EQUAL(S_OK, HCMockClearMocks());

        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

            XAsyncBlock asyncBlock3{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock3));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock3, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        }

        HCMockClearMocks();

        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

            XAsyncBlock asyncBlock4{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock4));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock4, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(0, errCode);
            VERIFY_ARE_EQUAL(0, statusCode);
            VERIFY_ARE_EQUAL_STR("", responseStr);
        }

        HCCleanup();
    }

    DEFINE_TEST_CASE(ExampleMultiSpecificUrlBodyMock)
    {
        DEFINE_TEST_CASE_PROPERTIES(ExampleMultiSpecificUrlBodyMock);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCMockCallHandle mockCall1 = CreateMockCall("Mock1", true, true);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall1, nullptr, nullptr, nullptr, 0));

        HCCallHandle call = nullptr;

        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

            XAsyncBlock asyncBlock{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock1", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        }

        HCMockCallHandle mockCall2 = CreateMockCall("Mock2", true, true);
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall2, nullptr, nullptr, nullptr, 0));

        {
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

            XAsyncBlock asyncBlock2{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock2));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock2, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        }

        {
            // Call 3 should repeat mock 2
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "requestBody"));

            XAsyncBlock asyncBlock3{};
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock3));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock3, true));

            HRESULT errCode = S_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            PCSTR responseStr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &responseStr));
            VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);
            VERIFY_ARE_EQUAL(300, platErrCode);
            VERIFY_ARE_EQUAL(400, statusCode);
            VERIFY_ARE_EQUAL_STR("Mock2", responseStr);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        }

        HCCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
