// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "singleton.h"

using namespace xbox::httpclient;

#define VERIFY_EXCEPTION_TO_HR(x,hrVerify) \
        try \
        { \
            throw x; \
        } \
        catch (...) \
        { \
            HRESULT hr = utils::convert_exception_to_hresult(); \
            VERIFY_ARE_EQUAL(hr, hrVerify); \
        }

static bool g_memAllocCalled = false;
static bool g_memFreeCalled = false;

static _Ret_maybenull_ _Post_writable_byte_size_(size) void* HC_CALLING_CONV MemAlloc(
    _In_ size_t size,
    _In_ HC_MEMORY_TYPE memoryType
    )   
{
    g_memAllocCalled = true;
    return new (std::nothrow) int8_t[size];
}

static void HC_CALLING_CONV MemFree(
    _In_ _Post_invalid_ void* pointer,
    _In_ HC_MEMORY_TYPE memoryType
    )
{
    g_memFreeCalled = true;
    delete[] pointer;
}

static bool g_PerformCallbackCalled = false;
static void HC_CALLING_CONV PerformCallback(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    g_PerformCallbackCalled = true;
}


DEFINE_TEST_CLASS(HttpTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(HttpTests);

    DEFINE_TEST_CASE(TestMem)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMem);
        g_memAllocCalled = false;
        g_memFreeCalled = false;

        HCMemSetFunctions(&MemAlloc, &MemFree);

        {
            http_internal_vector<int> v;
            v.reserve(10000);

            VERIFY_ARE_EQUAL(true, g_memAllocCalled);
            VERIFY_ARE_EQUAL(false, g_memFreeCalled);
            g_memAllocCalled = false;
            g_memFreeCalled = false;
        }
        VERIFY_ARE_EQUAL(false, g_memAllocCalled);
        VERIFY_ARE_EQUAL(true, g_memFreeCalled);

        HC_MEM_ALLOC_FUNC memAllocFunc = nullptr;
        HC_MEM_FREE_FUNC memFreeFunc = nullptr;
        HCMemGetFunctions(&memAllocFunc, &memFreeFunc);
        VERIFY_IS_NOT_NULL(memAllocFunc);
        VERIFY_IS_NOT_NULL(memFreeFunc);

        HCMemSetFunctions(nullptr, nullptr);

        g_memAllocCalled = false;
        g_memFreeCalled = false;
        {
            http_internal_vector<int> v;
            v.reserve(10000);

            VERIFY_ARE_EQUAL(false, g_memAllocCalled);
            VERIFY_ARE_EQUAL(false, g_memFreeCalled);
        }
        VERIFY_ARE_EQUAL(false, g_memAllocCalled);
        VERIFY_ARE_EQUAL(false, g_memFreeCalled);
    }

    DEFINE_TEST_CASE(TestGlobalInit)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestGlobalInit);

        VERIFY_IS_NULL(get_http_singleton());
        HCGlobalInitialize();
        VERIFY_IS_NOT_NULL(get_http_singleton());
        HCGlobalCleanup();
        VERIFY_IS_NULL(get_http_singleton());
    }

    DEFINE_TEST_CASE(TestGlobalPerformCallback)
    {
        HCGlobalInitialize();
        g_PerformCallbackCalled = false;
        HCGlobalSetHttpCallPerformFunction(&PerformCallback);
        HC_CALL_HANDLE call;
        HCHttpCallCreate(&call);
        VERIFY_ARE_EQUAL(false, g_PerformCallbackCalled);
        HCHttpCallPerform(0, call, nullptr, 
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                uint32_t errCode = 0;
                HCHttpCallResponseGetErrorCode(call, &errCode);
                uint32_t statusCode = 0;
                HCHttpCallResponseGetStatusCode(call, &statusCode);
            });
        VERIFY_ARE_EQUAL(true, g_PerformCallbackCalled);
        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestSettings)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSettings);
        HCGlobalInitialize();

        HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_ERROR);
        HC_LOG_LEVEL level;
        HCSettingsGetLogLevel(&level);
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_ERROR, level);

        HCSettingsSetTimeoutWindow(1000);
        uint32_t timeout = 0;
        HCSettingsGetTimeoutWindow(&timeout);
        VERIFY_ARE_EQUAL(1000, timeout);

        HCSettingsSetAssertsForThrottling(true);
        bool enabled = false;
        HCSettingsGetAssertsForThrottling(&enabled);
        VERIFY_ARE_EQUAL(true, enabled);

        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCall);

        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);
        VERIFY_IS_NOT_NULL(call);
        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestRequest)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequest);
        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        HCHttpCallRequestSetUrl(call, L"1", L"2");
        const WCHAR* t1 = nullptr;
        const WCHAR* t2 = nullptr;
        HCHttpCallRequestGetUrl(call, &t1, &t2);
        VERIFY_ARE_EQUAL_STR(L"1", t1);
        VERIFY_ARE_EQUAL_STR(L"2", t2);

        HCHttpCallRequestSetRequestBodyString(call, L"4");
        HCHttpCallRequestGetRequestBodyString(call, &t1);
        VERIFY_ARE_EQUAL_STR(L"4", t1);

        HCHttpCallRequestSetRetryAllowed(call, true);
        bool retry = false;
        HCHttpCallRequestGetRetryAllowed(call, &retry);
        VERIFY_ARE_EQUAL(true, retry);

        HCHttpCallRequestSetTimeout(call, 1000);
        uint32_t timeout = 0;
        HCHttpCallRequestGetTimeout(call, &timeout);
        VERIFY_ARE_EQUAL(1000, timeout);

        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }


    DEFINE_TEST_CASE(TestRequestHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaders);
        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        uint32_t numHeaders = 0;
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(0, numHeaders);

        HCHttpCallRequestSetHeader(call, L"testHeader", L"testValue");
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallRequestSetHeader(call, L"testHeader", L"testValue2");
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        const WCHAR* t1 = nullptr;
        HCHttpCallRequestGetHeader(call, L"testHeader", &t1);
        VERIFY_ARE_EQUAL_STR(L"testValue2", t1);
        HCHttpCallRequestGetHeader(call, L"testHeader2", &t1);
        VERIFY_IS_NULL(t1);
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallRequestSetHeader(call, L"testHeader", L"testValue");
        HCHttpCallRequestSetHeader(call, L"testHeader2", L"testValue2");
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(2, numHeaders);

        const WCHAR* hn0 = nullptr;
        const WCHAR* hv0 = nullptr;
        const WCHAR* hn1 = nullptr;
        const WCHAR* hv1 = nullptr;
        HCHttpCallRequestGetHeaderAtIndex(call, 0, &hn0, &hv0);
        HCHttpCallRequestGetHeaderAtIndex(call, 1, &hn1, &hv1);
        VERIFY_ARE_EQUAL_STR(L"testHeader", hn0);
        VERIFY_ARE_EQUAL_STR(L"testValue", hv0);
        VERIFY_ARE_EQUAL_STR(L"testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR(L"testValue2", hv1);

        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestResponse)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponse);

        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        HCHttpCallResponseSetResponseString(call, L"test1");
        const WCHAR* t1 = nullptr;
        HCHttpCallResponseGetResponseString(call, &t1);
        VERIFY_ARE_EQUAL_STR(L"test1", t1);

        HCHttpCallResponseSetStatusCode(call, 200);
        uint32_t statusCode = 0;
        HCHttpCallResponseGetStatusCode(call, &statusCode);
        VERIFY_ARE_EQUAL(200, statusCode);

        HCHttpCallResponseSetErrorCode(call, 101);
        uint32_t errorCode = 0;
        HCHttpCallResponseGetErrorCode(call, &errorCode);
        VERIFY_ARE_EQUAL(101, errorCode);

        HCHttpCallResponseSetErrorMessage(call, L"test2");
        HCHttpCallResponseGetErrorMessage(call, &t1);
        VERIFY_ARE_EQUAL_STR(L"test2", t1);

        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestResponseHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseHeaders);

        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        uint32_t numHeaders = 0;
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(0, numHeaders);

        HCHttpCallResponseSetHeader(call, L"testHeader", L"testValue");
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallResponseSetHeader(call, L"testHeader", L"testValue2");
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        const WCHAR* t1 = nullptr;
        HCHttpCallResponseGetHeader(call, L"testHeader", &t1);
        VERIFY_ARE_EQUAL_STR(L"testValue2", t1);
        HCHttpCallResponseGetHeader(call, L"testHeader2", &t1);
        VERIFY_IS_NULL(t1);
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallResponseSetHeader(call, L"testHeader", L"testValue");
        HCHttpCallResponseSetHeader(call, L"testHeader2", L"testValue2");
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(2, numHeaders);

        const WCHAR* hn0 = nullptr;
        const WCHAR* hv0 = nullptr;
        const WCHAR* hn1 = nullptr;
        const WCHAR* hv1 = nullptr;
        HCHttpCallResponseGetHeaderAtIndex(call, 0, &hn0, &hv0);
        HCHttpCallResponseGetHeaderAtIndex(call, 1, &hn1, &hv1);
        VERIFY_ARE_EQUAL_STR(L"testHeader", hn0);
        VERIFY_ARE_EQUAL_STR(L"testValue", hv0);
        VERIFY_ARE_EQUAL_STR(L"testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR(L"testValue2", hv1);

        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }
};

