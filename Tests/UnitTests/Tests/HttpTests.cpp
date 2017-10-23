// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

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
        DEFINE_TEST_CASE_PROPERTIES_FOCUS(TestGlobalInit);

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
        HC_HTTP_CALL_PERFORM_FUNC func = nullptr;
        HCGlobalGetHttpCallPerformFunction(&func);
        VERIFY_IS_NOT_NULL(func);

        HCGlobalSetHttpCallPerformFunction(&PerformCallback);
        HC_CALL_HANDLE call;
        HCHttpCallCreate(&call);
        VERIFY_ARE_EQUAL(false, g_PerformCallbackCalled);
        HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
                uint32_t statusCode = 0;
                HCHttpCallResponseGetStatusCode(call, &statusCode);
            });
        HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME);
        VERIFY_ARE_EQUAL(true, g_PerformCallbackCalled);
        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestSettings)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSettings);
        HCGlobalInitialize();

        HC_LOG_LEVEL level;

        HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_OFF);
        HCSettingsGetLogLevel(&level);
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_OFF, level);

        HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_ERROR);
        HCSettingsGetLogLevel(&level);
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_ERROR, level);

        HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_WARNING);
        HCSettingsGetLogLevel(&level);
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_WARNING, level);

        HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_IMPORTANT);
        HCSettingsGetLogLevel(&level);
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_IMPORTANT, level);

        HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_INFORMATION);
        HCSettingsGetLogLevel(&level);
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_INFORMATION, level);

        HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_VERBOSE);
        HCSettingsGetLogLevel(&level);
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_VERBOSE, level);

        HCHttpCallRequestSetTimeoutWindow(nullptr, 1000);
        uint32_t timeout = 0;
        HCHttpCallRequestGetTimeoutWindow(nullptr, &timeout);
        VERIFY_ARE_EQUAL(1000, timeout);

        HCHttpCallRequestSetAssertsForThrottling(nullptr, true);
        bool enabled = false;
        HCHttpCallRequestGetAssertsForThrottling(nullptr, &enabled);
        VERIFY_ARE_EQUAL(true, enabled);

        HCHttpCallRequestSetRetryDelay(nullptr, 500);
        uint32_t retryDelayInSeconds = 0;
        HCHttpCallRequestGetRetryDelay(nullptr, &retryDelayInSeconds);

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

        HCHttpCallRequestSetUrl(call, "1", "2");
        const CHAR* t1 = nullptr;
        const CHAR* t2 = nullptr;
        HCHttpCallRequestGetUrl(call, &t1, &t2);
        VERIFY_ARE_EQUAL_STR("1", t1);
        VERIFY_ARE_EQUAL_STR("2", t2);

        HCHttpCallRequestSetRequestBodyString(call, "4");
        uint32_t s1 = 0;
        HCHttpCallRequestGetRequestBodyBytes(call, &t1, &s1);
        VERIFY_ARE_EQUAL_STR("4", t1);
        VERIFY_ARE_EQUAL(strlen("4"), s1);

        HCHttpCallRequestSetRetryAllowed(call, true);
        bool retry = false;
        HCHttpCallRequestGetRetryAllowed(call, &retry);
        VERIFY_ARE_EQUAL(true, retry);

        HCHttpCallRequestSetTimeout(call, 2000);
        uint32_t timeout = 0;
        HCHttpCallRequestGetTimeout(call, &timeout);
        VERIFY_ARE_EQUAL(2000, timeout);
                
        HCHttpCallRequestSetTimeoutWindow(call, 1000);
        HCHttpCallRequestGetTimeoutWindow(call, &timeout);
        VERIFY_ARE_EQUAL(1000, timeout);

        HCHttpCallRequestSetAssertsForThrottling(call, true);
        bool enabled = false;
        HCHttpCallRequestGetAssertsForThrottling(call, &enabled);
        VERIFY_ARE_EQUAL(true, enabled);

        HCHttpCallRequestSetRetryDelay(call, 500);
        uint32_t retryDelayInSeconds = 0;
        HCHttpCallRequestGetRetryDelay(call, &retryDelayInSeconds);

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

        HCHttpCallRequestSetHeader(call, "testHeader", "testValue");
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallRequestSetHeader(call, "testHeader", "testValue2");
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        const CHAR* t1 = nullptr;
        HCHttpCallRequestGetHeader(call, "testHeader", &t1);
        VERIFY_ARE_EQUAL_STR("testValue2", t1);
        HCHttpCallRequestGetHeader(call, "testHeader2", &t1);
        VERIFY_IS_NULL(t1);
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallRequestSetHeader(call, "testHeader", "testValue");
        HCHttpCallRequestSetHeader(call, "testHeader2", "testValue2");
        HCHttpCallRequestGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* hn0 = nullptr;
        const CHAR* hv0 = nullptr;
        const CHAR* hn1 = nullptr;
        const CHAR* hv1 = nullptr;
        HCHttpCallRequestGetHeaderAtIndex(call, 0, &hn0, &hv0);
        HCHttpCallRequestGetHeaderAtIndex(call, 1, &hn1, &hv1);
        VERIFY_ARE_EQUAL_STR("testHeader", hn0);
        VERIFY_ARE_EQUAL_STR("testValue", hv0);
        VERIFY_ARE_EQUAL_STR("testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR("testValue2", hv1);

        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestResponse)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponse);

        HCGlobalInitialize();
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        HCHttpCallResponseSetResponseString(call, "test1");
        const CHAR* t1 = nullptr;
        HCHttpCallResponseGetResponseString(call, &t1);
        VERIFY_ARE_EQUAL_STR("test1", t1);

        HCHttpCallResponseSetStatusCode(call, 200);
        uint32_t statusCode = 0;
        HCHttpCallResponseGetStatusCode(call, &statusCode);
        VERIFY_ARE_EQUAL(200, statusCode);

        HCHttpCallResponseSetNetworkErrorCode(call, HC_E_OUTOFMEMORY, 101);
        HC_RESULT errCode = HC_OK;
        uint32_t errorCode = 0;
        uint32_t platErrorCode = 0;
        HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrorCode);
        VERIFY_ARE_EQUAL(101, platErrorCode);
        VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);

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

        HCHttpCallResponseSetHeader(call, "testHeader", "testValue");
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallResponseSetHeader(call, "testHeader", "testValue2");
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        const CHAR* t1 = nullptr;
        HCHttpCallResponseGetHeader(call, "testHeader", &t1);
        VERIFY_ARE_EQUAL_STR("testValue2", t1);
        HCHttpCallResponseGetHeader(call, "testHeader2", &t1);
        VERIFY_IS_NULL(t1);
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(1, numHeaders);

        HCHttpCallResponseSetHeader(call, "testHeader", "testValue");
        HCHttpCallResponseSetHeader(call, "testHeader2", "testValue2");
        HCHttpCallResponseGetNumHeaders(call, &numHeaders);
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* hn0 = nullptr;
        const CHAR* hv0 = nullptr;
        const CHAR* hn1 = nullptr;
        const CHAR* hv1 = nullptr;
        HCHttpCallResponseGetHeaderAtIndex(call, 0, &hn0, &hv0);
        HCHttpCallResponseGetHeaderAtIndex(call, 1, &hn1, &hv1);
        VERIFY_ARE_EQUAL_STR("testHeader", hn0);
        VERIFY_ARE_EQUAL_STR("testValue", hv0);
        VERIFY_ARE_EQUAL_STR("testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR("testValue2", hv1);

        HCHttpCallCleanup(call);
        HCGlobalCleanup();
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
