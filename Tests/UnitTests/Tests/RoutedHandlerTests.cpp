// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Routed handlers, call context, call ids, request url and per-call tracing (test-gap plan item 1.5).
//
// HTTP calls are served by a scripted fake provider installed with HCSetHttpCallPerformFunction and
// WebSocket traffic by fake functions installed with HCSetWebSocketFunctions, since the Generic platform
// used by the unit tests has no network stack.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "HTTP/httpcall.h"
#include "Platform/ExternalHttpProvider.h"
#include "Platform/ExternalWebSocketProvider.h"
#include <httpClient/httpProvider.h>
#include <httpClient/mock.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

const HRESULT c_providerFailure = static_cast<HRESULT>(0x80072EFDL); // HRESULT_FROM_WIN32(ERROR_WINHTTP_CANNOT_CONNECT)

class RoutedProviderScope
{
public:
    RoutedProviderScope(HCCallPerformFunction perform, void* context) noexcept
    {
        ExternalHttpProvider::Get().ClearCallback();
#ifndef HC_NOWEBSOCKETS
        ExternalWebSocketProvider::Get().ClearCallbacks();
#endif
        if (perform != nullptr)
        {
            m_initHr = HCSetHttpCallPerformFunction(perform, context);
        }
    }

    ~RoutedProviderScope()
    {
        HCMockClearMocks();
        HCCleanup();
        ExternalHttpProvider::Get().ClearCallback();
#ifndef HC_NOWEBSOCKETS
        ExternalWebSocketProvider::Get().ClearCallbacks();
#endif
    }

    RoutedProviderScope(const RoutedProviderScope&) = delete;
    RoutedProviderScope& operator=(const RoutedProviderScope&) = delete;

    HRESULT Initialize() noexcept
    {
        if (SUCCEEDED(m_initHr))
        {
            m_initHr = HCInitialize(nullptr);
        }
        return m_initHr;
    }

private:
    HRESULT m_initHr{ S_OK };
};

struct RoutedScopedCall
{
    RoutedScopedCall() = default;
    RoutedScopedCall(const RoutedScopedCall&) = delete;
    RoutedScopedCall& operator=(const RoutedScopedCall&) = delete;
    ~RoutedScopedCall()
    {
        if (handle != nullptr)
        {
            HCHttpCallCloseHandle(handle);
        }
    }

    HCCallHandle handle{ nullptr };
};

HRESULT RoutedPerformAndWait(HCCallHandle call)
{
    XTaskQueueHandle queue{ nullptr };
    HRESULT hr = XTaskQueueCreate(XTaskQueueDispatchMode::ThreadPool, XTaskQueueDispatchMode::ThreadPool, &queue);
    if (FAILED(hr))
    {
        return hr;
    }

    XAsyncBlock asyncBlock{};
    asyncBlock.queue = queue;
    hr = HCHttpCallPerformAsync(call, &asyncBlock);
    if (SUCCEEDED(hr))
    {
        hr = XAsyncGetStatus(&asyncBlock, true);
    }

    XTaskQueueCloseHandle(queue);
    return hr;
}

// Fake provider: responds with statuses[attempt] (the last entry repeats) or fails with failHr.
struct ScriptedServer
{
    std::vector<uint32_t> statuses{ 200 };
    HRESULT failHr{ S_OK };

    std::mutex lock;
    uint32_t attempts{ 0 };
    std::vector<void*> contextsSeen;
    std::vector<std::string> urlsSeen;
};

void CALLBACK ScriptedPerform(
    _In_ HCCallHandle call,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_opt_ HCPerformEnv /*env*/
)
{
    auto& server = *static_cast<ScriptedServer*>(context);

    void* callContext{ nullptr };
    HCHttpCallGetContext(call, &callContext);
    const char* url{ nullptr };
    HCHttpCallGetRequestUrl(call, &url);

    uint32_t attempt{ 0 };
    HRESULT failHr{ S_OK };
    uint32_t status{ 200 };
    {
        std::lock_guard<std::mutex> guard{ server.lock };
        attempt = server.attempts++;
        server.contextsSeen.push_back(callContext);
        server.urlsSeen.push_back(url != nullptr ? url : "<null>");
        failHr = server.failHr;
        if (!server.statuses.empty())
        {
            status = server.statuses[(std::min)(static_cast<size_t>(attempt), server.statuses.size() - 1)];
        }
    }

    if (FAILED(failHr))
    {
        XAsyncComplete(asyncBlock, failHr, 0);
        return;
    }

    HCHttpCallResponseSetStatusCode(call, status);
    XAsyncComplete(asyncBlock, S_OK, 0);
}

struct CallRoute
{
    HCCallHandle call;
    uint64_t id;
    uint32_t status;
    void* context;
};

struct CallRouteRecorder
{
    std::mutex lock;
    std::vector<CallRoute> routes;

    size_t Count()
    {
        std::lock_guard<std::mutex> guard{ lock };
        return routes.size();
    }
};

void STDAPIVCALLTYPE RecordCallRoute(
    _In_ HCCallHandle call,
    _In_opt_ void* context
)
{
    auto recorder = static_cast<CallRouteRecorder*>(context);
    uint32_t status{ 0 };
    HCHttpCallResponseGetStatusCode(call, &status);

    std::lock_guard<std::mutex> guard{ recorder->lock };
    recorder->routes.push_back(CallRoute{ call, HCHttpCallGetId(call), status, context });
}

#ifndef HC_NOWEBSOCKETS

struct WebSocketRoute
{
    HCWebsocketHandle websocket;
    bool receiving;
    bool hasMessage;
    std::string message;
    std::vector<uint8_t> payload;
    size_t payloadSize;
    void* context;
};

struct WebSocketRouteRecorder
{
    std::mutex lock;
    std::vector<WebSocketRoute> routes;

    size_t Count()
    {
        std::lock_guard<std::mutex> guard{ lock };
        return routes.size();
    }

    WebSocketRoute At(size_t index)
    {
        std::lock_guard<std::mutex> guard{ lock };
        return routes.at(index);
    }
};

void STDAPIVCALLTYPE RecordWebSocketRoute(
    _In_ HCWebsocketHandle websocket,
    _In_ bool receiving,
    _In_opt_z_ const char* message,
    _In_opt_ const uint8_t* payloadBytes,
    _In_ size_t payloadSize,
    _In_opt_ void* context
)
{
    auto recorder = static_cast<WebSocketRouteRecorder*>(context);
    WebSocketRoute route{ websocket, receiving, message != nullptr, message != nullptr ? message : "", {}, payloadSize, context };
    if (payloadBytes != nullptr)
    {
        route.payload.assign(payloadBytes, payloadBytes + payloadSize);
    }

    std::lock_guard<std::mutex> guard{ recorder->lock };
    recorder->routes.push_back(std::move(route));
}

struct FakeWebSocketServer
{
    std::atomic<HCWebsocketHandle> providerHandle{ nullptr };
    std::atomic<uint32_t> textSends{ 0 };
    std::atomic<uint32_t> binarySends{ 0 };
    std::atomic<uint32_t> disconnects{ 0 };
};

HRESULT CALLBACK FakeWebSocketConnectProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    switch (op)
    {
    case XAsyncOp::Begin:
        XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
        return S_OK;
    case XAsyncOp::GetResult:
    {
        if (data->bufferSize < sizeof(WebSocketCompletionResult))
        {
            return E_NOT_SUFFICIENT_BUFFER;
        }
        auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
        *result = WebSocketCompletionResult{};
        result->errorCode = S_OK;
        result->websocket = static_cast<HCWebsocketHandle>(data->context);
        return S_OK;
    }
    default:
        return S_OK;
    }
}

HRESULT CALLBACK FakeWebSocketConnect(
    _In_z_ const char* /*uri*/,
    _In_z_ const char* /*subProtocol*/,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_opt_ HCPerformEnv /*env*/
)
{
    static_cast<FakeWebSocketServer*>(context)->providerHandle = websocket;
    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__, FakeWebSocketConnectProvider);
}

HRESULT CALLBACK CompleteImmediatelyProvider(XAsyncOp op, const XAsyncProviderData* data)
{
    if (op == XAsyncOp::Begin)
    {
        XAsyncComplete(data->async, S_OK, 0);
    }
    return S_OK;
}

HRESULT CALLBACK FakeWebSocketSend(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_z_ const char* /*message*/,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
)
{
    ++static_cast<FakeWebSocketServer*>(context)->textSends;
    return XAsyncBegin(asyncBlock, nullptr, nullptr, __FUNCTION__, CompleteImmediatelyProvider);
}

HRESULT CALLBACK FakeWebSocketSendBinary(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_reads_bytes_(payloadSize) const uint8_t* /*payloadBytes*/,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
)
{
    (void)payloadSize;
    ++static_cast<FakeWebSocketServer*>(context)->binarySends;
    return XAsyncBegin(asyncBlock, nullptr, nullptr, __FUNCTION__, CompleteImmediatelyProvider);
}

HRESULT CALLBACK FakeWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* context
)
{
    ++static_cast<FakeWebSocketServer*>(context)->disconnects;

    // Behave like a provider whose close handshake completed: raise the close event.
    HCWebSocketCloseEventFunction closeFunc{ nullptr };
    void* closeContext{ nullptr };
    HRESULT hr = HCWebSocketGetEventFunctions(websocket, nullptr, nullptr, &closeFunc, &closeContext);
    if (SUCCEEDED(hr) && closeFunc != nullptr)
    {
        closeFunc(websocket, closeStatus, closeContext);
    }
    return hr;
}

struct ClientWebSocketEvents
{
    std::mutex lock;
    std::vector<std::string> messages;
    std::vector<std::vector<uint8_t>> binaryMessages;
    uint32_t closes{ 0 };
};

void CALLBACK ClientMessage(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_z_ const char* incomingBodyString,
    _In_opt_ void* context
)
{
    auto events = static_cast<ClientWebSocketEvents*>(context);
    std::lock_guard<std::mutex> guard{ events->lock };
    events->messages.emplace_back(incomingBodyString);
}

void CALLBACK ClientBinaryMessage(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _In_opt_ void* context
)
{
    auto events = static_cast<ClientWebSocketEvents*>(context);
    std::lock_guard<std::mutex> guard{ events->lock };
    events->binaryMessages.emplace_back(payloadBytes, payloadBytes + payloadSize);
}

void CALLBACK ClientClose(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_ HCWebSocketCloseStatus /*closeStatus*/,
    _In_opt_ void* context
)
{
    auto events = static_cast<ClientWebSocketEvents*>(context);
    std::lock_guard<std::mutex> guard{ events->lock };
    ++events->closes;
}

HRESULT SendTextAndWait(HCWebsocketHandle websocket, const char* message)
{
    XAsyncBlock asyncBlock{};
    HRESULT hr = HCWebSocketSendMessageAsync(websocket, message, &asyncBlock);
    if (SUCCEEDED(hr))
    {
        hr = XAsyncGetStatus(&asyncBlock, true);
    }
    return hr;
}

HRESULT SendBinaryAndWait(HCWebsocketHandle websocket, const std::vector<uint8_t>& payload)
{
    XAsyncBlock asyncBlock{};
    HRESULT hr = HCWebSocketSendBinaryMessageAsync(websocket, payload.data(), static_cast<uint32_t>(payload.size()), &asyncBlock);
    if (SUCCEEDED(hr))
    {
        hr = XAsyncGetStatus(&asyncBlock, true);
    }
    return hr;
}

#endif // !HC_NOWEBSOCKETS

} // namespace

DEFINE_TEST_CLASS(RoutedHandlerTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(RoutedHandlerTests);

    DEFINE_TEST_CASE(TestCallRoutedHandlerInvokedForEachCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCallRoutedHandlerInvokedForEachCall);

        ScriptedServer server;
        RoutedProviderScope scope{ ScriptedPerform, &server };
        VERIFY_SUCCEEDED(scope.Initialize());

        CallRouteRecorder recorder;
        int32_t handlerId = HCAddCallRoutedHandler(RecordCallRoute, &recorder);
        VERIFY_IS_TRUE(handlerId >= 0);

        RoutedScopedCall calls[3];
        for (auto& call : calls)
        {
            VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/routed"));
            VERIFY_SUCCEEDED(RoutedPerformAndWait(call.handle));
        }

        // The handler runs synchronously before the perform completes, once per completed call.
        VERIFY_ARE_EQUAL(3u, recorder.Count());
        for (size_t i = 0; i < 3; ++i)
        {
            const CallRoute& route = recorder.routes[i];
            VERIFY_IS_TRUE(route.call == calls[i].handle);
            VERIFY_ARE_EQUAL(HCHttpCallGetId(calls[i].handle), route.id);
            VERIFY_IS_TRUE(route.context == &recorder);
            VERIFY_ARE_EQUAL(200u, route.status);
        }

        HCRemoveCallRoutedHandler(handlerId);
    }

    DEFINE_TEST_CASE(TestMultipleCallRoutedHandlersAndRemoval)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestMultipleCallRoutedHandlersAndRemoval);

        ScriptedServer server;
        RoutedProviderScope scope{ ScriptedPerform, &server };
        VERIFY_SUCCEEDED(scope.Initialize());

        CallRouteRecorder first;
        CallRouteRecorder second;
        int32_t firstId = HCAddCallRoutedHandler(RecordCallRoute, &first);
        int32_t secondId = HCAddCallRoutedHandler(RecordCallRoute, &second);
        VERIFY_IS_TRUE(firstId >= 0);
        VERIFY_IS_TRUE(secondId >= 0);
        VERIFY_IS_TRUE(firstId != secondId);

        auto performOne = [&]()
        {
            RoutedScopedCall call;
            VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/"));
            VERIFY_SUCCEEDED(RoutedPerformAndWait(call.handle));
        };

        performOne();
        VERIFY_ARE_EQUAL(1u, first.Count());
        VERIFY_ARE_EQUAL(1u, second.Count());

        // Removing by id stops only that handler.
        HCRemoveCallRoutedHandler(firstId);
        performOne();
        VERIFY_ARE_EQUAL(1u, first.Count());
        VERIFY_ARE_EQUAL(2u, second.Count());

        // Removing unknown or already removed ids is a no-op.
        HCRemoveCallRoutedHandler(firstId);
        HCRemoveCallRoutedHandler(secondId + 1000);
        HCRemoveCallRoutedHandler(-1);
        performOne();
        VERIFY_ARE_EQUAL(1u, first.Count());
        VERIFY_ARE_EQUAL(3u, second.Count());

        HCRemoveCallRoutedHandler(secondId);
        performOne();
        VERIFY_ARE_EQUAL(1u, first.Count());
        VERIFY_ARE_EQUAL(3u, second.Count());

        // The same function/context can be registered again and gets a fresh id.
        int32_t againId = HCAddCallRoutedHandler(RecordCallRoute, &first);
        VERIFY_IS_TRUE(againId != firstId);
        VERIFY_IS_TRUE(againId != secondId);
        performOne();
        VERIFY_ARE_EQUAL(2u, first.Count());
        HCRemoveCallRoutedHandler(againId);
    }

    DEFINE_TEST_CASE(TestCallRoutedHandlerSeesRetriesFailuresAndMocks)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCallRoutedHandlerSeesRetriesFailuresAndMocks);

        ScriptedServer server;
        server.statuses = { 503, 200 };
        RoutedProviderScope scope{ ScriptedPerform, &server };
        VERIFY_SUCCEEDED(scope.Initialize());

        CallRouteRecorder recorder;
        int32_t handlerId = HCAddCallRoutedHandler(RecordCallRoute, &recorder);
        VERIFY_IS_TRUE(handlerId >= 0);

        // A retried call routes each attempt, with the response of that attempt.
        RoutedScopedCall retried;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&retried.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(retried.handle, "GET", "https://example.test/flaky"));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(retried.handle, true));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryDelay(retried.handle, 0)); // no back-off delay
        VERIFY_SUCCEEDED(RoutedPerformAndWait(retried.handle));

        uint32_t performCount{ 0 };
        VERIFY_SUCCEEDED(HCHttpCallGetPerformCount(retried.handle, &performCount));
        VERIFY_ARE_EQUAL(2u, performCount);
        VERIFY_ARE_EQUAL(2u, recorder.Count());
        VERIFY_ARE_EQUAL(503u, recorder.routes[0].status);
        VERIFY_ARE_EQUAL(200u, recorder.routes[1].status);
        VERIFY_IS_TRUE(recorder.routes[0].call == retried.handle);
        VERIFY_IS_TRUE(recorder.routes[1].call == retried.handle);

        // A provider failure is still routed, and the failure reaches the client.
        {
            std::lock_guard<std::mutex> guard{ server.lock };
            server.failHr = c_providerFailure;
        }
        RoutedScopedCall failed;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&failed.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(failed.handle, "GET", "https://example.test/down"));
        VERIFY_ARE_EQUAL(c_providerFailure, RoutedPerformAndWait(failed.handle));
        VERIFY_ARE_EQUAL(3u, recorder.Count());
        VERIFY_IS_TRUE(recorder.routes[2].call == failed.handle);
        VERIFY_ARE_EQUAL(0u, recorder.routes[2].status);

        // A mocked call never reaches the provider but is still routed.
        uint32_t attemptsBefore{ 0 };
        {
            std::lock_guard<std::mutex> guard{ server.lock };
            attemptsBefore = server.attempts;
        }
        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 201));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "GET", "https://example.test/mocked", nullptr, 0));
        VERIFY_SUCCEEDED(HCMockCallCloseHandle(mock));

        RoutedScopedCall mocked;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&mocked.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(mocked.handle, "GET", "https://example.test/mocked"));
        VERIFY_SUCCEEDED(RoutedPerformAndWait(mocked.handle));
        VERIFY_ARE_EQUAL(4u, recorder.Count());
        VERIFY_IS_TRUE(recorder.routes[3].call == mocked.handle);
        VERIFY_ARE_EQUAL(201u, recorder.routes[3].status);
        {
            std::lock_guard<std::mutex> guard{ server.lock };
            VERIFY_ARE_EQUAL(attemptsBefore, server.attempts);
        }

        HCRemoveCallRoutedHandler(handlerId);
    }

    DEFINE_TEST_CASE(TestRoutedHandlerRegistrationEdgeCases)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRoutedHandlerRegistrationEdgeCases);

        ScriptedServer server;
        RoutedProviderScope scope{ ScriptedPerform, &server };

        // Before HCInitialize the add functions return E_HC_NOT_INITIALISED cast to the id type and
        // remove is a harmless no-op.
        CallRouteRecorder recorder;
        VERIFY_ARE_EQUAL(static_cast<int32_t>(E_HC_NOT_INITIALISED), HCAddCallRoutedHandler(RecordCallRoute, &recorder));
        HCRemoveCallRoutedHandler(0);
#ifndef HC_NOWEBSOCKETS
        WebSocketRouteRecorder wsRecorder;
        VERIFY_ARE_EQUAL(static_cast<int32_t>(E_HC_NOT_INITIALISED), HCAddWebSocketRoutedHandler(RecordWebSocketRoute, &wsRecorder));
        HCRemoveWebSocketRoutedHandler(0);
#endif

        VERIFY_SUCCEEDED(scope.Initialize());

        // A null handler is rejected with -1.
        VERIFY_ARE_EQUAL(-1, HCAddCallRoutedHandler(nullptr, &recorder));
#ifndef HC_NOWEBSOCKETS
        VERIFY_ARE_EQUAL(-1, HCAddWebSocketRoutedHandler(nullptr, &wsRecorder));
#endif

        // Call and WebSocket handlers share one id sequence, so ids never collide.
        int32_t callId = HCAddCallRoutedHandler(RecordCallRoute, &recorder);
        VERIFY_IS_TRUE(callId >= 0);
#ifndef HC_NOWEBSOCKETS
        int32_t wsId = HCAddWebSocketRoutedHandler(RecordWebSocketRoute, &wsRecorder);
        VERIFY_ARE_EQUAL(callId + 1, wsId);
        // Removing a WebSocket handler id through the call API does not remove the call handler.
        HCRemoveCallRoutedHandler(wsId);
#endif

        {
            RoutedScopedCall call;
            VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/"));
            VERIFY_SUCCEEDED(RoutedPerformAndWait(call.handle));
        }
        VERIFY_ARE_EQUAL(1u, recorder.Count());

        // Handlers belong to the initialized instance and do not survive HCCleanup.
        HCCleanup();
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        {
            RoutedScopedCall call;
            VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/"));
            VERIFY_SUCCEEDED(RoutedPerformAndWait(call.handle));
        }
        VERIFY_ARE_EQUAL(1u, recorder.Count());
    }

#ifndef HC_NOWEBSOCKETS
    DEFINE_TEST_CASE(TestWebSocketRoutedHandlers)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestWebSocketRoutedHandlers);

        FakeWebSocketServer wsServer;
        RoutedProviderScope scope{ nullptr, nullptr };
        VERIFY_SUCCEEDED(HCSetWebSocketFunctions(FakeWebSocketConnect, FakeWebSocketSend, FakeWebSocketSendBinary, FakeWebSocketDisconnect, &wsServer));
        VERIFY_SUCCEEDED(scope.Initialize());

        WebSocketRouteRecorder first;
        WebSocketRouteRecorder second;
        int32_t firstId = HCAddWebSocketRoutedHandler(RecordWebSocketRoute, &first);
        int32_t secondId = HCAddWebSocketRoutedHandler(RecordWebSocketRoute, &second);
        VERIFY_IS_TRUE(firstId >= 0);
        VERIFY_IS_TRUE(secondId >= 0);
        VERIFY_IS_TRUE(firstId != secondId);

        // Call routed handlers are not invoked for WebSocket traffic.
        CallRouteRecorder callRecorder;
        int32_t callHandlerId = HCAddCallRoutedHandler(RecordCallRoute, &callRecorder);

        ClientWebSocketEvents events;
        HCWebsocketHandle websocket{ nullptr };
        VERIFY_SUCCEEDED(HCWebSocketCreate(&websocket, ClientMessage, ClientBinaryMessage, ClientClose, &events));

        XAsyncBlock connectBlock{};
        VERIFY_SUCCEEDED(HCWebSocketConnectAsync("wss://example.test/socket", "", websocket, &connectBlock));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&connectBlock, true));
        WebSocketCompletionResult connectResult{};
        VERIFY_SUCCEEDED(HCGetWebSocketConnectResult(&connectBlock, &connectResult));
        VERIFY_SUCCEEDED(connectResult.errorCode);

        HCWebsocketHandle providerHandle = wsServer.providerHandle;
        VERIFY_IS_NOT_NULL(providerHandle);

        // Connecting is not routed.
        VERIFY_ARE_EQUAL(0u, first.Count());

        // Outgoing text and binary messages are routed with receiving == false.
        VERIFY_SUCCEEDED(SendTextAndWait(websocket, "hello"));
        const std::vector<uint8_t> outgoingBinary{ 1, 0, 2, 3 };
        VERIFY_SUCCEEDED(SendBinaryAndWait(websocket, outgoingBinary));
        VERIFY_ARE_EQUAL(1u, wsServer.textSends.load());
        VERIFY_ARE_EQUAL(1u, wsServer.binarySends.load());

        VERIFY_ARE_EQUAL(2u, first.Count());
        VERIFY_ARE_EQUAL(2u, second.Count());
        {
            WebSocketRoute text = first.At(0);
            VERIFY_IS_TRUE(text.websocket == providerHandle);
            VERIFY_IS_FALSE(text.receiving);
            VERIFY_IS_TRUE(text.hasMessage);
            VERIFY_ARE_EQUAL_STR("hello", text.message.c_str());
            VERIFY_IS_TRUE(text.payload.empty());
            VERIFY_ARE_EQUAL(0u, text.payloadSize);
            VERIFY_IS_TRUE(text.context == &first);

            WebSocketRoute binary = first.At(1);
            VERIFY_IS_FALSE(binary.receiving);
            VERIFY_IS_FALSE(binary.hasMessage);
            VERIFY_IS_TRUE(binary.payload == outgoingBinary);
            VERIFY_ARE_EQUAL(outgoingBinary.size(), binary.payloadSize);

            VERIFY_IS_TRUE(second.At(0).context == &second);
        }

        // Incoming messages delivered by the provider are routed with receiving == true, and still
        // reach the client's handlers.
        HCWebSocketMessageFunction messageFunc{ nullptr };
        HCWebSocketBinaryMessageFunction binaryMessageFunc{ nullptr };
        void* eventContext{ nullptr };
        VERIFY_SUCCEEDED(HCWebSocketGetEventFunctions(providerHandle, &messageFunc, &binaryMessageFunc, nullptr, &eventContext));
        messageFunc(providerHandle, "world", eventContext);
        const std::vector<uint8_t> incomingBinary{ 9, 8, 7 };
        binaryMessageFunc(providerHandle, incomingBinary.data(), static_cast<uint32_t>(incomingBinary.size()), eventContext);

        VERIFY_ARE_EQUAL(4u, first.Count());
        {
            WebSocketRoute text = first.At(2);
            VERIFY_IS_TRUE(text.receiving);
            VERIFY_ARE_EQUAL_STR("world", text.message.c_str());
            WebSocketRoute binary = first.At(3);
            VERIFY_IS_TRUE(binary.receiving);
            VERIFY_IS_FALSE(binary.hasMessage);
            VERIFY_IS_TRUE(binary.payload == incomingBinary);
        }
        {
            std::lock_guard<std::mutex> guard{ events.lock };
            VERIFY_ARE_EQUAL(1u, events.messages.size());
            VERIFY_ARE_EQUAL_STR("world", events.messages[0].c_str());
            VERIFY_ARE_EQUAL(1u, events.binaryMessages.size());
            VERIFY_IS_TRUE(events.binaryMessages[0] == incomingBinary);
        }

        // After removal the first handler sees nothing more; the second keeps receiving.
        HCRemoveWebSocketRoutedHandler(firstId);
        HCRemoveWebSocketRoutedHandler(secondId + 1000); // unknown id: no-op
        VERIFY_SUCCEEDED(SendTextAndWait(websocket, "after removal"));
        messageFunc(providerHandle, "incoming after removal", eventContext);
        VERIFY_ARE_EQUAL(4u, first.Count());
        VERIFY_ARE_EQUAL(6u, second.Count());

        // Closing is not routed, but the client close handler runs.
        VERIFY_SUCCEEDED(HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(1u, wsServer.disconnects.load());
        VERIFY_ARE_EQUAL(6u, second.Count());
        {
            std::lock_guard<std::mutex> guard{ events.lock };
            VERIFY_ARE_EQUAL(1u, events.closes);
        }

        VERIFY_ARE_EQUAL(0u, callRecorder.Count());

        HCRemoveWebSocketRoutedHandler(secondId);
        HCRemoveCallRoutedHandler(callHandlerId);
        VERIFY_SUCCEEDED(HCWebSocketCloseHandle(websocket));
    }
#endif // !HC_NOWEBSOCKETS

    DEFINE_TEST_CASE(TestCallContext)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCallContext);

        ScriptedServer server;
        RoutedProviderScope scope{ ScriptedPerform, &server };
        VERIFY_SUCCEEDED(scope.Initialize());

        RoutedScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/context"));

        int sentinel{ 0 };
        void* context{ &sentinel };
        VERIFY_SUCCEEDED(HCHttpCallGetContext(call.handle, &context));
        VERIFY_IS_NULL(context);

        int clientState{ 42 };
        VERIFY_SUCCEEDED(HCHttpCallSetContext(call.handle, &clientState));
        VERIFY_SUCCEEDED(HCHttpCallGetContext(call.handle, &context));
        VERIFY_IS_TRUE(context == &clientState);

        // Duplicated handles refer to the same call object, so they share the context.
        HCCallHandle duplicate = HCHttpCallDuplicateHandle(call.handle);
        VERIFY_IS_TRUE(duplicate == call.handle);
        VERIFY_SUCCEEDED(HCHttpCallGetContext(duplicate, &context));
        VERIFY_IS_TRUE(context == &clientState);
        VERIFY_SUCCEEDED(HCHttpCallCloseHandle(duplicate));

        // The provider can read the context during perform.
        VERIFY_SUCCEEDED(RoutedPerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(1u, server.contextsSeen.size());
        VERIFY_IS_TRUE(server.contextsSeen[0] == &clientState);

        // Unlike request settings, the context may still be changed after perform.
        VERIFY_SUCCEEDED(HCHttpCallSetContext(call.handle, nullptr));
        VERIFY_SUCCEEDED(HCHttpCallGetContext(call.handle, &context));
        VERIFY_IS_NULL(context);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallSetContext(nullptr, &clientState));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallGetContext(nullptr, &context));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallGetContext(call.handle, nullptr));
    }

    DEFINE_TEST_CASE(TestCallIdUniqueAndIncreasing)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCallIdUniqueAndIncreasing);

        RoutedProviderScope scope{ nullptr, nullptr };
        VERIFY_SUCCEEDED(scope.Initialize());

        VERIFY_ARE_EQUAL(0u, HCHttpCallGetId(nullptr));

        RoutedScopedCall calls[4];
        uint64_t previous{ 0 };
        for (auto& call : calls)
        {
            VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
            uint64_t id = HCHttpCallGetId(call.handle);
            VERIFY_IS_TRUE(id > previous);
            previous = id;
        }

        // The id is a property of the call object, so a duplicated handle reports the same id.
        HCCallHandle duplicate = HCHttpCallDuplicateHandle(calls[0].handle);
        VERIFY_ARE_EQUAL(HCHttpCallGetId(calls[0].handle), HCHttpCallGetId(duplicate));
        VERIFY_SUCCEEDED(HCHttpCallCloseHandle(duplicate));

        RoutedScopedCall afterWebSocket;
#ifndef HC_NOWEBSOCKETS
        // WebSockets draw from the same counter, so a call created after a WebSocket skips an id.
        HCWebsocketHandle websocket{ nullptr };
        VERIFY_SUCCEEDED(HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        VERIFY_SUCCEEDED(HCHttpCallCreate(&afterWebSocket.handle));
        VERIFY_ARE_EQUAL(previous + 2, HCHttpCallGetId(afterWebSocket.handle));
        VERIFY_SUCCEEDED(HCWebSocketCloseHandle(websocket));
        VERIFY_SUCCEEDED(HCHttpCallCloseHandle(afterWebSocket.handle));
        afterWebSocket.handle = nullptr;
#endif

        // Ids are per initialized instance: they restart after HCCleanup/HCInitialize.
        for (auto& call : calls)
        {
            VERIFY_SUCCEEDED(HCHttpCallCloseHandle(call.handle));
            call.handle = nullptr;
        }
        HCCleanup();
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        RoutedScopedCall fresh;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&fresh.handle));
        VERIFY_ARE_EQUAL(1u, HCHttpCallGetId(fresh.handle));
    }

    DEFINE_TEST_CASE(TestCallGetRequestUrl)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCallGetRequestUrl);

        ScriptedServer server;
        RoutedProviderScope scope{ ScriptedPerform, &server };
        VERIFY_SUCCEEDED(scope.Initialize());

        RoutedScopedCall call;
        VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));

        const char* url{ nullptr };
        VERIFY_SUCCEEDED(HCHttpCallGetRequestUrl(call.handle, &url));
        VERIFY_ARE_EQUAL_STR("", url);

        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/a?b=c"));
        VERIFY_SUCCEEDED(HCHttpCallGetRequestUrl(call.handle, &url));
        VERIFY_ARE_EQUAL_STR("https://example.test/a?b=c", url);

        // Setting the url again replaces it; the provider sees the final url.
        VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "POST", "https://example.test/final"));
        VERIFY_SUCCEEDED(HCHttpCallGetRequestUrl(call.handle, &url));
        VERIFY_ARE_EQUAL_STR("https://example.test/final", url);

        VERIFY_SUCCEEDED(RoutedPerformAndWait(call.handle));
        VERIFY_ARE_EQUAL(1u, server.urlsSeen.size());
        VERIFY_ARE_EQUAL_STR("https://example.test/final", server.urlsSeen[0].c_str());

        // The url is frozen after perform.
        VERIFY_ARE_EQUAL(E_HC_PERFORM_ALREADY_CALLED, HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/other"));
        VERIFY_SUCCEEDED(HCHttpCallGetRequestUrl(call.handle, &url));
        VERIFY_ARE_EQUAL_STR("https://example.test/final", url);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallGetRequestUrl(nullptr, &url));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallGetRequestUrl(call.handle, nullptr));
    }

    DEFINE_TEST_CASE(TestCallSetTracing)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCallSetTracing);

        ScriptedServer server;
        RoutedProviderScope scope{ ScriptedPerform, &server };
        VERIFY_SUCCEEDED(scope.Initialize());

        CallRouteRecorder recorder;
        int32_t handlerId = HCAddCallRoutedHandler(RecordCallRoute, &recorder);

        // Tracing is on by default; turning it off does not change call behaviour.
        for (bool traceCall : { false, true })
        {
            RoutedScopedCall call;
            VERIFY_SUCCEEDED(HCHttpCallCreate(&call.handle));
            VERIFY_IS_TRUE(call.handle->traceCall);
            VERIFY_SUCCEEDED(HCHttpCallSetTracing(call.handle, traceCall));
            VERIFY_ARE_EQUAL(traceCall, call.handle->traceCall);
            VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call.handle, "GET", "https://example.test/traced"));
            VERIFY_SUCCEEDED(HCHttpCallRequestSetHeader(call.handle, "Authorization", "secret", false));
            VERIFY_SUCCEEDED(RoutedPerformAndWait(call.handle));

            uint32_t status{ 0 };
            VERIFY_SUCCEEDED(HCHttpCallResponseGetStatusCode(call.handle, &status));
            VERIFY_ARE_EQUAL(200u, status);

            // Tracing may be toggled after perform (it is not a request setting).
            VERIFY_SUCCEEDED(HCHttpCallSetTracing(call.handle, !traceCall));
        }

        VERIFY_ARE_EQUAL(2u, recorder.Count());
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallSetTracing(nullptr, true));
        HCRemoveCallRoutedHandler(handlerId);
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
