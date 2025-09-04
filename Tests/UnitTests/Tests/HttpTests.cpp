// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include <httpClient/httpProvider.h>

#if HC_PLATFORM == HC_PLATFORM_GDK
#include <winhttp.h>
#endif

#pragma warning(disable:4389)

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

_Ret_maybenull_ _Post_writable_byte_size_(size) void* STDAPIVCALLTYPE MemAlloc(
    _In_ size_t size,
    _In_ HCMemoryType memoryType
    )   
{
    UNREFERENCED_PARAMETER(memoryType);
    g_memAllocCalled = true;
    return new (std::nothrow) int8_t[size];
}

void STDAPIVCALLTYPE MemFree(
    _In_ _Post_invalid_ void* pointer,
    _In_ HCMemoryType memoryType
    )
{
    UNREFERENCED_PARAMETER(memoryType);
    g_memFreeCalled = true;
    delete[] pointer;
}

static bool g_PerformCallbackCalled = false;
static void* g_PerformCallbackContext = nullptr;
static int g_performContext = 0;
static void CALLBACK PerformCallback(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* ctx,
    _In_opt_ HCPerformEnv /*env*/
    )
{
    UNREFERENCED_PARAMETER(call);
    UNREFERENCED_PARAMETER(asyncBlock);
    g_PerformCallbackCalled = true;
    g_PerformCallbackContext = ctx;
    XAsyncComplete(asyncBlock, S_OK, 0);
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

        VERIFY_ARE_EQUAL(S_OK, HCMemSetFunctions(&MemAlloc, &MemFree));

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

        HCMemAllocFunction memAllocFunc = nullptr;
        HCMemFreeFunction memFreeFunc = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCMemGetFunctions(&memAllocFunc, &memFreeFunc));
        VERIFY_IS_NOT_NULL(memAllocFunc);
        VERIFY_IS_NOT_NULL(memFreeFunc);

        VERIFY_ARE_EQUAL(S_OK, HCMemSetFunctions(nullptr, nullptr));

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

    DEFINE_TEST_CASE(TestInit)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestInit);

        VERIFY_IS_NULL(get_http_singleton());
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        VERIFY_IS_NOT_NULL(get_http_singleton());
        HCCleanup();
        VERIFY_IS_NULL(get_http_singleton());
    }

    DEFINE_TEST_CASE(TestPerformCallback)
    {
        DEFINE_TEST_CASE_PROPERTIES_FOCUS(TestPerformCallback);

        VERIFY_ARE_EQUAL(S_OK, HCSetHttpCallPerformFunction(&PerformCallback, &g_performContext));
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        g_PerformCallbackCalled = false;
        HCCallPerformFunction func = nullptr;
        void* ctx = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCGetHttpCallPerformFunction(&func, &ctx));
        VERIFY_IS_NOT_NULL(func);

        HCCallHandle call;
        HCHttpCallCreate(&call);
        VERIFY_ARE_EQUAL(false, g_PerformCallbackCalled);

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
            HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode));
            uint32_t statusCode = 0;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
            delete asyncBlock;
        };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, asyncBlock));

        while (true)
        {
            if (!XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0)) break;
        }
        VERIFY_ARE_EQUAL(true, XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0));
        VERIFY_ARE_EQUAL(true, g_PerformCallbackCalled);
        VERIFY_ARE_EQUAL(reinterpret_cast<uintptr_t>(&g_performContext), reinterpret_cast<uintptr_t>(g_PerformCallbackContext));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        XTaskQueueCloseHandle(queue);
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestSettings)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSettings);
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCTraceLevel level;

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Off));
        VERIFY_ARE_EQUAL(S_OK, HCSettingsGetTraceLevel(&level));
        VERIFY_ARE_EQUAL(HCTraceLevel::Off, level);

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Error));
        VERIFY_ARE_EQUAL(S_OK, HCSettingsGetTraceLevel(&level));
        VERIFY_ARE_EQUAL(HCTraceLevel::Error, level);

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Warning));
        VERIFY_ARE_EQUAL(S_OK, HCSettingsGetTraceLevel(&level));
        VERIFY_ARE_EQUAL(HCTraceLevel::Warning, level);

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Important));
        VERIFY_ARE_EQUAL(S_OK, HCSettingsGetTraceLevel(&level));
        VERIFY_ARE_EQUAL(HCTraceLevel::Important, level);

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Information));
        VERIFY_ARE_EQUAL(S_OK, HCSettingsGetTraceLevel(&level));
        VERIFY_ARE_EQUAL(HCTraceLevel::Information, level);

        VERIFY_ARE_EQUAL(S_OK, HCSettingsSetTraceLevel(HCTraceLevel::Verbose));
        VERIFY_ARE_EQUAL(S_OK, HCSettingsGetTraceLevel(&level));
        VERIFY_ARE_EQUAL(HCTraceLevel::Verbose, level);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetTimeoutWindow(nullptr, 1000));
        uint32_t timeout = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetTimeoutWindow(nullptr, &timeout));
        VERIFY_ARE_EQUAL(1000, timeout);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryDelay(nullptr, 500));
        uint32_t retryDelayInSeconds = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetRetryDelay(nullptr, &retryDelayInSeconds));

        HCCleanup();
    }

    DEFINE_TEST_CASE(TestCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCall);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));
        VERIFY_IS_NOT_NULL(call);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestRequest)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequest);
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "1", "2"));
        const CHAR* t1 = nullptr;
        const CHAR* t2 = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetUrl(call, &t1, &t2));
        VERIFY_ARE_EQUAL_STR("1", t1);
        VERIFY_ARE_EQUAL_STR("2", t2);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRequestBodyString(call, "4"));
        const BYTE* s1 = 0;
        uint32_t bodySize = 0;
        const CHAR* t3 = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetRequestBodyString(call, &t3));
        VERIFY_ARE_EQUAL_STR("4", t3);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetRequestBodyBytes(call, &s1, &bodySize));
        VERIFY_ARE_EQUAL(bodySize, 1);
        VERIFY_ARE_EQUAL(s1[0], '4');
        std::string s2( reinterpret_cast<char const*>(s1), bodySize);
        VERIFY_ARE_EQUAL_STR("4", s2.c_str());

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, true));
        bool retry = false;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetRetryAllowed(call, &retry));
        VERIFY_ARE_EQUAL(true, retry);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetTimeout(call, 2000));
        uint32_t timeout = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetTimeout(call, &timeout));
        VERIFY_ARE_EQUAL(2000, timeout);
                
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetTimeoutWindow(call, 1000));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetTimeoutWindow(call, &timeout));
        VERIFY_ARE_EQUAL(1000, timeout);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryDelay(call, 500));
        uint32_t retryDelayInSeconds = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetRetryDelay(call, &retryDelayInSeconds));

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        HCCleanup();
    }


    DEFINE_TEST_CASE(TestRequestHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaders);
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "testHeader", "testValue", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "testHeader", "testValue2", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        const CHAR* t1 = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeader(call, "testHeader", &t1));
        VERIFY_ARE_EQUAL_STR("testValue2", t1);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeader(call, "testHeader2", &t1));
        VERIFY_IS_NULL(t1);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "testHeader", "testValue", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "testHeader2", "testValue2", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* hn0 = nullptr;
        const CHAR* hv0 = nullptr;
        const CHAR* hn1 = nullptr;
        const CHAR* hv1 = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeaderAtIndex(call, 0, &hn0, &hv0));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeaderAtIndex(call, 1, &hn1, &hv1));
        VERIFY_ARE_EQUAL_STR("testHeader", hn0);
        VERIFY_ARE_EQUAL_STR("testValue", hv0);
        VERIFY_ARE_EQUAL_STR("testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR("testValue2", hv1);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestResponse)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponse);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));

        std::string s1 = "test1";
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetResponseBodyBytes(call, (uint8_t*)&s1[0], s1.length()));
        const CHAR* t1 = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetResponseString(call, &t1));
        VERIFY_ARE_EQUAL_STR("test1", t1);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetStatusCode(call, 200));
        uint32_t statusCode = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
        VERIFY_ARE_EQUAL(200, statusCode);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetNetworkErrorCode(call, E_OUTOFMEMORY, 101));
        HRESULT errCode = S_OK;
        uint32_t platErrorCode = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrorCode));
        VERIFY_ARE_EQUAL(101, platErrorCode);
        VERIFY_ARE_EQUAL(E_OUTOFMEMORY, errCode);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCloseHandle(call));
        HCCleanup();
        VERIFY_ARE_EQUAL(101, platErrorCode);
    }

    DEFINE_TEST_CASE(TestResponseHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseHeaders);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCCallHandle call;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        const char* str;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "testHeader", &str));
        VERIFY_IS_NULL(str);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "testHeader", &str));
        VERIFY_ARE_EQUAL_STR("testValue", str);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        const char* headerName;
        const char* headerValue;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeaderAtIndex(call, 0, &headerName, &headerValue));
        VERIFY_ARE_EQUAL_STR("testHeader", headerName);
        VERIFY_ARE_EQUAL_STR("testValue", headerValue);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseGetHeaderAtIndex(call, 1, &headerName, &headerValue));

        HCHttpCallCloseHandle(call);
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestMaxReceiveBufferSize)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMaxReceiveBufferSize);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCCallHandle call;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));

        // Test default value (should be 0)
        size_t bufferSize = 999; // Initialize to non-zero to verify it gets set
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetMaxReceiveBufferSize(call, &bufferSize));
        VERIFY_ARE_EQUAL(0U, bufferSize);

        // Test setting and getting valid buffer size
        const size_t testBufferSize = 64 * 1024; // 64KB
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetMaxReceiveBufferSize(call, testBufferSize));
        
        size_t retrievedBufferSize = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize));
        VERIFY_ARE_EQUAL(testBufferSize, retrievedBufferSize);

        // Test setting different buffer size
        const size_t newBufferSize = 128 * 1024; // 128KB
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetMaxReceiveBufferSize(call, newBufferSize));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize));
        VERIFY_ARE_EQUAL(newBufferSize, retrievedBufferSize);

        // Test invalid parameters - null call handle
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestSetMaxReceiveBufferSize(nullptr, testBufferSize));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetMaxReceiveBufferSize(nullptr, &bufferSize));

        // Test setting zero buffer size (reset to provider default)
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetMaxReceiveBufferSize(call, 0));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize));
        VERIFY_ARE_EQUAL(0U, retrievedBufferSize);

        // Test invalid parameters - null output parameter
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallRequestGetMaxReceiveBufferSize(call, nullptr));

        // Test that the buffer size persists through call operations
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetMaxReceiveBufferSize(call, newBufferSize));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "GET", "https://www.example.com"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetMaxReceiveBufferSize(call, &retrievedBufferSize));
        VERIFY_ARE_EQUAL(newBufferSize, retrievedBufferSize);

        // Test with a duplicate handle
        HCCallHandle duplicateCall = HCHttpCallDuplicateHandle(call);
        VERIFY_IS_NOT_NULL(duplicateCall);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetMaxReceiveBufferSize(duplicateCall, &retrievedBufferSize));
        VERIFY_ARE_EQUAL(newBufferSize, retrievedBufferSize);

        HCHttpCallCloseHandle(duplicateCall);
        HCHttpCallCloseHandle(call);
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestHttpProtocol)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestHttpProtocol);
        
        // Test to verify HTTP (non-HTTPS) requests work correctly
        // 
        // Background: This test was added to ensure HTTP protocol support remains functional
        // after changes to provider flag configuration that could potentially affect HTTP vs HTTPS handling.
        
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCCallHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallCreate(&call));

        // Use HTTP protocol to verify it works correctly
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetUrl(call, "GET", "http://example.com"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetRetryAllowed(call, false));
        
        // Create a mock response for the HTTP call
        HCMockCallHandle mockCall;
        VERIFY_ARE_EQUAL(S_OK, HCMockCallCreate(&mockCall));
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetStatusCode(mockCall, 200));
        std::string responseBody = "HTTP test successful";
        VERIFY_ARE_EQUAL(S_OK, HCMockResponseSetResponseBodyBytes(mockCall, (uint8_t*)responseBody.c_str(), (uint32_t)responseBody.length()));
        VERIFY_ARE_EQUAL(S_OK, HCMockAddMock(mockCall, "GET", "http://example.com", nullptr, 0));

        XAsyncBlock asyncBlock = {};
        asyncBlock.context = call;
        
        // Perform the HTTP call
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallPerformAsync(call, &asyncBlock));
        
        // Wait for completion
        HRESULT hr = XAsyncGetStatus(&asyncBlock, true);
        
        // Verify the HTTP request succeeded
        VERIFY_ARE_EQUAL(S_OK, hr);
        
        // Verify we got a successful HTTP response
        uint32_t statusCode = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetStatusCode(call, &statusCode));
        VERIFY_ARE_EQUAL(200, statusCode);

        HCHttpCallCloseHandle(call);
        HCCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
