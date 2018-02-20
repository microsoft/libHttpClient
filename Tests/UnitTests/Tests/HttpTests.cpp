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

bool g_memAllocCalled = false;
bool g_memFreeCalled = false;

_Ret_maybenull_ _Post_writable_byte_size_(size) void* HC_CALLING_CONV MemAlloc(
    _In_ size_t size,
    _In_ HC_MEMORY_TYPE memoryType
    )   
{
    g_memAllocCalled = true;
    return new (std::nothrow) int8_t[size];
}

void HC_CALLING_CONV MemFree(
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

        VERIFY_ARE_EQUAL(HC_OK, HCMemSetFunctions(&MemAlloc, &MemFree));

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
        VERIFY_ARE_EQUAL(HC_OK, HCMemGetFunctions(&memAllocFunc, &memFreeFunc));
        VERIFY_IS_NOT_NULL(memAllocFunc);
        VERIFY_IS_NOT_NULL(memFreeFunc);

        VERIFY_ARE_EQUAL(HC_OK, HCMemSetFunctions(nullptr, nullptr));

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

        VERIFY_IS_NULL(get_http_singleton(false));
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        VERIFY_IS_NOT_NULL(get_http_singleton(false));
        HCGlobalCleanup();
        VERIFY_IS_NULL(get_http_singleton(false));
    }

    DEFINE_TEST_CASE(TestGlobalPerformCallback)
    {
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        g_PerformCallbackCalled = false;
        HC_HTTP_CALL_PERFORM_FUNC func = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalGetHttpCallPerformFunction(&func));
        VERIFY_IS_NOT_NULL(func);

        HCGlobalSetHttpCallPerformFunction(&PerformCallback);
        HC_CALL_HANDLE call;
        HCHttpCallCreate(&call);
        VERIFY_ARE_EQUAL(false, g_PerformCallbackCalled);
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallPerform(call, nullptr, HC_SUBSYSTEM_ID_GAME, 0, nullptr,
            [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
            {
                HC_RESULT errCode = HC_OK;
                uint32_t platErrCode = 0;
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
                uint32_t statusCode = 0;
                VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            }));
        VERIFY_ARE_EQUAL(HC_OK, HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME));
        VERIFY_ARE_EQUAL(true, g_PerformCallbackCalled);
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestSettings)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSettings);
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());

        HC_LOG_LEVEL level;

        VERIFY_ARE_EQUAL(HC_OK, HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_OFF));
        VERIFY_ARE_EQUAL(HC_OK, HCSettingsGetLogLevel(&level));
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_OFF, level);

        VERIFY_ARE_EQUAL(HC_OK, HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_ERROR));
        VERIFY_ARE_EQUAL(HC_OK, HCSettingsGetLogLevel(&level));
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_ERROR, level);

        VERIFY_ARE_EQUAL(HC_OK, HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_WARNING));
        VERIFY_ARE_EQUAL(HC_OK, HCSettingsGetLogLevel(&level));
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_WARNING, level);

        VERIFY_ARE_EQUAL(HC_OK, HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_IMPORTANT));
        VERIFY_ARE_EQUAL(HC_OK, HCSettingsGetLogLevel(&level));
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_IMPORTANT, level);

        VERIFY_ARE_EQUAL(HC_OK, HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_INFORMATION));
        VERIFY_ARE_EQUAL(HC_OK, HCSettingsGetLogLevel(&level));
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_INFORMATION, level);

        VERIFY_ARE_EQUAL(HC_OK, HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_VERBOSE));
        VERIFY_ARE_EQUAL(HC_OK, HCSettingsGetLogLevel(&level));
        VERIFY_ARE_EQUAL(HC_LOG_LEVEL::LOG_VERBOSE, level);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetTimeoutWindow(nullptr, 1000));
        uint32_t timeout = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetTimeoutWindow(nullptr, &timeout));
        VERIFY_ARE_EQUAL(1000, timeout);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRetryDelay(nullptr, 500));
        uint32_t retryDelayInSeconds = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetRetryDelay(nullptr, &retryDelayInSeconds));

        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCall);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestRequest)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequest);
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        const CHAR* t1 = nullptr;
        const CHAR* t2 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetUrl(call, &t1, &t2));
        VERIFY_ARE_EQUAL_STR("1", t1);
        VERIFY_ARE_EQUAL_STR("2", t2);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRequestBodyString(call, "4"));
        const BYTE* s1 = 0;
        uint32_t bodySize = 0;
        const CHAR* t3 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetRequestBodyString(call, &t3));
        VERIFY_ARE_EQUAL_STR("4", t3);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetRequestBodyBytes(call, &s1, &bodySize));
        VERIFY_ARE_EQUAL(bodySize, 1);
        VERIFY_ARE_EQUAL(s1[0], '4');
        std::string s2( reinterpret_cast<char const*>(s1), bodySize);
        VERIFY_ARE_EQUAL_STR("4", s2.c_str());

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRetryAllowed(call, true));
        bool retry = false;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetRetryAllowed(call, &retry));
        VERIFY_ARE_EQUAL(true, retry);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetTimeout(call, 2000));
        uint32_t timeout = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetTimeout(call, &timeout));
        VERIFY_ARE_EQUAL(2000, timeout);
                
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetTimeoutWindow(call, 1000));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetTimeoutWindow(call, &timeout));
        VERIFY_ARE_EQUAL(1000, timeout);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetRetryDelay(call, 500));
        uint32_t retryDelayInSeconds = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetRetryDelay(call, &retryDelayInSeconds));

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
        HCGlobalCleanup();
    }


    DEFINE_TEST_CASE(TestRequestHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaders);
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetHeader(call, "testHeader", "testValue2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        const CHAR* t1 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetHeader(call, "testHeader", &t1));
        VERIFY_ARE_EQUAL_STR("testValue2", t1);
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetHeader(call, "testHeader2", &t1));
        VERIFY_IS_NULL(t1);
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestSetHeader(call, "testHeader2", "testValue2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* hn0 = nullptr;
        const CHAR* hv0 = nullptr;
        const CHAR* hn1 = nullptr;
        const CHAR* hv1 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetHeaderAtIndex(call, 0, &hn0, &hv0));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallRequestGetHeaderAtIndex(call, 1, &hn1, &hv1));
        VERIFY_ARE_EQUAL_STR("testHeader", hn0);
        VERIFY_ARE_EQUAL_STR("testValue", hv0);
        VERIFY_ARE_EQUAL_STR("testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR("testValue2", hv1);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestResponse)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponse);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        HC_CALL_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCreate(&call));

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetResponseString(call, "test1"));
        const CHAR* t1 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetResponseString(call, &t1));
        VERIFY_ARE_EQUAL_STR("test1", t1);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetStatusCode(call, 200));
        uint32_t statusCode = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
        VERIFY_ARE_EQUAL(200, statusCode);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetNetworkErrorCode(call, HC_E_OUTOFMEMORY, 101));
        HC_RESULT errCode = HC_OK;
        uint32_t errorCode = 0;
        uint32_t platErrorCode = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrorCode));
        VERIFY_ARE_EQUAL(101, platErrorCode);
        VERIFY_ARE_EQUAL(HC_E_OUTOFMEMORY, errCode);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestResponseHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        HC_CALL_HANDLE call = nullptr;
        HCHttpCallCreate(&call);

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetHeader(call, "testHeader", "testValue2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        const CHAR* t1 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetHeader(call, "testHeader", &t1));
        VERIFY_ARE_EQUAL_STR("testValue2", t1);
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetHeader(call, "testHeader2", &t1));
        VERIFY_IS_NULL(t1);
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseSetHeader(call, "testHeader2", "testValue2"));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* hn0 = nullptr;
        const CHAR* hv0 = nullptr;
        const CHAR* hn1 = nullptr;
        const CHAR* hv1 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetHeaderAtIndex(call, 0, &hn0, &hv0));
        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallResponseGetHeaderAtIndex(call, 1, &hn1, &hv1));
        VERIFY_ARE_EQUAL_STR("testHeader", hn0);
        VERIFY_ARE_EQUAL_STR("testValue", hv0);
        VERIFY_ARE_EQUAL_STR("testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR("testValue2", hv1);

        VERIFY_ARE_EQUAL(HC_OK, HCHttpCallCloseHandle(call));
        HCGlobalCleanup();
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
