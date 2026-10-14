// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"libHttpClient"
#include "DefineTestMacros.h"
#include "utils.h"
#include "../Global/global.h"
#include "WebSocket/hcwebsocket.h"
#include "Platform/ExternalWebSocketProvider.h"
#include <httpClient/httpClient.h>
#include <httpClient/httpProvider.h>

#include <limits>
#include <string>
#include <vector>

#pragma warning(disable:4389)

// The binary-fragment callback APIs only exist for the Win32 / GDK WinHTTP provider, and the
// proxy-decrypts-HTTPS API only on Win32 / GDK desktop (see httpClient.h).
#define WSAPI_HAS_FRAGMENT_API (HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK)
#define WSAPI_HAS_PROXY_DECRYPTS_HTTPS (HC_PLATFORM_IS_MICROSOFT && (HC_PLATFORM != HC_PLATFORM_UWP) && (HC_PLATFORM != HC_PLATFORM_XDK))

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

//------------------------------------------------------------------------------
// Test WebSocket provider installed with HCSetWebSocketFunctions
//------------------------------------------------------------------------------

struct WsApiProviderState
{
    uint32_t connectCalls{ 0 };
    HCWebsocketHandle providerHandle{ nullptr };
    std::string connectUri;
    std::string connectSubProtocol;
    void* connectContext{ nullptr };

    uint32_t sendCalls{ 0 };
    std::string sentMessage;

    uint32_t sendBinaryCalls{ 0 };
    HCWebsocketHandle sendBinaryHandle{ nullptr };
    std::vector<uint8_t> sentBinary;
    void* sendBinaryContext{ nullptr };

    uint32_t disconnectCalls{ 0 };
    HCWebSocketCloseStatus disconnectStatus{ HCWebSocketCloseStatus::UnknownError };
};

WsApiProviderState g_wsApiProvider{};
int g_wsApiProviderContextTag{ 0 };

HRESULT CALLBACK WsApiConnect(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context,
    _In_ HCPerformEnv /*env*/)
{
    ++g_wsApiProvider.connectCalls;
    g_wsApiProvider.providerHandle = websocket;
    g_wsApiProvider.connectUri = uri;
    g_wsApiProvider.connectSubProtocol = subProtocol;
    g_wsApiProvider.connectContext = context;

    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
                XAsyncComplete(data->async, S_OK, sizeof(WebSocketCompletionResult));
                return S_OK;
            case XAsyncOp::GetResult:
            {
                RETURN_HR_IF(E_NOT_SUFFICIENT_BUFFER, data->bufferSize < sizeof(WebSocketCompletionResult));
                auto result = static_cast<WebSocketCompletionResult*>(data->buffer);
                *result = WebSocketCompletionResult{};
                result->errorCode = S_OK;
                result->websocket = static_cast<HCWebsocketHandle>(data->context);
                return S_OK;
            }
            default:
                return S_OK;
            }
        });
}

HRESULT CompleteImmediately(XAsyncBlock* asyncBlock, char const* identityName)
{
    return XAsyncBegin(asyncBlock, nullptr, nullptr, identityName,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            if (op == XAsyncOp::Begin)
            {
                XAsyncComplete(data->async, S_OK, 0);
            }
            return S_OK;
        });
}

HRESULT CALLBACK WsApiSend(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_z_ const char* message,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/)
{
    ++g_wsApiProvider.sendCalls;
    g_wsApiProvider.sentMessage = message;
    return CompleteImmediately(asyncBlock, __FUNCTION__);
}

HRESULT CALLBACK WsApiSendBinary(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* payloadBytes,
    _In_ uint32_t payloadSize,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context)
{
    ++g_wsApiProvider.sendBinaryCalls;
    g_wsApiProvider.sendBinaryHandle = websocket;
    g_wsApiProvider.sentBinary.assign(payloadBytes, payloadBytes + payloadSize);
    g_wsApiProvider.sendBinaryContext = context;
    return CompleteImmediately(asyncBlock, __FUNCTION__);
}

// Behaves like a provider whose close handshake completes synchronously: reports the close back through the
// provider-side close function with the requested status.
HRESULT CALLBACK WsApiDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* /*context*/)
{
    ++g_wsApiProvider.disconnectCalls;
    g_wsApiProvider.disconnectStatus = closeStatus;

    HCWebSocketCloseEventFunction closeFunc = nullptr;
    void* closeContext = nullptr;
    RETURN_IF_FAILED(HCWebSocketGetEventFunctions(websocket, nullptr, nullptr, &closeFunc, &closeContext));
    closeFunc(websocket, closeStatus, closeContext);
    return S_OK;
}

//------------------------------------------------------------------------------
// Client-side event recording
//------------------------------------------------------------------------------

struct WsApiClientLog
{
    std::vector<std::string> events;
    std::vector<std::vector<uint8_t>> payloads;
    std::vector<HCWebsocketHandle> handles;
    std::vector<void*> contexts;
    HCWebSocketCloseStatus closeStatus{ HCWebSocketCloseStatus::UnknownError };
    uint32_t closeCalls{ 0 };

    void Record(HCWebsocketHandle handle, void* context, std::string event, uint8_t const* bytes, uint32_t size)
    {
        events.push_back(std::move(event));
        payloads.emplace_back(bytes, bytes + size);
        handles.push_back(handle);
        contexts.push_back(context);
    }
};

void CALLBACK WsApiClientMessage(HCWebsocketHandle websocket, const char* message, void* context)
{
    static_cast<WsApiClientLog*>(context)->Record(websocket, context, std::string{ "message:" } + message, nullptr, 0);
}

void CALLBACK WsApiClientBinary(HCWebsocketHandle websocket, const uint8_t* bytes, uint32_t size, void* context)
{
    static_cast<WsApiClientLog*>(context)->Record(websocket, context, "binary:" + std::to_string(size), bytes, size);
}

void CALLBACK WsApiClientFragment(HCWebsocketHandle websocket, const uint8_t* bytes, uint32_t size, bool isLastFragment, void* context)
{
    static_cast<WsApiClientLog*>(context)->Record(
        websocket,
        context,
        "fragment:" + std::to_string(size) + (isLastFragment ? ":last" : ":more"),
        bytes,
        size);
}

void CALLBACK WsApiClientClose(HCWebsocketHandle websocket, HCWebSocketCloseStatus status, void* context)
{
    auto log = static_cast<WsApiClientLog*>(context);
    ++log->closeCalls;
    log->closeStatus = status;
    log->Record(websocket, context, "close:" + std::to_string(static_cast<uint32_t>(status)), nullptr, 0);
}

// Installs the test provider, initializes the library and always undoes both, even when a VERIFY throws.
class WsApiScope
{
public:
    WsApiScope() noexcept
    {
        xbox::httpclient::ExternalWebSocketProvider::Get().ClearCallbacks();
        g_wsApiProvider = WsApiProviderState{};
        m_setFunctionsResult = HCSetWebSocketFunctions(WsApiConnect, WsApiSend, WsApiSendBinary, WsApiDisconnect, &g_wsApiProviderContextTag);
        m_initResult = HCInitialize(nullptr);
    }

    ~WsApiScope()
    {
        if (SUCCEEDED(m_initResult))
        {
            HCCleanup();
        }
        xbox::httpclient::ExternalWebSocketProvider::Get().ClearCallbacks();
        g_wsApiProvider = WsApiProviderState{};
    }

    WsApiScope(WsApiScope const&) = delete;
    WsApiScope& operator=(WsApiScope const&) = delete;

    void VerifyInitialized() const
    {
        VERIFY_ARE_EQUAL(S_OK, m_setFunctionsResult);
        VERIFY_ARE_EQUAL(S_OK, m_initResult);
    }

private:
    HRESULT m_setFunctionsResult{ E_FAIL };
    HRESULT m_initResult{ E_FAIL };
};

HCWebsocketHandle CreateWebSocket(WsApiClientLog* log)
{
    HCWebsocketHandle websocket = nullptr;
    VERIFY_ARE_EQUAL(S_OK, HCWebSocketCreate(&websocket, WsApiClientMessage, WsApiClientBinary, WsApiClientClose, log));
    VERIFY_IS_NOT_NULL(websocket);
    return websocket;
}

void ConnectWebSocket(HCWebsocketHandle websocket)
{
    XAsyncBlock asyncBlock{};
    VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("wss://example.com/socket", "test-protocol", websocket, &asyncBlock));
    VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&asyncBlock, true));

    size_t resultSize = 0;
    VERIFY_ARE_EQUAL(S_OK, XAsyncGetResultSize(&asyncBlock, &resultSize));
    VERIFY_ARE_EQUAL(sizeof(WebSocketCompletionResult), resultSize);

    WebSocketCompletionResult result{};
    VERIFY_ARE_EQUAL(S_OK, HCGetWebSocketConnectResult(&asyncBlock, &result));
    VERIFY_ARE_EQUAL(S_OK, result.errorCode);
    VERIFY_IS_TRUE(result.websocket == websocket);
    VERIFY_IS_NOT_NULL(g_wsApiProvider.providerHandle);
}

} // namespace

DEFINE_TEST_CLASS(WebSocketApiTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(WebSocketApiTests);

    DEFINE_TEST_CASE(TestSendBinaryMessageReachesProviderWithExactBytes)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSendBinaryMessageReachesProviderWithExactBytes);
        WsApiScope scope;
        scope.VerifyInitialized();

        WsApiClientLog log;
        HCWebsocketHandle websocket = CreateWebSocket(&log);

        uint8_t const payload[] = { 0x00, 0x01, 0x7F, 0x80, 0xFF, 'a', 0x00 };

        // Argument validation and "not connected" happen before the provider is reached.
        XAsyncBlock rejected{};
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSendBinaryMessageAsync(nullptr, payload, sizeof(payload), &rejected));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSendBinaryMessageAsync(websocket, nullptr, sizeof(payload), &rejected));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSendBinaryMessageAsync(websocket, payload, 0, &rejected));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSendBinaryMessageAsync(websocket, payload, sizeof(payload), &rejected));
        VERIFY_ARE_EQUAL(0u, g_wsApiProvider.sendBinaryCalls);

        ConnectWebSocket(websocket);
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.connectCalls);
        VERIFY_ARE_EQUAL_STR("wss://example.com/socket", g_wsApiProvider.connectUri.c_str());
        VERIFY_ARE_EQUAL_STR("test-protocol", g_wsApiProvider.connectSubProtocol.c_str());
        VERIFY_IS_TRUE(g_wsApiProvider.connectContext == &g_wsApiProviderContextTag);

        XAsyncBlock sendAsync{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSendBinaryMessageAsync(websocket, payload, sizeof(payload), &sendAsync));
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&sendAsync, true));
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.sendBinaryCalls);
        VERIFY_ARE_EQUAL(sizeof(payload), g_wsApiProvider.sentBinary.size());
        VERIFY_IS_TRUE(memcmp(payload, g_wsApiProvider.sentBinary.data(), sizeof(payload)) == 0);
        VERIFY_IS_TRUE(g_wsApiProvider.sendBinaryContext == &g_wsApiProviderContextTag);
        VERIFY_IS_TRUE(g_wsApiProvider.sendBinaryHandle == g_wsApiProvider.providerHandle);

        // A larger payload is forwarded intact as well.
        std::vector<uint8_t> large(70000);
        for (size_t i = 0; i < large.size(); ++i)
        {
            large[i] = static_cast<uint8_t>((i * 31) & 0xFF);
        }
        XAsyncBlock largeAsync{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSendBinaryMessageAsync(websocket, large.data(), static_cast<uint32_t>(large.size()), &largeAsync));
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&largeAsync, true));
        VERIFY_ARE_EQUAL(2u, g_wsApiProvider.sendBinaryCalls);
        VERIFY_IS_TRUE(g_wsApiProvider.sentBinary == large);

        // Text sends go to the text function, not the binary one.
        XAsyncBlock textAsync{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSendMessageAsync(websocket, "hello", &textAsync));
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&textAsync, true));
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.sendCalls);
        VERIFY_ARE_EQUAL_STR("hello", g_wsApiProvider.sentMessage.c_str());
        VERIFY_ARE_EQUAL(2u, g_wsApiProvider.sendBinaryCalls);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
    }

#if WSAPI_HAS_FRAGMENT_API
    DEFINE_TEST_CASE(TestBinaryMessageFragmentFunctions)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestBinaryMessageFragmentFunctions);
        WsApiScope scope;
        scope.VerifyInitialized();

        WsApiClientLog log;
        HCWebsocketHandle websocket = CreateWebSocket(&log);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetBinaryMessageFragmentEventFunction(nullptr, WsApiClientFragment));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetBinaryMessageFragmentEventFunction(websocket, WsApiClientFragment));

        // The getter is the provider-side API: it returns the library's dispatcher (not the client function),
        // which routes fragments to the client's handler.
        HCWebSocketBinaryMessageFragmentFunction providerFragmentFunc = nullptr;
        void* providerContext = &log;
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetBinaryMessageFragmentEventFunction(nullptr, &providerFragmentFunc, &providerContext));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetBinaryMessageFragmentEventFunction(websocket, nullptr, &providerContext));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetBinaryMessageFragmentEventFunction(websocket, &providerFragmentFunc, nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetBinaryMessageFragmentEventFunction(websocket, &providerFragmentFunc, &providerContext));
        VERIFY_IS_NOT_NULL(providerFragmentFunc);
        VERIFY_IS_TRUE(providerFragmentFunc != WsApiClientFragment);
        VERIFY_IS_NULL(providerContext);

        ConnectWebSocket(websocket);
        HCWebsocketHandle providerHandle = g_wsApiProvider.providerHandle;

        uint8_t const first[] = { 0x10, 0x20, 0x30 };
        uint8_t const last[] = { 0x40 };
        providerFragmentFunc(providerHandle, first, sizeof(first), false, providerContext);
        providerFragmentFunc(providerHandle, last, sizeof(last), true, providerContext);

        VERIFY_ARE_EQUAL(2u, static_cast<uint32_t>(log.events.size()));
        VERIFY_ARE_EQUAL_STR("fragment:3:more", log.events[0].c_str());
        VERIFY_ARE_EQUAL_STR("fragment:1:last", log.events[1].c_str());
        VERIFY_IS_TRUE(log.payloads[0] == std::vector<uint8_t>(first, first + sizeof(first)));
        VERIFY_IS_TRUE(log.payloads[1] == std::vector<uint8_t>(last, last + sizeof(last)));
        VERIFY_IS_TRUE(log.handles[0] == websocket);
        VERIFY_IS_TRUE(log.contexts[0] == &log);

        // Without a fragment handler, fragments are not delivered at all (documented on
        // HCWebSocketSetBinaryMessageFragmentEventFunction); they do not fall back to the binary message handler.
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetBinaryMessageFragmentEventFunction(websocket, nullptr));
        providerFragmentFunc(providerHandle, first, sizeof(first), false, providerContext);
        VERIFY_ARE_EQUAL(2u, static_cast<uint32_t>(log.events.size()));

        // Complete binary messages still reach the binary handler.
        HCWebSocketBinaryMessageFunction providerBinaryFunc = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetEventFunctions(providerHandle, nullptr, &providerBinaryFunc, nullptr, nullptr));
        providerBinaryFunc(providerHandle, first, sizeof(first), nullptr);
        VERIFY_ARE_EQUAL(3u, static_cast<uint32_t>(log.events.size()));
        VERIFY_ARE_EQUAL_STR("binary:3", log.events[2].c_str());
        VERIFY_IS_TRUE(log.payloads[2] == std::vector<uint8_t>(first, first + sizeof(first)));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
    }
#endif

    DEFINE_TEST_CASE(TestPingIntervalRoundTrip)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestPingIntervalRoundTrip);
        WsApiScope scope;
        scope.VerifyInitialized();

        WsApiClientLog log;
        HCWebsocketHandle websocket = CreateWebSocket(&log);

        uint32_t interval = 12345;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetPingInterval(websocket, &interval));
        VERIFY_ARE_EQUAL(0u, interval);

        // No range validation: any value, including 0 (disabled) and UINT32_MAX, round-trips.
        uint32_t const values[] = { 30u, 1u, 0u, (std::numeric_limits<uint32_t>::max)(), 60u };
        for (uint32_t value : values)
        {
            VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetPingInterval(websocket, value));
            VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetPingInterval(websocket, &interval));
            VERIFY_IS_TRUE(value == interval);
        }

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetPingInterval(nullptr, 10));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetPingInterval(nullptr, &interval));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetPingInterval(websocket, nullptr));

        // The interval is fixed once connect has been called.
        ConnectWebSocket(websocket);
        VERIFY_ARE_EQUAL(E_HC_CONNECT_ALREADY_CALLED, HCWebSocketSetPingInterval(websocket, 5));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetPingInterval(websocket, &interval));
        VERIFY_ARE_EQUAL(60u, interval);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
    }

#if WSAPI_HAS_PROXY_DECRYPTS_HTTPS
    DEFINE_TEST_CASE(TestProxyDecryptsHttpsRequiresProxyUri)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestProxyDecryptsHttpsRequiresProxyUri);
        WsApiScope scope;
        scope.VerifyInitialized();

        WsApiClientLog log;
        HCWebsocketHandle websocket = CreateWebSocket(&log);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketSetProxyDecryptsHttps(nullptr, true));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSetProxyDecryptsHttps(websocket, true));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSetProxyDecryptsHttps(websocket, false));
        VERIFY_IS_FALSE(websocket->websocket->ProxyDecryptsHttps());

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyUri(websocket, "http://127.0.0.1:8888"));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyDecryptsHttps(websocket, true));
        VERIFY_IS_TRUE(websocket->websocket->ProxyDecryptsHttps());
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyDecryptsHttps(websocket, false));
        VERIFY_IS_FALSE(websocket->websocket->ProxyDecryptsHttps());

        // Changing the proxy URI resets the decrypt opt-in; clearing it makes the precondition fail again.
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyDecryptsHttps(websocket, true));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyUri(websocket, "http://127.0.0.1:9999"));
        VERIFY_IS_FALSE(websocket->websocket->ProxyDecryptsHttps());
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyUri(websocket, ""));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSetProxyDecryptsHttps(websocket, true));

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyUri(websocket, "http://127.0.0.1:8888"));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetProxyDecryptsHttps(websocket, true));

        // Both settings are fixed once connect has been called.
        ConnectWebSocket(websocket);
        VERIFY_ARE_EQUAL(E_HC_CONNECT_ALREADY_CALLED, HCWebSocketSetProxyDecryptsHttps(websocket, false));
        VERIFY_ARE_EQUAL(E_HC_CONNECT_ALREADY_CALLED, HCWebSocketSetProxyUri(websocket, "http://127.0.0.1:7777"));
        VERIFY_IS_TRUE(websocket->websocket->ProxyDecryptsHttps());
        char const* proxyUri = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetProxyUri(websocket, &proxyUri));
        VERIFY_ARE_EQUAL_STR("http://127.0.0.1:8888", proxyUri);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
    }
#endif

    DEFINE_TEST_CASE(TestProviderEventsReachClientHandlersInOrder)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestProviderEventsReachClientHandlersInOrder);
        WsApiScope scope;
        scope.VerifyInitialized();

        WsApiClientLog log;
        HCWebsocketHandle websocket = CreateWebSocket(&log);
#if WSAPI_HAS_FRAGMENT_API
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSetBinaryMessageFragmentEventFunction(websocket, WsApiClientFragment));
#endif
        ConnectWebSocket(websocket);
        HCWebsocketHandle providerHandle = g_wsApiProvider.providerHandle;

        HCWebSocketMessageFunction messageFunc = nullptr;
        HCWebSocketBinaryMessageFunction binaryFunc = nullptr;
        HCWebSocketCloseEventFunction closeFunc = nullptr;
        void* eventContext = &log;
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCWebSocketGetEventFunctions(nullptr, &messageFunc, &binaryFunc, &closeFunc, &eventContext));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetEventFunctions(providerHandle, nullptr, nullptr, nullptr, nullptr));
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetEventFunctions(providerHandle, &messageFunc, &binaryFunc, &closeFunc, &eventContext));
        VERIFY_IS_NOT_NULL(messageFunc);
        VERIFY_IS_NOT_NULL(binaryFunc);
        VERIFY_IS_NOT_NULL(closeFunc);
        VERIFY_IS_NULL(eventContext);

#if WSAPI_HAS_FRAGMENT_API
        HCWebSocketBinaryMessageFragmentFunction fragmentFunc = nullptr;
        void* fragmentContext = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetBinaryMessageFragmentEventFunction(providerHandle, &fragmentFunc, &fragmentContext));
#endif

        uint8_t const binary[] = { 0xDE, 0xAD, 0xBE };
#if WSAPI_HAS_FRAGMENT_API
        uint8_t const fragment[] = { 0xEF, 0x00 };
#endif
        messageFunc(providerHandle, "hello", eventContext);
        binaryFunc(providerHandle, binary, sizeof(binary), eventContext);
        messageFunc(providerHandle, "world", eventContext);
#if WSAPI_HAS_FRAGMENT_API
        fragmentFunc(providerHandle, fragment, sizeof(fragment), true, fragmentContext);
#endif
        closeFunc(providerHandle, HCWebSocketCloseStatus::GoingAway, eventContext);      // server-initiated close
        providerHandle = nullptr; // the provider's handle is released once the close is delivered

        std::vector<std::string> const expected
        {
            "message:hello",
            "binary:3",
            "message:world",
#if WSAPI_HAS_FRAGMENT_API
            "fragment:2:last",
#endif
            "close:1001",
        };
        VERIFY_ARE_EQUAL(static_cast<uint32_t>(expected.size()), static_cast<uint32_t>(log.events.size()));
        for (size_t i = 0; i < expected.size(); ++i)
        {
            VERIFY_ARE_EQUAL_STR(expected[i].c_str(), log.events[i].c_str());
            VERIFY_IS_TRUE(log.handles[i] == websocket);
            VERIFY_IS_TRUE(log.contexts[i] == &log);
        }
        VERIFY_IS_TRUE(log.payloads[1] == std::vector<uint8_t>(binary, binary + sizeof(binary)));
#if WSAPI_HAS_FRAGMENT_API
        VERIFY_IS_TRUE(log.payloads[3] == std::vector<uint8_t>(fragment, fragment + sizeof(fragment)));
#endif
        VERIFY_ARE_EQUAL(1u, log.closeCalls);

        // After a remote close the socket is disconnected; the provider is not called again.
        XAsyncBlock asyncBlock{};
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSendMessageAsync(websocket, "late", &asyncBlock));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSendBinaryMessageAsync(websocket, binary, sizeof(binary), &asyncBlock));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(0u, g_wsApiProvider.sendCalls);
        VERIFY_ARE_EQUAL(0u, g_wsApiProvider.sendBinaryCalls);
        VERIFY_ARE_EQUAL(0u, g_wsApiProvider.disconnectCalls);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
    }

    DEFINE_TEST_CASE(TestSendAfterDisconnect)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSendAfterDisconnect);
        WsApiScope scope;
        scope.VerifyInitialized();

        WsApiClientLog log;
        HCWebsocketHandle websocket = CreateWebSocket(&log);

        // Disconnect before connect is rejected.
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(0u, g_wsApiProvider.disconnectCalls);

        ConnectWebSocket(websocket);

        XAsyncBlock sendAsync{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketSendMessageAsync(websocket, "before", &sendAsync));
        VERIFY_ARE_EQUAL(S_OK, XAsyncGetStatus(&sendAsync, true));
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.sendCalls);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.disconnectCalls);
        VERIFY_ARE_EQUAL(1u, log.closeCalls);

        XAsyncBlock lateAsync{};
        uint8_t const bytes[] = { 1, 2, 3 };
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSendMessageAsync(websocket, "after", &lateAsync));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketSendBinaryMessageAsync(websocket, bytes, sizeof(bytes), &lateAsync));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, HCWebSocketDisconnect(websocket));
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.sendCalls);
        VERIFY_ARE_EQUAL(0u, g_wsApiProvider.sendBinaryCalls);
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.disconnectCalls);
        VERIFY_ARE_EQUAL(1u, log.closeCalls);

        // A disconnected handle cannot be reconnected.
        XAsyncBlock reconnect{};
        VERIFY_ARE_EQUAL(S_OK, HCWebSocketConnectAsync("wss://example.com/socket", "test-protocol", websocket, &reconnect));
        VERIFY_ARE_EQUAL(E_UNEXPECTED, XAsyncGetStatus(&reconnect, true));
        VERIFY_ARE_EQUAL(1u, g_wsApiProvider.connectCalls);

        VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
    }

    DEFINE_TEST_CASE(TestCloseStatusPropagation)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestCloseStatusPropagation);
        WsApiScope scope;
        scope.VerifyInitialized();

        HCWebSocketCloseStatus const statuses[] =
        {
            HCWebSocketCloseStatus::Normal,
            HCWebSocketCloseStatus::GoingAway,
            HCWebSocketCloseStatus::ProtocolError,
            HCWebSocketCloseStatus::PolicyViolation,
            HCWebSocketCloseStatus::TooLarge,
            HCWebSocketCloseStatus::ServerTerminate,
            HCWebSocketCloseStatus::UnknownError,
        };

        // Client-initiated: the requested status reaches the provider and comes back on the close event.
        for (auto status : statuses)
        {
            g_wsApiProvider.disconnectStatus = HCWebSocketCloseStatus::EmptyStatus;
            WsApiClientLog log;
            HCWebsocketHandle websocket = CreateWebSocket(&log);
            ConnectWebSocket(websocket);

            VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnectWithStatus(websocket, status));
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(status), static_cast<uint32_t>(g_wsApiProvider.disconnectStatus));
            VERIFY_ARE_EQUAL(1u, log.closeCalls);
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(status), static_cast<uint32_t>(log.closeStatus));

            VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        }

        // HCWebSocketDisconnect uses Normal.
        {
            WsApiClientLog log;
            HCWebsocketHandle websocket = CreateWebSocket(&log);
            ConnectWebSocket(websocket);
            VERIFY_ARE_EQUAL(S_OK, HCWebSocketDisconnect(websocket));
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCWebSocketCloseStatus::Normal), static_cast<uint32_t>(g_wsApiProvider.disconnectStatus));
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(HCWebSocketCloseStatus::Normal), static_cast<uint32_t>(log.closeStatus));
            VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        }

        // Provider-initiated: whatever status the provider reports is what the client sees.
        HCWebSocketCloseStatus const remoteStatuses[] =
        {
            HCWebSocketCloseStatus::AbnormalClose,
            HCWebSocketCloseStatus::HandshakeError,
            HCWebSocketCloseStatus::ErrorWinhttpTimeout,
        };
        for (auto status : remoteStatuses)
        {
            WsApiClientLog log;
            HCWebsocketHandle websocket = CreateWebSocket(&log);
            ConnectWebSocket(websocket);

            HCWebSocketCloseEventFunction closeFunc = nullptr;
            void* closeContext = nullptr;
            VERIFY_ARE_EQUAL(S_OK, HCWebSocketGetEventFunctions(g_wsApiProvider.providerHandle, nullptr, nullptr, &closeFunc, &closeContext));
            closeFunc(g_wsApiProvider.providerHandle, status, closeContext);

            VERIFY_ARE_EQUAL(1u, log.closeCalls);
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(status), static_cast<uint32_t>(log.closeStatus));
            VERIFY_ARE_EQUAL(S_OK, HCWebSocketCloseHandle(websocket));
        }
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
