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

bool g_PerformMessageCallbackCalled = false;
void HC_CALLING_CONV PerformMessageCallback(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR incomingBodyString
    )
{
    g_PerformMessageCallbackCalled = true;
}

bool g_PerformCloseCallbackCalled = false;
void HC_CALLING_CONV PerformCloseCallback(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CLOSE_STATUS closeStatus
    )
{
    g_PerformCloseCallbackCalled = true;
}

_Ret_maybenull_ _Post_writable_byte_size_(size) void* HC_CALLING_CONV MemAlloc(
    _In_ size_t size,
    _In_ HC_MEMORY_TYPE memoryType
    );
void HC_CALLING_CONV MemFree(
    _In_ _Post_invalid_ void* pointer,
    _In_ HC_MEMORY_TYPE memoryType
    );
extern bool g_memAllocCalled;
extern bool g_memFreeCalled;




void Internal_HCWebSocketMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR incomingBodyString
    )
{
}

void Internal_HCWebSocketCloseEvent(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CLOSE_STATUS closeStatus
)
{
}




bool g_HCWebSocketConnect_Called = false;
HC_RESULT Test_Internal_HCWebSocketConnect(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ AsyncBlock* asyncBlock
    )
{
    g_HCWebSocketConnect_Called = true;
    return HC_OK;
}

bool g_HCWebSocketSendMessage_Called = false;
HC_RESULT Test_Internal_HCWebSocketSendMessage(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_z_ PCSTR message,
    _In_ AsyncBlock* asyncBlock
    )
{
    g_HCWebSocketSendMessage_Called = true;
    return HC_OK;
}

bool g_HCWebSocketDisconnect_Called = false;
HC_RESULT Test_Internal_HCWebSocketDisconnect(
    _In_ HC_WEBSOCKET_HANDLE websocket,
    _In_ HC_WEBSOCKET_CLOSE_STATUS closeStatus
    )
{
    g_HCWebSocketDisconnect_Called = true;
    return HC_OK;
}

DEFINE_TEST_CLASS(WebsocketTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(WebsocketTests);

    DEFINE_TEST_CASE(TestGlobalCallbacks)
    {
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        //g_PerformMessageCallbackCalled = false;
        //g_PerformCloseCallbackCalled = false;

        HC_WEBSOCKET_MESSAGE_FUNC messageFunc = nullptr;
        HC_WEBSOCKET_CLOSE_EVENT_FUNC closeFunc = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetFunctions(&messageFunc, &closeFunc));
        VERIFY_IS_NULL(messageFunc);
        VERIFY_IS_NULL(closeFunc);

        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketSetFunctions(Internal_HCWebSocketMessage, Internal_HCWebSocketCloseEvent));
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetFunctions(&messageFunc, &closeFunc));
        VERIFY_IS_NOT_NULL(messageFunc);
        VERIFY_IS_NOT_NULL(closeFunc);
        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestCloseHandles)
    {
        g_memAllocCalled = false;
        g_memFreeCalled = false;
        VERIFY_ARE_EQUAL(HC_OK, HCMemSetFunctions(&MemAlloc, &MemFree));
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());

        HC_WEBSOCKET_HANDLE websocket;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketCreate(&websocket));

        HC_WEBSOCKET_HANDLE websocket2;
        websocket2 = HCWebSocketDuplicateHandle(websocket);
        VERIFY_IS_NOT_NULL(websocket2);
        g_memFreeCalled = false;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketCloseHandle(websocket2));
        VERIFY_ARE_EQUAL(false, g_memFreeCalled);
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketCloseHandle(websocket));
        VERIFY_ARE_EQUAL(true, g_memFreeCalled);

        HCGlobalCleanup();
    }

    DEFINE_TEST_CASE(TestConnect)
    {
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());

        HC_WEBSOCKET_CONNECT_FUNC websocketConnectFunc = nullptr;
        HC_WEBSOCKET_SEND_MESSAGE_FUNC websocketSendMessageFunc = nullptr;
        HC_WEBSOCKET_DISCONNECT_FUNC websocketDisconnectFunc = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalGetWebSocketFunctions(&websocketConnectFunc, &websocketSendMessageFunc, &websocketDisconnectFunc));
        VERIFY_IS_NOT_NULL(websocketConnectFunc);
        VERIFY_IS_NOT_NULL(websocketSendMessageFunc);
        VERIFY_IS_NOT_NULL(websocketDisconnectFunc);

        VERIFY_ARE_EQUAL(HC_OK, HCGlobalSetWebSocketFunctions(Test_Internal_HCWebSocketConnect, Test_Internal_HCWebSocketSendMessage, Test_Internal_HCWebSocketDisconnect));
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalGetWebSocketFunctions(&websocketConnectFunc, &websocketSendMessageFunc, &websocketDisconnectFunc));
        VERIFY_IS_NOT_NULL(websocketConnectFunc);
        VERIFY_IS_NOT_NULL(websocketSendMessageFunc);
        VERIFY_IS_NOT_NULL(websocketDisconnectFunc);

        HC_WEBSOCKET_HANDLE websocket;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketCreate(&websocket));
        VERIFY_IS_NOT_NULL(websocket);

        const CHAR* proxy = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetProxyUri(websocket, &proxy));
        VERIFY_ARE_EQUAL_STR("", proxy);
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketSetProxyUri(websocket, "1234"));
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetProxyUri(websocket, &proxy));
        VERIFY_ARE_EQUAL_STR("1234", proxy);

        VERIFY_ARE_EQUAL(false, g_HCWebSocketConnect_Called);
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketConnect("test", "subProtoTest", websocket, nullptr));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketConnect_Called);

        VERIFY_ARE_EQUAL(false, g_HCWebSocketSendMessage_Called);
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketSendMessage(websocket, "test", nullptr));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketSendMessage_Called);

        VERIFY_ARE_EQUAL(false, g_HCWebSocketDisconnect_Called);
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketDisconnect_Called);

        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketCloseHandle(websocket));
        HCGlobalCleanup();
    }


    DEFINE_TEST_CASE(TestRequestHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaders);
        VERIFY_ARE_EQUAL(HC_OK, HCGlobalInitialize());
        HC_WEBSOCKET_HANDLE call = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketCreate(&call));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketSetHeader(call, "testHeader", "testValue2"));
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        const CHAR* t1 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetHeader(call, "testHeader", &t1));
        VERIFY_ARE_EQUAL_STR("testValue2", t1);
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetHeader(call, "testHeader2", &t1));
        VERIFY_IS_NULL(t1);
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketSetHeader(call, "testHeader2", "testValue2"));
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* hn0 = nullptr;
        const CHAR* hv0 = nullptr;
        const CHAR* hn1 = nullptr;
        const CHAR* hv1 = nullptr;
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetHeaderAtIndex(call, 0, &hn0, &hv0));
        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketGetHeaderAtIndex(call, 1, &hn1, &hv1));
        VERIFY_ARE_EQUAL_STR("testHeader", hn0);
        VERIFY_ARE_EQUAL_STR("testValue", hv0);
        VERIFY_ARE_EQUAL_STR("testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR("testValue2", hv1);

        VERIFY_ARE_EQUAL(HC_OK, HCWebSocketCloseHandle(call));
        HCGlobalCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
