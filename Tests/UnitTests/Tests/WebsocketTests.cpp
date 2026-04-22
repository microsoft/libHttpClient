// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include "WebSocket/hcwebsocket.h"
#include <httpClient/httpProvider.h>

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

bool g_PerformMessageCallbackCalled = false;
void STDAPIVCALLTYPE PerformMessageCallback(
    _In_ HCWebsocketHandle websocket,
    _In_z_ PCSTR incomingBodyString
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(incomingBodyString);
    g_PerformMessageCallbackCalled = true;
}

bool g_PerformCloseCallbackCalled = false;
void STDAPIVCALLTYPE PerformCloseCallback(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(closeStatus);
    g_PerformCloseCallbackCalled = true;
}

_Ret_maybenull_ _Post_writable_byte_size_(size) void* STDAPIVCALLTYPE MemAlloc(
    _In_ size_t size,
    _In_ HCMemoryType memoryType
    );
void STDAPIVCALLTYPE MemFree(
    _In_ _Post_invalid_ void* pointer,
    _In_ HCMemoryType memoryType
    );
extern bool g_memAllocCalled;
extern bool g_memFreeCalled;




void CALLBACK Internal_HCWebSocketMessage(
    _In_ HCWebsocketHandle websocket,
    _In_z_ PCSTR incomingBodyString,
    _In_ void* context
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(incomingBodyString);
    UNREFERENCED_PARAMETER(context);
}

void CALLBACK Internal_HCWebSocketBinaryMessage(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _In_ void* context
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(payloadBytes);
    UNREFERENCED_PARAMETER(payloadSize);
    UNREFERENCED_PARAMETER(context);
}

void CALLBACK Internal_HCWebSocketCloseEvent(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_ void* context
)
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(closeStatus);
    UNREFERENCED_PARAMETER(context);
}




bool g_HCWebSocketConnect_Called = false;
HRESULT CALLBACK Test_Internal_HCWebSocketConnectAsync(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
    )
{
    UNREFERENCED_PARAMETER(uri);
    UNREFERENCED_PARAMETER(subProtocol);
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(asyncBlock);
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(env);

    // TODO bug - inconsistent behavior for websocket providers: Connect calls XAsyncBegin before
    // invoke the client handler, Send does not.
    return XAsyncBegin(asyncBlock, nullptr, nullptr, __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
            {
                g_HCWebSocketConnect_Called = true;
                XAsyncComplete(data->async, S_OK, 0);
                return S_OK;
            }
            default: return S_OK;
            }
        });
}

bool g_HCWebSocketConnectAndClose_Called = false;
HRESULT CALLBACK Test_Internal_HCWebSocketConnectAsyncAndClose(
    _In_z_ PCSTR uri,
    _In_z_ PCSTR subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv env
    )
{
    UNREFERENCED_PARAMETER(uri);
    UNREFERENCED_PARAMETER(subProtocol);
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(env);

    return XAsyncBegin(asyncBlock, websocket, nullptr, __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            auto websocket = static_cast<HCWebsocketHandle>(data->context);

            switch (op)
            {
            case XAsyncOp::Begin:
            {
                g_HCWebSocketConnectAndClose_Called = true;
                RETURN_IF_FAILED(XTaskQueueSubmitCallback(data->async->queue, XTaskQueuePort::Work, websocket,
                    [](void* context, bool canceled)
                    {
                        if (canceled)
                        {
                            return;
                        }

                        auto websocket = static_cast<HCWebsocketHandle>(context);
                        HCWebSocketCloseEventFunction closeFunc = nullptr;
                        void* closeContext = nullptr;
                        HRESULT hr = HCWebSocketGetEventFunctions(websocket, nullptr, nullptr, &closeFunc, &closeContext);
                        if (SUCCEEDED(hr) && closeFunc != nullptr)
                        {
                            closeFunc(websocket, HCWebSocketCloseStatus::AbnormalClose, closeContext);
                        }
                    }));

                XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                return S_OK;
            }
            case XAsyncOp::GetResult:
            {
                RETURN_HR_IF(E_NOT_SUFFICIENT_BUFFER, data->bufferSize < sizeof(WebSocketCompletionResult));

                auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
                ZeroMemory(result, sizeof(WebSocketCompletionResult));
                result->errorCode = S_OK;
                result->websocket = websocket;
                return S_OK;
            }
            default:
                return S_OK;
            }
        });
}

class TestWebSocketConnectAndCloseProvider : public IWebSocketProvider
{
public:
    HRESULT ConnectAsync(
        xbox::httpclient::String const& uri,
        xbox::httpclient::String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async
    ) noexcept override
    {
        UNREFERENCED_PARAMETER(uri);
        UNREFERENCED_PARAMETER(subprotocol);

        m_websocket = websocketHandle;
        return XAsyncBegin(async, this, nullptr, __FUNCTION__,
            [](XAsyncOp op, const XAsyncProviderData* data)
            {
                auto provider = static_cast<TestWebSocketConnectAndCloseProvider*>(data->context);

                switch (op)
                {
                case XAsyncOp::Begin:
                {
                    provider->m_connectCalled = true;
                    RETURN_IF_FAILED(XTaskQueueSubmitCallback(data->async->queue, XTaskQueuePort::Work, provider->m_websocket,
                        [](void* context, bool canceled)
                        {
                            if (canceled)
                            {
                                return;
                            }

                            auto websocket = static_cast<HCWebsocketHandle>(context);
                            HCWebSocketCloseEventFunction closeFunc = nullptr;
                            void* closeContext = nullptr;
                            HRESULT hr = HCWebSocketGetEventFunctions(websocket, nullptr, nullptr, &closeFunc, &closeContext);
                            if (SUCCEEDED(hr) && closeFunc != nullptr)
                            {
                                closeFunc(websocket, HCWebSocketCloseStatus::AbnormalClose, closeContext);
                            }
                        }));

                    XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                    return S_OK;
                }
                case XAsyncOp::GetResult:
                {
                    RETURN_HR_IF(E_NOT_SUFFICIENT_BUFFER, data->bufferSize < sizeof(WebSocketCompletionResult));

                    auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
                    ZeroMemory(result, sizeof(WebSocketCompletionResult));
                    result->errorCode = S_OK;
                    result->websocket = provider->m_websocket;
                    return S_OK;
                }
                default:
                    return S_OK;
                }
            });
    }

    HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async
    ) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(message);
        UNREFERENCED_PARAMETER(async);
        return E_UNEXPECTED;
    }

    HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* async
    ) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(payloadBytes);
        UNREFERENCED_PARAMETER(payloadSize);
        UNREFERENCED_PARAMETER(async);
        return E_UNEXPECTED;
    }

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus
    ) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(closeStatus);
        return E_UNEXPECTED;
    }

    bool m_connectCalled{ false };

private:
    HCWebsocketHandle m_websocket{ nullptr };
};

bool g_HCWebSocketSendMessage_Called = false;
HRESULT CALLBACK Test_Internal_HCWebSocketSendMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_z_ PCSTR message,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(asyncBlock);
    UNREFERENCED_PARAMETER(context);

    return XAsyncBegin(asyncBlock, nullptr, nullptr, __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
            {
                g_HCWebSocketSendMessage_Called = true;
                XAsyncComplete(data->async, S_OK, 0);
                return S_OK;
            }
            default: return S_OK;
            }
        });
}

bool g_HCWebSocketSendBinaryMessage_Called = false;
HRESULT CALLBACK Test_Internal_HCWebSocketSendBinaryMessageAsync(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
)
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(payloadBytes);
    UNREFERENCED_PARAMETER(payloadSize);
    UNREFERENCED_PARAMETER(asyncBlock);
    UNREFERENCED_PARAMETER(context);

    return XAsyncBegin(asyncBlock, nullptr, nullptr, __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
            {
                g_HCWebSocketSendBinaryMessage_Called = true;
                XAsyncComplete(data->async, S_OK, 0);
                return S_OK;
            }
            default: return S_OK;
            }
        });
    return S_OK;
}

bool g_HCWebSocketDisconnect_Called = false;
HRESULT CALLBACK Test_Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(context);

    g_HCWebSocketDisconnect_Called = true;
    
    // Simulate proper disconnect by calling the close callback
    // This is needed for cleanup to work properly
    HCWebSocketCloseEventFunction closeFunc = nullptr;
    void* closeContext = nullptr;
    HRESULT hr = HCWebSocketGetEventFunctions(websocket, nullptr, nullptr, &closeFunc, &closeContext);
    if (SUCCEEDED(hr) && closeFunc != nullptr)
    {
        closeFunc(websocket, closeStatus, closeContext);
    }
    
    return S_OK;
}

DEFINE_TEST_CLASS(WebsocketTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(WebsocketTests);

    DEFINE_TEST_CASE(TestGlobalCallbacks)
    {
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        g_PerformMessageCallbackCalled = false;
        g_PerformCloseCallbackCalled = false;

        HCWebSocketMessageFunction messageFunc = nullptr;
        HCWebSocketBinaryMessageFunction binaryMessageFunc = nullptr;
        HCWebSocketCloseEventFunction closeFunc = nullptr;
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetEventFunctions(nullptr, &messageFunc, &binaryMessageFunc, &closeFunc, nullptr));
        VERIFY_IS_NULL(messageFunc);
        VERIFY_IS_NULL(binaryMessageFunc);
        VERIFY_IS_NULL(closeFunc);

        HCWebsocketHandle websocket;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, Internal_HCWebSocketMessage, Internal_HCWebSocketBinaryMessage, Internal_HCWebSocketCloseEvent, nullptr));
        VERIFY_IS_NOT_NULL(websocket);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetEventFunctions(websocket, &messageFunc, &binaryMessageFunc, &closeFunc, nullptr));
        VERIFY_IS_NOT_NULL(messageFunc);
        VERIFY_IS_NOT_NULL(binaryMessageFunc);
        VERIFY_IS_NOT_NULL(closeFunc);
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestCloseHandles)
    {
        g_memAllocCalled = false;
        g_memFreeCalled = false;
        VERIFY_ARE_EQUAL(S_OK, HCMemSetFunctions(&MemAlloc, &MemFree));
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCWebsocketHandle websocket;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));

        HCWebsocketHandle websocket2;
        websocket2 = HCWebSocketDuplicateHandle(websocket);
        VERIFY_IS_NOT_NULL(websocket2);
        g_memFreeCalled = false;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket2));
        VERIFY_ARE_EQUAL(false, g_memFreeCalled);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        VERIFY_ARE_EQUAL(true, g_memFreeCalled);

        HCCleanup();
    }

    DEFINE_TEST_CASE(TestConnect)
    {
        HCWebSocketConnectFunction websocketConnectFunc = nullptr;
        HCWebSocketSendMessageFunction websocketSendMessageFunc = nullptr;
        HCWebSocketSendBinaryMessageFunction websocketSendBinaryMessageFunc = nullptr;
        HCWebSocketDisconnectFunction websocketDisconnectFunc = nullptr;
        void* context = nullptr;

        VERIFY_ARE_EQUAL(S_OK, HCGetWebSocketFunctions(&websocketConnectFunc, &websocketSendMessageFunc, &websocketSendBinaryMessageFunc, &websocketDisconnectFunc, &context));
        VERIFY_IS_NULL(websocketConnectFunc);
        VERIFY_IS_NULL(websocketSendMessageFunc);
        VERIFY_IS_NULL(websocketDisconnectFunc);

        VERIFY_ARE_EQUAL(S_OK, HCSetWebSocketFunctions(Test_Internal_HCWebSocketConnectAsync, Test_Internal_HCWebSocketSendMessageAsync, Test_Internal_HCWebSocketSendBinaryMessageAsync, Test_Internal_HCWebSocketDisconnect, nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCGetWebSocketFunctions(&websocketConnectFunc, &websocketSendMessageFunc, &websocketSendBinaryMessageFunc, &websocketDisconnectFunc, &context));
        VERIFY_IS_NOT_NULL(websocketConnectFunc);
        VERIFY_IS_NOT_NULL(websocketSendMessageFunc);
        VERIFY_IS_NOT_NULL(websocketDisconnectFunc);

        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCWebsocketHandle websocket;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        VERIFY_IS_NOT_NULL(websocket);

        const CHAR* proxy = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetProxyUri(websocket, &proxy));
        VERIFY_ARE_EQUAL_STR("", proxy);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyUri(websocket, "1234"));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetProxyUri(websocket, &proxy));
        VERIFY_ARE_EQUAL_STR("1234", proxy);

        VERIFY_ARE_EQUAL(false, g_HCWebSocketConnect_Called);
        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("test", "subProtoTest", websocket, &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketConnect_Called);

        ZeroMemory(&asyncBlock, sizeof(XAsyncBlock));
        VERIFY_ARE_EQUAL(false, g_HCWebSocketSendMessage_Called);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSendMessageAsync(websocket, "test", &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketSendMessage_Called);

        VERIFY_ARE_EQUAL(false, g_HCWebSocketDisconnect_Called);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketDisconnect_Called);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestConnectFailsWhenDisconnectedDuringCompletion)
    {
#ifdef UNITTEST_TE
        return;
#else
        TestWebSocketConnectAndCloseProvider provider;
        auto websocket = std::make_shared<WebSocket>(1, provider);

        XAsyncBlock asyncBlock{};
        VERIFY_SUCCEEDED(XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &asyncBlock.queue));

        VERIFY_ARE_EQUAL(S_OK, websocket->ConnectAsync(http_internal_string{ "test" }, http_internal_string{ "subProtoTest" }, &asyncBlock));
        WebSocketCompletionResult connectResult{};
        XAsyncBlock sendAsyncBlock{};
        HRESULT connectStatus = E_PENDING;
        HRESULT getConnectResultHr = E_PENDING;
        HRESULT sendHr = S_OK;
        HRESULT disconnectHr = S_OK;

        for (uint32_t attempt = 0; attempt < 8 && connectStatus == E_PENDING; ++attempt)
        {
            auto timeout = attempt == 0 ? 100u : 0u;
            while (XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Work, timeout)) {}
            while (XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Completion, timeout)) {}
            connectStatus = XAsyncGetStatus(&asyncBlock, false);
        }

        if (connectStatus == E_FAIL)
        {
            getConnectResultHr = HCGetWebSocketConnectResult(&asyncBlock, &connectResult);
        }
        else if (connectStatus == E_PENDING)
        {
            XAsyncCancel(&asyncBlock);
            while (XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Work, 0)) {}
            while (XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Completion, 0)) {}
            connectStatus = XAsyncGetStatus(&asyncBlock, false);
        }

        getConnectResultHr = HCGetWebSocketConnectResult(&asyncBlock, &connectResult);
        sendHr = websocket->SendAsync("test", &sendAsyncBlock);
        disconnectHr = websocket->Disconnect();

        while (XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Work, 0)) {}
        while (XTaskQueueDispatch(asyncBlock.queue, XTaskQueuePort::Completion, 0)) {}
        XTaskQueueCloseHandle(asyncBlock.queue);
        websocket.reset();

        VERIFY_ARE_EQUAL(true, provider.m_connectCalled);
        VERIFY_IS_TRUE(FAILED(connectStatus));
        VERIFY_IS_TRUE(FAILED(getConnectResultHr));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, sendHr);
        VERIFY_ARE_EQUAL(E_UNEXPECTED, disconnectHr);
#endif
    }


    DEFINE_TEST_CASE(TestRequestHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaders);
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCWebsocketHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&call, nullptr, nullptr, nullptr, nullptr));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetHeader(call, "testHeader", "testValue2"));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        const CHAR* t1 = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetHeader(call, "testHeader", &t1));
        VERIFY_ARE_EQUAL_STR("testValue2", t1);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetHeader(call, "testHeader2", &t1));
        VERIFY_IS_NULL(t1);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1, numHeaders);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetHeader(call, "testHeader", "testValue"));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetHeader(call, "testHeader2", "testValue2"));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* hn0 = nullptr;
        const CHAR* hv0 = nullptr;
        const CHAR* hn1 = nullptr;
        const CHAR* hv1 = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetHeaderAtIndex(call, 0, &hn0, &hv0));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetHeaderAtIndex(call, 1, &hn1, &hv1));
        VERIFY_ARE_EQUAL_STR("testHeader", hn0);
        VERIFY_ARE_EQUAL_STR("testValue", hv0);
        VERIFY_ARE_EQUAL_STR("testHeader2", hn1);
        VERIFY_ARE_EQUAL_STR("testValue2", hv1);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(call));
        HCCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
