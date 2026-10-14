// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "E2ETestFramework.h"

namespace e2e
{

// Owns an HCCallHandle; closes it on destruction so a failing CHECK doesn't leak the call.
class Call
{
public:
    Call();
    ~Call();
    Call(const Call&) = delete;
    Call& operator=(const Call&) = delete;

    HCCallHandle Get() const { return m_call; }
    operator HCCallHandle() const { return m_call; }

    Call& Url(const char* method, const std::string& url);
    Call& Header(const char* name, const char* value);
    Call& Body(const std::string& body);
    Call& Body(const std::vector<uint8_t>& body);
    Call& NoRetry();

    // Perform and wait. Returns the XAsync status (the HRESULT the caller's completion sees).
    HRESULT Perform(std::chrono::milliseconds timeout = std::chrono::seconds{ 60 });

    uint32_t Status() const;
    HRESULT NetworkError() const;
    uint32_t PlatformError() const;
    std::string ResponseString() const;
    std::vector<uint8_t> ResponseBytes() const;
    bool ResponseHeader(const char* name, std::string& value) const;

    // One-line summary for failure messages.
    std::string Describe() const;

private:
    HCCallHandle m_call{ nullptr };
};

// Perform a GET (or other method) with retries disabled and return the response body. Fails the
// test unless the call succeeds with HTTP 200.
std::string FetchString(const std::string& url, const char* method = "GET");

// Owns an HCWebsocketHandle and records everything it receives.
class WebSocket
{
public:
    WebSocket();
    ~WebSocket();
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    HCWebsocketHandle Get() const { return m_ws; }
    operator HCWebsocketHandle() const { return m_ws; }

    // Install a fragment handler (Win32/GDK legacy semantics) that accumulates fragments.
    void EnableFragmentHandler();

    HRESULT Connect(const std::string& url, const char* subprotocol = "", std::chrono::milliseconds timeout = std::chrono::seconds{ 15 });
    WebSocketCompletionResult ConnectResult() const { return m_connectResult; }

    HRESULT SendText(const std::string& text, std::chrono::milliseconds timeout = std::chrono::seconds{ 15 });
    HRESULT SendBinary(const std::vector<uint8_t>& data, std::chrono::milliseconds timeout = std::chrono::seconds{ 15 });

    // Wait until at least `count` text / binary messages have been received.
    bool WaitForText(size_t count, std::chrono::milliseconds timeout = std::chrono::seconds{ 15 });
    bool WaitForBinary(size_t count, std::chrono::milliseconds timeout = std::chrono::seconds{ 15 });
    bool WaitForFragmentedMessages(size_t count, std::chrono::milliseconds timeout = std::chrono::seconds{ 15 });
    bool WaitForClose(std::chrono::milliseconds timeout = std::chrono::seconds{ 15 });

    // Send "__stats__" and return the server's JSON reply.
    std::string ServerStats();

    std::vector<std::string> Texts();
    std::vector<std::vector<uint8_t>> Binaries();
    std::vector<std::vector<uint8_t>> FragmentedMessages();
    size_t FragmentCallbackCount();
    bool Closed();
    HCWebSocketCloseStatus CloseStatus();

    // Disconnect (if connected) and wait for the close callback.
    void DisconnectAndWait(std::chrono::milliseconds timeout = std::chrono::seconds{ 10 });

private:
    static void CALLBACK OnText(HCWebsocketHandle, const char* text, void* context);
    static void CALLBACK OnBinary(HCWebsocketHandle, const uint8_t* bytes, uint32_t size, void* context);
    static void CALLBACK OnFragment(HCWebsocketHandle, const uint8_t* bytes, uint32_t size, bool isLast, void* context);
    static void CALLBACK OnClose(HCWebsocketHandle, HCWebSocketCloseStatus status, void* context);

    struct State
    {
        std::mutex mutex;
        std::condition_variable cv;
        std::vector<std::string> texts;
        std::vector<std::vector<uint8_t>> binaries;
        std::vector<uint8_t> fragmentBuffer;
        std::vector<std::vector<uint8_t>> fragmentedMessages;
        size_t fragmentCallbacks{ 0 };
        bool closed{ false };
        HCWebSocketCloseStatus closeStatus{ HCWebSocketCloseStatus::Normal };
    };

    template<typename Pred>
    bool WaitFor(Pred pred, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock{ m_state->mutex };
        return m_state->cv.wait_for(lock, timeout, pred);
    }

    HCWebsocketHandle m_ws{ nullptr };
    WebSocketCompletionResult m_connectResult{};
    bool m_connected{ false };
    // Heap-allocated callback context. If the socket never reports close (a bug the test will
    // already have failed on) it is leaked rather than freed under a still-registered callback.
    State* m_state{ nullptr };
};

} // namespace e2e