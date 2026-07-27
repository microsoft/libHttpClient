// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include "WebSocket/hcwebsocket.h"
#include "../../Source/WebSocket/Websocketpp/websocketpp_websocket.h"
#include <httpClient/httpClient.h>
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

void CALLBACK Internal_HCWebSocketBinaryMessageFragment(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _In_ bool isLastFragment,
    _In_ void* context
    )
{
    UNREFERENCED_PARAMETER(websocket);
    UNREFERENCED_PARAMETER(payloadBytes);
    UNREFERENCED_PARAMETER(payloadSize);
    UNREFERENCED_PARAMETER(isLastFragment);
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
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(env);

    // TODO bug - inconsistent behavior for websocket providers: Connect calls XAsyncBegin before
    // invoke the client handler, Send does not.
    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            auto websocket = static_cast<HCWebsocketHandle>(data->context);

            switch (op)
            {
            case XAsyncOp::Begin:
            {
                g_HCWebSocketConnect_Called = true;
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

    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
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

bool g_HCWebSocketConnectWithFailureHeaders_Called = false;
HRESULT CALLBACK Test_Internal_HCWebSocketConnectAsyncFailsWithResponseHeaders(
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

    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            auto websocket = static_cast<HCWebsocketHandle>(data->context);

            switch (op)
            {
            case XAsyncOp::Begin:
            {
                g_HCWebSocketConnectWithFailureHeaders_Called = true;

                HttpHeaders responseHeaders;
                responseHeaders["failureHeader"] = "failureValue";
                RETURN_IF_FAILED(websocket->websocket->SetResponseHeaders(std::move(responseHeaders)));

                XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                return S_OK;
            }
            case XAsyncOp::GetResult:
            {
                RETURN_HR_IF(E_NOT_SUFFICIENT_BUFFER, data->bufferSize < sizeof(WebSocketCompletionResult));

                auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
                ZeroMemory(result, sizeof(WebSocketCompletionResult));
                result->websocket = websocket;
                result->errorCode = E_ACCESSDENIED;
                result->platformErrorCode = 1234;
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
        return XAsyncBegin(async, this, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
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
HCWebSocketCloseStatus g_HCWebSocketDisconnect_CloseStatus = HCWebSocketCloseStatus::Normal;
HRESULT CALLBACK Test_Internal_HCWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
    )
{
    UNREFERENCED_PARAMETER(context);

    g_HCWebSocketDisconnect_Called = true;
    g_HCWebSocketDisconnect_CloseStatus = closeStatus;
    
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

class UnsupportedOptionsTestProvider final : public IWebSocketProvider
{
public:
    HRESULT ConnectAsync(
        xbox::httpclient::String const& uri,
        xbox::httpclient::String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async) noexcept override
    {
        UNREFERENCED_PARAMETER(uri);
        UNREFERENCED_PARAMETER(subprotocol);
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(async);
        return E_NOTIMPL;
    }

    HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(message);
        UNREFERENCED_PARAMETER(async);
        return E_NOTIMPL;
    }

    HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* async) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(payloadBytes);
        UNREFERENCED_PARAMETER(payloadSize);
        UNREFERENCED_PARAMETER(async);
        return E_NOTIMPL;
    }

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(closeStatus);
        return E_NOTIMPL;
    }

    HRESULT OptionsResult(HCWebSocketOptions options) const noexcept override
    {
        UNREFERENCED_PARAMETER(options);
        return E_NOT_SUPPORTED;
    }
};

class ConfigurableCompressionTestProvider final : public IWebSocketProvider
{
public:
    HRESULT ConnectAsync(
        xbox::httpclient::String const& uri,
        xbox::httpclient::String const& subprotocol,
        HCWebsocketHandle websocketHandle,
        XAsyncBlock* async) noexcept override
    {
        UNREFERENCED_PARAMETER(uri);
        UNREFERENCED_PARAMETER(subprotocol);
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(async);
        return E_NOTIMPL;
    }

    HRESULT SendAsync(
        HCWebsocketHandle websocketHandle,
        const char* message,
        XAsyncBlock* async) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(message);
        UNREFERENCED_PARAMETER(async);
        return E_NOTIMPL;
    }

    HRESULT SendBinaryAsync(
        HCWebsocketHandle websocketHandle,
        const uint8_t* payloadBytes,
        uint32_t payloadSize,
        XAsyncBlock* async) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(payloadBytes);
        UNREFERENCED_PARAMETER(payloadSize);
        UNREFERENCED_PARAMETER(async);
        return E_NOTIMPL;
    }

    HRESULT Disconnect(
        HCWebsocketHandle websocketHandle,
        HCWebSocketCloseStatus closeStatus) noexcept override
    {
        UNREFERENCED_PARAMETER(websocketHandle);
        UNREFERENCED_PARAMETER(closeStatus);
        return E_NOTIMPL;
    }

    HRESULT OptionsResult(HCWebSocketOptions options) const noexcept override
    {
        auto const noContextTakeoverMask =
            HCWebSocketOptions::CompressionServerNoContextTakeover |
            HCWebSocketOptions::CompressionClientNoContextTakeover;

        if ((options & HCWebSocketOptions::LegacySemantics) != HCWebSocketOptions::None)
        {
            return S_OK;
        }

        if ((options & ~(HCWebSocketOptions::RequestCompression | noContextTakeoverMask)) != HCWebSocketOptions::None)
        {
            return E_NOT_SUPPORTED;
        }

        if ((options & noContextTakeoverMask) != HCWebSocketOptions::None &&
            (options & HCWebSocketOptions::RequestCompression) == HCWebSocketOptions::None)
        {
            return E_NOT_SUPPORTED;
        }

        return S_OK;
    }
};

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
        WebSocketCompletionResult connectResult{};
        VERIFY_ARE_EQUAL(S_OK, HCGetWebSocketConnectResult(&asyncBlock, &connectResult));
        VERIFY_ARE_EQUAL(S_OK, connectResult.errorCode);
        VERIFY_IS_TRUE(connectResult.websocket == websocket);
        VERIFY_ARE_EQUAL(true, g_HCWebSocketConnect_Called);

        ZeroMemory(&asyncBlock, sizeof(XAsyncBlock));
        VERIFY_ARE_EQUAL(false, g_HCWebSocketSendMessage_Called);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSendMessageAsync(websocket, "test", &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketSendMessage_Called);

        VERIFY_ARE_EQUAL(false, g_HCWebSocketDisconnect_Called);
        g_HCWebSocketDisconnect_CloseStatus = HCWebSocketCloseStatus::UnknownError;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketDisconnect_Called);
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCWebSocketCloseStatus::Normal), static_cast<uint32_t>(g_HCWebSocketDisconnect_CloseStatus));

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

    DEFINE_TEST_CASE(TestFailedConnectPreservesResponseHeadersAndHandle)
    {
        VERIFY_ARE_EQUAL(S_OK, HCSetWebSocketFunctions(Test_Internal_HCWebSocketConnectAsyncFailsWithResponseHeaders, Test_Internal_HCWebSocketSendMessageAsync, Test_Internal_HCWebSocketSendBinaryMessageAsync, Test_Internal_HCWebSocketDisconnect, nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCWebsocketHandle websocket = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        VERIFY_IS_NOT_NULL(websocket);

        g_HCWebSocketConnectWithFailureHeaders_Called = false;
        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("test", "subProtoTest", websocket, &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketConnectWithFailureHeaders_Called);

        WebSocketCompletionResult firstResult{};
        VERIFY_ARE_EQUAL(S_OK, HCGetWebSocketConnectResult(&asyncBlock, &firstResult));

        VERIFY_IS_TRUE(websocket == firstResult.websocket);
        VERIFY_ARE_EQUAL(E_ACCESSDENIED, firstResult.errorCode);
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(1234), firstResult.platformErrorCode);

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumResponseHeaders(firstResult.websocket, &numHeaders));
        VERIFY_ARE_EQUAL(1u, numHeaders);

        const CHAR* headerValue = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeader(firstResult.websocket, "failureHeader", &headerValue));
        VERIFY_ARE_EQUAL_STR("failureValue", headerValue);

        const CHAR* headerName = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeaderAtIndex(websocket, 0, &headerName, &headerValue));
        VERIFY_ARE_EQUAL_STR("failureHeader", headerName);
        VERIFY_ARE_EQUAL_STR("failureValue", headerValue);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestDisconnectWithStatus)
    {
        VERIFY_ARE_EQUAL(S_OK, HCSetWebSocketFunctions(Test_Internal_HCWebSocketConnectAsync, Test_Internal_HCWebSocketSendMessageAsync, Test_Internal_HCWebSocketSendBinaryMessageAsync, Test_Internal_HCWebSocketDisconnect, nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCWebsocketHandle websocket = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        VERIFY_IS_NOT_NULL(websocket);

        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("test", "subProtoTest", websocket, &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));

        g_HCWebSocketDisconnect_Called = false;
        g_HCWebSocketDisconnect_CloseStatus = HCWebSocketCloseStatus::Normal;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnectWithStatus(websocket, HCWebSocketCloseStatus::GoingAway));
        VERIFY_ARE_EQUAL(true, g_HCWebSocketDisconnect_Called);
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCWebSocketCloseStatus::GoingAway), static_cast<uint32_t>(g_HCWebSocketDisconnect_CloseStatus));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestCompressionOptions)
    {
        auto const legacySemantics = HCWebSocketOptions::LegacySemantics;
        auto const requestCompression = HCWebSocketOptions::RequestCompression;
        auto const serverNoContextTakeover = HCWebSocketOptions::CompressionServerNoContextTakeover;
        auto const clientNoContextTakeover = HCWebSocketOptions::CompressionClientNoContextTakeover;
        auto const requestWithServerNoContextTakeover = requestCompression | serverNoContextTakeover;
        auto const requestWithClientNoContextTakeover = requestCompression | clientNoContextTakeover;
        auto const requestWithBothNoContextTakeover = requestWithServerNoContextTakeover | clientNoContextTakeover;

        VERIFY_ARE_EQUAL(S_OK, HCSetWebSocketFunctions(Test_Internal_HCWebSocketConnectAsync, Test_Internal_HCWebSocketSendMessageAsync, Test_Internal_HCWebSocketSendBinaryMessageAsync, Test_Internal_HCWebSocketDisconnect, nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCWebsocketHandle websocket = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        VERIFY_IS_NOT_NULL(websocket);
        VERIFY_IS_TRUE(websocket->websocket->UsesLegacySemantics());
        VERIFY_IS_FALSE(websocket->websocket->OptionsExplicitlySet());

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetOptions(nullptr, HCWebSocketOptions::None));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetOptions(websocket, static_cast<HCWebSocketOptions>(0x8)));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetOptions(websocket, legacySemantics | requestCompression));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetOptions(websocket, serverNoContextTakeover));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetOptions(websocket, clientNoContextTakeover));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetOptions(websocket, serverNoContextTakeover | clientNoContextTakeover));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, legacySemantics));
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(legacySemantics), static_cast<uint32_t>(websocket->websocket->Options()));
        VERIFY_IS_TRUE(websocket->websocket->UsesLegacySemantics());
        VERIFY_IS_TRUE(websocket->websocket->OptionsExplicitlySet());
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetOptions(websocket, HCWebSocketOptions::None));
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetOptions(websocket, requestCompression));
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetOptions(websocket, requestWithServerNoContextTakeover));
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetOptions(websocket, requestWithClientNoContextTakeover));
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetOptions(websocket, requestWithBothNoContextTakeover));

        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("test", "subProtoTest", websocket, &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));
        VERIFY_ARE_EQUAL(E_HC_CONNECT_ALREADY_CALLED, HCWebSocketSetOptions(websocket, requestWithBothNoContextTakeover));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestLegacySemanticsNoOp)
    {
        auto const requestCompression = HCWebSocketOptions::RequestCompression;
        auto const legacySemantics = HCWebSocketOptions::LegacySemantics;

        UnsupportedOptionsTestProvider provider;
        auto observer = HC_WEBSOCKET_OBSERVER::Initialize(
            std::make_shared<WebSocket>(1, provider),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        HCWebsocketHandle websocket = observer.get();

        VERIFY_IS_TRUE(websocket->websocket->UsesLegacySemantics());
        VERIFY_IS_FALSE(websocket->websocket->OptionsExplicitlySet());

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, legacySemantics));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(legacySemantics),
            static_cast<uint32_t>(websocket->websocket->Options()));
        VERIFY_IS_TRUE(websocket->websocket->UsesLegacySemantics());
        VERIFY_IS_TRUE(websocket->websocket->OptionsExplicitlySet());

        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetOptions(websocket, requestCompression));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(legacySemantics),
            static_cast<uint32_t>(websocket->websocket->Options()));
    }

    DEFINE_TEST_CASE(TestCompressionOptionsConfigurableProvider)
    {
        auto const legacySemantics = HCWebSocketOptions::LegacySemantics;
        auto const requestCompression = HCWebSocketOptions::RequestCompression;
        auto const requestWithServerNoContextTakeover = requestCompression | HCWebSocketOptions::CompressionServerNoContextTakeover;
        auto const requestWithClientNoContextTakeover = requestCompression | HCWebSocketOptions::CompressionClientNoContextTakeover;
        auto const requestWithBothNoContextTakeover = requestWithServerNoContextTakeover | HCWebSocketOptions::CompressionClientNoContextTakeover;

        ConfigurableCompressionTestProvider provider;
        auto observer = HC_WEBSOCKET_OBSERVER::Initialize(
            std::make_shared<WebSocket>(1, provider),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        HCWebsocketHandle websocket = observer.get();

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, legacySemantics));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(legacySemantics),
            static_cast<uint32_t>(websocket->websocket->Options()));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, HCWebSocketOptions::None));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(HCWebSocketOptions::None),
            static_cast<uint32_t>(websocket->websocket->Options()));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, requestCompression));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(requestCompression),
            static_cast<uint32_t>(websocket->websocket->Options()));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, requestWithServerNoContextTakeover));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(requestWithServerNoContextTakeover),
            static_cast<uint32_t>(websocket->websocket->Options()));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, requestWithClientNoContextTakeover));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(requestWithClientNoContextTakeover),
            static_cast<uint32_t>(websocket->websocket->Options()));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, requestWithBothNoContextTakeover));
        VERIFY_ARE_EQUAL(
            static_cast<uint32_t>(requestWithBothNoContextTakeover),
            static_cast<uint32_t>(websocket->websocket->Options()));
        VERIFY_IS_TRUE(websocket->websocket->OptionsExplicitlySet());
    }

    DEFINE_TEST_CASE(TestWebSocketppGdkProxyDecryptsHttpsPolicyHelpers)
    {
        VERIFY_IS_TRUE(ShouldForceTlsValidationForGdkSandbox(E_FAIL, "CERT"));
        VERIFY_IS_TRUE(ShouldForceTlsValidationForGdkSandbox(S_OK, "RETAIL"));
        VERIFY_IS_FALSE(ShouldForceTlsValidationForGdkSandbox(S_OK, "CERT"));

        VERIFY_IS_TRUE(ApplyTlsValidationBackstopForGdkConsole(true, true));
        VERIFY_IS_FALSE(ApplyTlsValidationBackstopForGdkConsole(false, true));
        VERIFY_IS_FALSE(ApplyTlsValidationBackstopForGdkConsole(true, false));
        VERIFY_IS_FALSE(ApplyTlsValidationBackstopForGdkConsole(false, false));
    }

#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
    DEFINE_TEST_CASE(TestDeterministicOptionsRejectFragmentHandlers)
    {
        ConfigurableCompressionTestProvider provider;

        auto observer = HC_WEBSOCKET_OBSERVER::Initialize(
            std::make_shared<WebSocket>(1, provider),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        HCWebsocketHandle websocket = observer.get();

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, HCWebSocketOptions::None));
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetBinaryMessageFragmentEventFunction(websocket, Internal_HCWebSocketBinaryMessageFragment));

        auto observerWithFragment = HC_WEBSOCKET_OBSERVER::Initialize(
            std::make_shared<WebSocket>(2, provider),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        HCWebsocketHandle websocketWithFragment = observerWithFragment.get();

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetBinaryMessageFragmentEventFunction(websocketWithFragment, Internal_HCWebSocketBinaryMessageFragment));
        VERIFY_ARE_EQUAL(E_NOT_SUPPORTED, HCWebSocketSetOptions(websocketWithFragment, HCWebSocketOptions::None));
    }

    DEFINE_TEST_CASE(TestMaxReceiveBufferSizePreservedAcrossSetOptions)
    {
        ConfigurableCompressionTestProvider provider;
        auto observer = HC_WEBSOCKET_OBSERVER::Initialize(
            std::make_shared<WebSocket>(1, provider),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        HCWebsocketHandle websocket = observer.get();

        constexpr size_t configuredReceiveBufferSize{ 4096 };
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetMaxReceiveBufferSize(websocket, configuredReceiveBufferSize));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetOptions(websocket, HCWebSocketOptions::None));
        VERIFY_ARE_EQUAL(configuredReceiveBufferSize, websocket->websocket->MaxReceiveBufferSize());
        VERIFY_IS_TRUE(websocket->websocket->MaxReceiveBufferSizeExplicitlySet());
    }

    DEFINE_TEST_CASE(TestMaxReceiveBufferSizeValidation)
    {
        VERIFY_ARE_EQUAL(S_OK, HCSetWebSocketFunctions(Test_Internal_HCWebSocketConnectAsync, Test_Internal_HCWebSocketSendMessageAsync, Test_Internal_HCWebSocketSendBinaryMessageAsync, Test_Internal_HCWebSocketDisconnect, nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        HCWebsocketHandle websocket = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        VERIFY_IS_NOT_NULL(websocket);

        constexpr size_t initialReceiveBufferSize{ 4096 };
        constexpr size_t updatedReceiveBufferSize{ 8192 };

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetMaxReceiveBufferSize(nullptr, initialReceiveBufferSize));

        if (sizeof(size_t) > sizeof(uint32_t))
        {
            size_t const oversizedReceiveBufferSize = static_cast<size_t>(UINT32_MAX) + 1u;
            VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetMaxReceiveBufferSize(websocket, oversizedReceiveBufferSize));
        }

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetMaxReceiveBufferSize(websocket, initialReceiveBufferSize));
        VERIFY_ARE_EQUAL(initialReceiveBufferSize, websocket->websocket->MaxReceiveBufferSize());

        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("test", "subProtoTest", websocket, &asyncBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));

        VERIFY_ARE_EQUAL(E_HC_CONNECT_ALREADY_CALLED, HCWebSocketSetMaxReceiveBufferSize(websocket, updatedReceiveBufferSize));
        VERIFY_ARE_EQUAL(initialReceiveBufferSize, websocket->websocket->MaxReceiveBufferSize());

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        HCCleanup();
    }
#endif

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

    DEFINE_TEST_CASE(TestResponseHeaders)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseHeaders);
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCWebsocketHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&call, nullptr, nullptr, nullptr, nullptr));

        uint32_t numHeaders = 1;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumResponseHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        const CHAR* headerValue = "sentinel";
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeader(call, "testHeader", &headerValue));
        VERIFY_IS_NULL(headerValue);

        const CHAR* headerName = "sentinel";
        headerValue = "sentinel";
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetResponseHeaderAtIndex(call, 0, &headerName, &headerValue));
        VERIFY_IS_NULL(headerName);
        VERIFY_IS_NULL(headerValue);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(call));
        HCCleanup();
    }

    DEFINE_TEST_CASE(TestResponseHeadersAccessors)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseHeadersAccessors);
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));
        HCWebsocketHandle call = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&call, nullptr, nullptr, nullptr, nullptr));

        HttpHeaders responseHeaders;
        responseHeaders["aHeader"] = "aValue";
        responseHeaders["bHeader"] = "bValue";
        VERIFY_ARE_EQUAL(S_OK, call->websocket->SetResponseHeaders(std::move(responseHeaders)));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumResponseHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(2, numHeaders);

        const CHAR* headerValue = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeader(call, "aHeader", &headerValue));
        VERIFY_ARE_EQUAL_STR("aValue", headerValue);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeader(call, "bHeader", &headerValue));
        VERIFY_ARE_EQUAL_STR("bValue", headerValue);

        const CHAR* headerName = nullptr;
        const CHAR* indexedHeaderValue = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeaderAtIndex(call, 0, &headerName, &indexedHeaderValue));
        VERIFY_ARE_EQUAL_STR("aHeader", headerName);
        VERIFY_ARE_EQUAL_STR("aValue", indexedHeaderValue);
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeaderAtIndex(call, 1, &headerName, &indexedHeaderValue));
        VERIFY_ARE_EQUAL_STR("bHeader", headerName);
        VERIFY_ARE_EQUAL_STR("bValue", indexedHeaderValue);

        call->websocket->ClearResponseHeaders();

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetNumResponseHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(0, numHeaders);

        headerValue = "sentinel";
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetResponseHeader(call, "aHeader", &headerValue));
        VERIFY_IS_NULL(headerValue);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(call));
        HCCleanup();
    }

    // Regression guard: connect must complete on the Work port without the Completion port being
    // dispatched, while the client completion callback is still deferred to the Completion port.
    DEFINE_TEST_CASE(VerifyWebSocketConnectCompletesOnWorkPortWithoutCompletionPump)
    {
        VERIFY_ARE_EQUAL(S_OK, HCSetWebSocketFunctions(
            Test_Internal_HCWebSocketConnectAsync,
            Test_Internal_HCWebSocketSendMessageAsync,
            Test_Internal_HCWebSocketSendBinaryMessageAsync,
            Test_Internal_HCWebSocketDisconnect,
            nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCInitialize(nullptr));

        // Manual / Manual queue: the caller controls when each port is dispatched.
        XTaskQueueHandle queue{ nullptr };
        VERIFY_ARE_EQUAL(S_OK, XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &queue));

        g_HCWebSocketConnect_Called = false;
        g_HCWebSocketSendMessage_Called = false;

        HCWebsocketHandle websocket{ nullptr };
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        VERIFY_IS_NOT_NULL(websocket);

        std::atomic<bool> clientCompleted{ false };
        struct ConnectState { std::atomic<bool>* completed; } connectState{ &clientCompleted };

        XAsyncBlock asyncBlock{};
        asyncBlock.queue = queue;
        asyncBlock.context = &connectState;
        asyncBlock.callback = [](XAsyncBlock* async)
        {
            auto state = static_cast<ConnectState*>(async->context);
            state->completed->store(true);
        };

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("test", "subProtoTest", websocket, &asyncBlock));

        // Pump ONLY the Work port: the internal connect completion runs here, reaching Connected.
        bool workDispatched = true;
        while (workDispatched)
        {
            workDispatched = XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0);
        }

        VERIFY_IS_TRUE(g_HCWebSocketConnect_Called);

        // A send is accepted only once the socket is Connected (SendAsync returns E_UNEXPECTED before
        // the connect completion allocates the provider context), so this probes that the connect
        // completion was not stranded on the unpumped Completion port.
        XAsyncBlock probeSendBlock{};
        probeSendBlock.queue = queue;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSendMessageAsync(websocket, "probe", &probeSendBlock));

        // The client completion is deferred to the Completion port, so it has not fired yet.
        VERIFY_IS_FALSE(clientCompleted.load());

        // Drain both ports to deliver the client and send completions.
        bool dispatched = true;
        while (dispatched)
        {
            bool work = XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0);
            bool completion = XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0);
            dispatched = work || completion;
        }

        VERIFY_IS_TRUE(clientCompleted.load());
        VERIFY_IS_TRUE(g_HCWebSocketSendMessage_Called);

        WebSocketCompletionResult connectResult{};
        VERIFY_ARE_EQUAL(S_OK, HCGetWebSocketConnectResult(&asyncBlock, &connectResult));
        VERIFY_ARE_EQUAL(S_OK, connectResult.errorCode);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        XTaskQueueCloseHandle(queue);
        HCCleanup();
    }

};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
