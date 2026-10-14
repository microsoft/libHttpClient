// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

//
// Leak checks: every allocation libHttpClient makes through HCMemSetFunctions must be freed by the
// time HCCleanup returns. Each test installs LeakCheckAllocator (a counting allocator) before
// HCInitialize and verifies the alloc/free counts balance after HCCleanup.
//

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"jasonsa"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../global/global.h"
#include "Platform/ExternalHttpProvider.h"
#include "Platform/ExternalWebSocketProvider.h"
#include <httpClient/httpProvider.h>

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

constexpr char c_responseBody[]{ "leak check response body" };

std::atomic<uint32_t> s_performCalls{ 0 };

// Stands in for a platform HTTP stack: fills in a response (which libHttpClient allocates) and
// completes the call.
void CALLBACK RespondingPerform(HCCallHandle call, XAsyncBlock* asyncBlock, void*, HCPerformEnv) noexcept
{
    ++s_performCalls;
    HCHttpCallResponseSetStatusCode(call, 200);
    HCHttpCallResponseSetHeader(call, "Content-Type", "text/plain");
    HCHttpCallResponseSetResponseBodyBytes(call, reinterpret_cast<uint8_t const*>(c_responseBody), sizeof(c_responseBody) - 1);
    XAsyncComplete(asyncBlock, S_OK, 0);
}

std::atomic<uint32_t> s_webSocketConnects{ 0 };
std::atomic<uint32_t> s_webSocketSends{ 0 };
std::atomic<uint32_t> s_webSocketDisconnects{ 0 };

HRESULT CALLBACK LeakTestWebSocketConnect(
    _In_z_ PCSTR /*uri*/,
    _In_z_ PCSTR /*subProtocol*/,
    _In_ HCWebsocketHandle websocket,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/,
    _In_ HCPerformEnv /*env*/
) noexcept
{
    return XAsyncBegin(asyncBlock, websocket, reinterpret_cast<void*>(HCWebSocketConnectAsync), __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            switch (op)
            {
            case XAsyncOp::Begin:
                ++s_webSocketConnects;
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

HRESULT CALLBACK LeakTestWebSocketSend(
    _In_ HCWebsocketHandle /*websocket*/,
    _In_z_ PCSTR /*message*/,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* /*context*/
) noexcept
{
    return XAsyncBegin(asyncBlock, nullptr, nullptr, __FUNCTION__,
        [](XAsyncOp op, const XAsyncProviderData* data)
        {
            if (op == XAsyncOp::Begin)
            {
                ++s_webSocketSends;
                XAsyncComplete(data->async, S_OK, 0);
            }
            return S_OK;
        });
}

HRESULT CALLBACK LeakTestWebSocketSendBinary(
    _In_ HCWebsocketHandle websocket,
    _In_reads_bytes_(payloadSize) const uint8_t* /*payloadBytes*/,
    _In_ uint32_t /*payloadSize*/,
    _Inout_ XAsyncBlock* asyncBlock,
    _In_opt_ void* context
) noexcept
{
    return LeakTestWebSocketSend(websocket, "", asyncBlock, context);
}

HRESULT CALLBACK LeakTestWebSocketDisconnect(
    _In_ HCWebsocketHandle websocket,
    _In_ HCWebSocketCloseStatus closeStatus,
    _In_opt_ void* /*context*/
) noexcept
{
    ++s_webSocketDisconnects;

    // A real provider reports the close back to libHttpClient; cleanup depends on it.
    HCWebSocketCloseEventFunction closeFunc{ nullptr };
    void* closeContext{ nullptr };
    if (SUCCEEDED(HCWebSocketGetEventFunctions(websocket, nullptr, nullptr, &closeFunc, &closeContext)) && closeFunc != nullptr)
    {
        closeFunc(websocket, closeStatus, closeContext);
    }
    return S_OK;
}

// Runs one HTTP call to completion on a thread pool queue and returns its response body.
std::string PerformCall(char const* url)
{
    HCCallHandle call{ nullptr };
    VERIFY_SUCCEEDED(HCHttpCallCreate(&call));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetUrl(call, "POST", url));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetHeader(call, "X-Test", "leak-check", true));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetRequestBodyString(call, "request body"));
    VERIFY_SUCCEEDED(HCHttpCallRequestSetRetryAllowed(call, false));

    XAsyncBlock asyncBlock{};
    VERIFY_SUCCEEDED(HCHttpCallPerformAsync(call, &asyncBlock));
    VERIFY_SUCCEEDED(XAsyncGetStatus(&asyncBlock, true));

    uint32_t statusCode{ 0 };
    VERIFY_SUCCEEDED(HCHttpCallResponseGetStatusCode(call, &statusCode));
    VERIFY_ARE_EQUAL_UINT(200u, statusCode);

    char const* body{ nullptr };
    VERIFY_SUCCEEDED(HCHttpCallResponseGetResponseString(call, &body));
    std::string result{ body };

    VERIFY_SUCCEEDED(HCHttpCallCloseHandle(call));
    return result;
}

// Verifies everything allocated through the leak-check allocator since Install() has been freed.
void VerifyNoLeaks()
{
    VERIFY_IS_FALSE(HCIsInitialized());
    bool const balanced = LeakCheckAllocator::WaitForBalance();
    if (!balanced)
    {
        std::string message = "Outstanding libHttpClient allocations after HCCleanup: " +
            std::to_string(LeakCheckAllocator::OutstandingAllocations()) + " (" +
            std::to_string(LeakCheckAllocator::AllocCount()) + " allocs, " +
            std::to_string(LeakCheckAllocator::FreeCount()) + " frees)";
        TEST_LOG(std::wstring(message.begin(), message.end()).c_str());
    }
    VERIFY_IS_TRUE(balanced);
}

} // namespace

DEFINE_TEST_CLASS(MemoryLeakTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(MemoryLeakTests);

    DEFINE_TEST_CASE(TestLeakCheckAllocatorCountsAllocations)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestLeakCheckAllocatorCountsAllocations);

        VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
        VERIFY_IS_TRUE(LeakCheckAllocator::IsInstalled());
        VERIFY_ARE_EQUAL_UINT(0u, LeakCheckAllocator::AllocCount());

        // The checker must see an outstanding block, or a passing leak test proves nothing.
        void* block = http_memory::mem_alloc(64);
        VERIFY_IS_NOT_NULL(block);
        VERIFY_ARE_EQUAL_UINT(1u, LeakCheckAllocator::AllocCount());
        VERIFY_ARE_EQUAL_INT(1, LeakCheckAllocator::OutstandingAllocations());
        VERIFY_IS_FALSE(LeakCheckAllocator::WaitForBalance(0));

        http_memory::mem_free(block);
        VERIFY_ARE_EQUAL_UINT(1u, LeakCheckAllocator::FreeCount());
        VERIFY_IS_TRUE(LeakCheckAllocator::WaitForBalance(0));

        // Install is only legal before HCInitialize, like HCMemSetFunctions.
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        VERIFY_ARE_EQUAL(E_HC_ALREADY_INITIALISED, LeakCheckAllocator::Install());
        VERIFY_ARE_EQUAL(E_HC_ALREADY_INITIALISED, LeakCheckAllocator::Uninstall());
        HCCleanup();

        VerifyNoLeaks();
        VERIFY_SUCCEEDED(LeakCheckAllocator::Uninstall());
        VERIFY_IS_FALSE(LeakCheckAllocator::IsInstalled());
    }

    DEFINE_TEST_CASE(TestNoLeaksAfterInitializeAndCleanup)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNoLeaksAfterInitializeAndCleanup);

        VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
        VERIFY_SUCCEEDED(HCInitialize(nullptr));
        VERIFY_IS_TRUE(LeakCheckAllocator::AllocCount() > 0);
        HCCleanup();

        VerifyNoLeaks();
        VERIFY_SUCCEEDED(LeakCheckAllocator::Uninstall());
    }

    DEFINE_TEST_CASE(TestNoLeaksAfterMockedHttpCall)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNoLeaksAfterMockedHttpCall);

        VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        constexpr char mockUrl[]{ "https://example.com/leakcheck" };
        HCMockCallHandle mock{ nullptr };
        VERIFY_SUCCEEDED(HCMockCallCreate(&mock));
        VERIFY_SUCCEEDED(HCMockResponseSetStatusCode(mock, 200));
        VERIFY_SUCCEEDED(HCMockResponseSetHeader(mock, "Content-Type", "text/plain"));
        VERIFY_SUCCEEDED(HCMockResponseSetResponseBodyBytes(mock, reinterpret_cast<uint8_t const*>(c_responseBody), sizeof(c_responseBody) - 1));
        VERIFY_SUCCEEDED(HCMockAddMock(mock, "POST", mockUrl, nullptr, 0));
        // HCMockAddMock takes its own reference; HCCleanup releases that one.
        VERIFY_SUCCEEDED(HCMockCallCloseHandle(mock));

        uint64_t const allocsBeforeCall = LeakCheckAllocator::AllocCount();
        VERIFY_ARE_EQUAL_STR(c_responseBody, PerformCall(mockUrl).c_str());
        VERIFY_ARE_EQUAL_STR(c_responseBody, PerformCall(mockUrl).c_str());
        VERIFY_IS_TRUE(LeakCheckAllocator::AllocCount() > allocsBeforeCall);

        // The mock is still registered; HCCleanup releases it.
        HCCleanup();

        VerifyNoLeaks();
        VERIFY_SUCCEEDED(LeakCheckAllocator::Uninstall());
    }

    DEFINE_TEST_CASE(TestNoLeaksAfterHttpCallWithPerformCallback)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNoLeaksAfterHttpCallWithPerformCallback);

        s_performCalls = 0;
        VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
        VERIFY_SUCCEEDED(HCSetHttpCallPerformFunction(&RespondingPerform, nullptr));
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        VERIFY_ARE_EQUAL_STR(c_responseBody, PerformCall("https://example.com/perform").c_str());
        VERIFY_ARE_EQUAL_STR(c_responseBody, PerformCall("https://example.com/perform").c_str());
        VERIFY_ARE_EQUAL_UINT(2u, s_performCalls.load());

        HCCleanup();

        VerifyNoLeaks();
        VERIFY_SUCCEEDED(LeakCheckAllocator::Uninstall());
        ExternalHttpProvider::Get().ClearCallback();
    }

    DEFINE_TEST_CASE(TestNoLeaksAfterWebSocketCreateConnectClose)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNoLeaksAfterWebSocketCreateConnectClose);

        s_webSocketConnects = 0;
        s_webSocketSends = 0;
        s_webSocketDisconnects = 0;

        VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
        VERIFY_SUCCEEDED(HCSetWebSocketFunctions(LeakTestWebSocketConnect, LeakTestWebSocketSend, LeakTestWebSocketSendBinary, LeakTestWebSocketDisconnect, nullptr));
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        // A handle that is created and closed without ever connecting.
        {
            HCWebsocketHandle websocket{ nullptr };
            VERIFY_SUCCEEDED(HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
            VERIFY_SUCCEEDED(HCWebSocketSetHeader(websocket, "X-Test", "leak-check"));
            HCWebsocketHandle duplicate = HCWebSocketDuplicateHandle(websocket);
            VERIFY_IS_NOT_NULL(duplicate);
            VERIFY_SUCCEEDED(HCWebSocketCloseHandle(duplicate));
            VERIFY_SUCCEEDED(HCWebSocketCloseHandle(websocket));
        }

        // A handle that connects, sends and disconnects.
        {
            HCWebsocketHandle websocket{ nullptr };
            VERIFY_SUCCEEDED(HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));

            XAsyncBlock connectAsync{};
            VERIFY_SUCCEEDED(HCWebSocketConnectAsync("wss://example.com/leakcheck", "proto", websocket, &connectAsync));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&connectAsync, true));
            WebSocketCompletionResult connectResult{};
            VERIFY_SUCCEEDED(HCGetWebSocketConnectResult(&connectAsync, &connectResult));
            VERIFY_SUCCEEDED(connectResult.errorCode);

            XAsyncBlock sendAsync{};
            VERIFY_SUCCEEDED(HCWebSocketSendMessageAsync(websocket, "hello", &sendAsync));
            VERIFY_SUCCEEDED(XAsyncGetStatus(&sendAsync, true));

            VERIFY_SUCCEEDED(HCWebSocketDisconnect(websocket));
            VERIFY_SUCCEEDED(HCWebSocketCloseHandle(websocket));
        }

        VERIFY_ARE_EQUAL_UINT(1u, s_webSocketConnects.load());
        VERIFY_ARE_EQUAL_UINT(1u, s_webSocketSends.load());
        VERIFY_ARE_EQUAL_UINT(1u, s_webSocketDisconnects.load());

        HCCleanup();

        VerifyNoLeaks();
        VERIFY_SUCCEEDED(LeakCheckAllocator::Uninstall());
        ExternalWebSocketProvider::Get().ClearCallbacks();
    }

    DEFINE_TEST_CASE(TestNoLeaksAfterCleanupWithOpenWebSocket)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestNoLeaksAfterCleanupWithOpenWebSocket);

        // HCCleanup disconnects a still-connected WebSocket; once the client closes its handle,
        // nothing may be left behind.
        s_webSocketDisconnects = 0;
        VERIFY_SUCCEEDED(LeakCheckAllocator::Install());
        VERIFY_SUCCEEDED(HCSetWebSocketFunctions(LeakTestWebSocketConnect, LeakTestWebSocketSend, LeakTestWebSocketSendBinary, LeakTestWebSocketDisconnect, nullptr));
        VERIFY_SUCCEEDED(HCInitialize(nullptr));

        HCWebsocketHandle websocket{ nullptr };
        VERIFY_SUCCEEDED(HCWebSocketCreate(&websocket, nullptr, nullptr, nullptr, nullptr));
        XAsyncBlock connectAsync{};
        VERIFY_SUCCEEDED(HCWebSocketConnectAsync("wss://example.com/leakcheck", "", websocket, &connectAsync));
        VERIFY_SUCCEEDED(XAsyncGetStatus(&connectAsync, true));
        // Retrieving the result is what releases the connect operation's state.
        WebSocketCompletionResult connectResult{};
        VERIFY_SUCCEEDED(HCGetWebSocketConnectResult(&connectAsync, &connectResult));
        VERIFY_SUCCEEDED(connectResult.errorCode);

        HCCleanup();
        VERIFY_ARE_EQUAL_UINT(1u, s_webSocketDisconnects.load());
        VERIFY_SUCCEEDED(HCWebSocketCloseHandle(websocket));

        VerifyNoLeaks();
        VERIFY_SUCCEEDED(LeakCheckAllocator::Uninstall());
        ExternalWebSocketProvider::Get().ClearCallbacks();
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
