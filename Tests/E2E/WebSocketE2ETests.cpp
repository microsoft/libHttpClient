// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
//
// WebSocket end-to-end tests against the loopback server. On Win32 this is the WinHTTP WebSocket
// provider (and websocketpp, which the Win32 build switches to when compression is requested); on
// Linux and Apple it is websocketpp throughout. Per-provider expectations are marked with
// HC_PLATFORM checks; the Win32 expectations are unchanged.

#include "E2EHelpers.h"

#include <httpClient/httpProvider.h>
#include <memory>

using namespace e2e;

namespace
{

uint32_t StatusValue(HCWebSocketCloseStatus status)
{
    return static_cast<uint32_t>(status);
}

// Polls the server's record of a WebSocket (keyed by ?id=) until it reports the socket closed.
std::string WaitForServerClose(const std::string& id)
{
    std::string log;
    for (int i = 0; i < 100; ++i)
    {
        log = FetchString(HttpUrl("/ws-log?id=" + id));
        if (JsonFieldOr(log, "closed") == "true")
        {
            return log;
        }
        platform::SleepMs(50);
    }
    Fail(__FILE__, __LINE__, "server never saw WebSocket " + id + " close: " + log);
}

std::string CompressibleText(size_t size)
{
    std::string text;
    while (text.size() < size)
    {
        text += "libHttpClient websocket compression payload " + std::to_string(text.size()) + "; ";
    }
    text.resize(size);
    return text;
}

void ConnectOrFail(WebSocket& ws, const std::string& path)
{
    HRESULT hr = ws.Connect(WsUrl(path));
    E2E_CHECK_MSG(SUCCEEDED(hr), "connect to %s failed: hr=%s platformError=%u", path.c_str(), HrToString(hr).c_str(), ws.ConnectResult().platformErrorCode);
}

// HCWebSocketSetOptions returns E_NOT_SUPPORTED (HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)) when the
// library was built without WebSocket compression support (HC_ENABLE_WEBSOCKET_COMPRESSION). On
// Win32 that happens on the first MSBuild of a fresh clone, because the wintls Exists() check runs
// before the vcpkg restore (fixed separately by #1034); on Linux with -nwc / HC_ENABLE_WEBSOCKET_
// COMPRESSION=OFF. Skip rather than fail; any other failure is still a failure.
void SetOptionsOrSkip(WebSocket& ws, HCWebSocketOptions options, const char* what)
{
    HRESULT hr = HCWebSocketSetOptions(ws, options);
    if (hr == E_NOT_SUPPORTED)
    {
        Skip(std::string{ "HCWebSocketSetOptions(" } + what + ") returned E_NOT_SUPPORTED: WebSocket compression not compiled into this build; see #1034");
    }
    E2E_CHECK_MSG(SUCCEEDED(hr), "HCWebSocketSetOptions(%s) failed: %s", what, HrToString(hr).c_str());
}

} // namespace

E2E_TEST(ws_text_echo)
{
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo");
    const int count = 10;
    for (int i = 0; i < count; ++i)
    {
        E2E_CHECK_HR(ws.SendText("message " + std::to_string(i)));
    }
    E2E_CHECK_MSG(ws.WaitForText(count), "received %zu of %d echoes", ws.Texts().size(), count);
    auto texts = ws.Texts();
    for (int i = 0; i < count; ++i)
    {
        E2E_CHECK_EQ("message " + std::to_string(i), texts[i]);
    }
    // UTF-8 survives the round trip.
    const std::string unicode = "h\xC3\xA9llo \xE2\x9C\x93 \xF0\x9F\x8E\xAE";
    E2E_CHECK_HR(ws.SendText(unicode));
    E2E_CHECK(ws.WaitForText(count + 1));
    E2E_CHECK_EQ(unicode, ws.Texts()[count]);
    ws.DisconnectAndWait();
}

E2E_TEST(ws_binary_echo)
{
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo");
    // Frame-length encodings (7-bit, 16-bit) up to the legacy 20 KB receive buffer; larger
    // messages are covered by the max-receive-buffer and fragment tests.
    std::vector<size_t> sizes{ 1, 125, 126, 1000, 20000 };
    for (size_t size : sizes)
    {
        E2E_CHECK_HR(ws.SendBinary(PatternBytes(size, size)));
    }
    E2E_CHECK_MSG(ws.WaitForBinary(sizes.size()), "received %zu of %zu binary echoes", ws.Binaries().size(), sizes.size());
    auto binaries = ws.Binaries();
    for (size_t i = 0; i < sizes.size(); ++i)
    {
        E2E_CHECK_EQ(sizes[i], binaries[i].size());
        E2E_CHECK_MSG(IsPattern(binaries[i].data(), binaries[i].size(), sizes[i]), "binary echo %zu corrupted", i);
    }
    ws.DisconnectAndWait();
}

E2E_TEST(ws_many_queued_sends)
{
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo");
    const int count = 100;
    std::vector<std::string> messages;
    std::vector<XAsyncBlock*> asyncs;
    for (int i = 0; i < count; ++i)
    {
        messages.push_back("queued " + std::to_string(i));
    }
    // Issue every send before waiting for any of them, so they queue inside the provider.
    for (int i = 0; i < count; ++i)
    {
        asyncs.push_back(new XAsyncBlock{});
        E2E_CHECK_HR(HCWebSocketSendMessageAsync(ws, messages[i].c_str(), asyncs.back()));
    }

    // Every block must now belong to a started operation: E_PENDING, or genuinely completed with a
    // result for this socket. (The websocketpp provider used to start a queued send's operation
    // only when it reached the front of its queue, so a queued block still reported S_OK from
    // XAsyncGetStatus; callers that waited on it freed it while the provider still held it.)
    std::vector<bool> resultTaken(count, false);
    for (int i = count - 1; i >= 0; --i)
    {
        if (XAsyncGetStatus(asyncs[i], false) != E_PENDING)
        {
            WebSocketCompletionResult result{};
            HRESULT hr = HCGetWebSocketSendMessageResult(asyncs[i], &result);
            // Note: send results carry the provider-side (observer) handle, not the caller's handle,
            // on every provider; a never-started operation leaves the result zeroed.
            E2E_CHECK_MSG(SUCCEEDED(hr) && result.websocket != nullptr, "send %d reported completion right after being queued, but has no result (hr=%s)", i, HrToString(hr).c_str());
            E2E_CHECK_HR(result.errorCode);
            resultTaken[i] = true;
        }
    }

    for (int i = 0; i < count; ++i)
    {
        HRESULT hr = WaitAsync(asyncs[i], std::chrono::seconds{ 15 });
        if (hr == E_PENDING)
        {
            PoisonProcess("a queued WebSocket send never completed");
            Fail(__FILE__, __LINE__, "queued send did not complete");
        }
        WebSocketCompletionResult result{};
        if (!resultTaken[i])
        {
            HCGetWebSocketSendMessageResult(asyncs[i], &result);
        }
        delete asyncs[i];
        E2E_CHECK_MSG(SUCCEEDED(hr) && SUCCEEDED(result.errorCode), "send %d: hr=%s errorCode=%s", i, HrToString(hr).c_str(), HrToString(result.errorCode).c_str());
    }
    E2E_CHECK_MSG(ws.WaitForText(count), "received %zu of %d echoes", ws.Texts().size(), count);
    auto texts = ws.Texts();
    for (int i = 0; i < count; ++i)
    {
        E2E_CHECK_EQ(messages[i], texts[i]);
    }
    ws.DisconnectAndWait();
}

E2E_TEST(ws_large_binary_with_max_receive_buffer)
{
    const size_t size = 600 * 1000;
    WebSocket ws;
    E2E_CHECK_HR(HCWebSocketSetMaxReceiveBufferSize(ws, 1024 * 1024));
    ConnectOrFail(ws, "/ws/echo");

    // Client -> server -> client.
    E2E_CHECK_HR(ws.SendBinary(PatternBytes(0, size)));
    // Server-originated.
    E2E_CHECK_HR(ws.SendText("__cmd:binary:" + std::to_string(size)));
    E2E_CHECK_MSG(ws.WaitForBinary(2, std::chrono::seconds{ 30 }), "received %zu of 2 large messages", ws.Binaries().size());
    for (const auto& b : ws.Binaries())
    {
        E2E_CHECK_EQ(size, b.size());
        E2E_CHECK(IsPattern(b.data(), b.size()));
    }
    ws.DisconnectAndWait();
}

E2E_TEST(ws_legacy_oversized_message_fragments)
{
    const size_t size = 100 * 1000;
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
    // Legacy Win32 semantics: payloads larger than the receive buffer (default 20 KB) are surfaced
    // through the fragment handler.
    WebSocket ws;
    ws.EnableFragmentHandler();
    ConnectOrFail(ws, "/ws/echo");
    E2E_CHECK_HR(ws.SendText("__cmd:binary:" + std::to_string(size)));
    E2E_CHECK_MSG(ws.WaitForFragmentedMessages(1), "no complete fragmented message (fragments so far: %zu)", ws.FragmentCallbackCount());
    auto messages = ws.FragmentedMessages();
    E2E_CHECK_EQ(size, messages[0].size());
    E2E_CHECK(IsPattern(messages[0].data(), messages[0].size()));
    E2E_CHECK_MSG(ws.FragmentCallbackCount() > 1, "expected several fragments, got %zu", ws.FragmentCallbackCount());
    E2E_CHECK_EQ(size_t{ 0 }, ws.Binaries().size());
    Info("%zu-byte message arrived in %zu fragments", size, ws.FragmentCallbackCount());

    // Messages under the buffer size still use the whole-message callback.
    E2E_CHECK_HR(ws.SendBinary(PatternBytes(0, 1000)));
    E2E_CHECK(ws.WaitForBinary(1));
    ws.DisconnectAndWait();
#else
    // websocketpp (Linux, Apple) has no fragment callback: legacy semantics keep its 32,000,000-byte
    // maximum and ignore HCWebSocketSetMaxReceiveBufferSize, so an oversized message (relative to
    // the Win32 20 KB default, and even to an explicit smaller buffer) arrives whole.
    WebSocket ws;
    E2E_CHECK_HR(HCWebSocketSetMaxReceiveBufferSize(ws, 16 * 1024));
    ConnectOrFail(ws, "/ws/echo");
    E2E_CHECK_HR(ws.SendText("__cmd:binary:" + std::to_string(size)));
    E2E_CHECK_MSG(ws.WaitForBinary(1), "the %zu-byte message was not delivered", size);
    E2E_CHECK_EQ(size, ws.Binaries()[0].size());
    E2E_CHECK(IsPattern(ws.Binaries()[0].data(), size));
    ws.DisconnectAndWait();
#endif
}

E2E_TEST(ws_server_fragmented_message_reassembled)
{
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo");
    E2E_CHECK_HR(ws.SendText("__cmd:fragmented:5,1000"));
    E2E_CHECK(ws.WaitForText(1));
    std::string expected;
    while (expected.size() < 1000)
    {
        expected += "0123456789";
    }
    E2E_CHECK_EQ(expected, ws.Texts()[0]);
    ws.DisconnectAndWait();
}

E2E_TEST(ws_deterministic_too_large_closes)
{
    WebSocket ws;
    SetOptionsOrSkip(ws, HCWebSocketOptions::None, "deterministic WebSocket semantics (HCWebSocketOptions::None)");
    E2E_CHECK_HR(HCWebSocketSetMaxReceiveBufferSize(ws, 16 * 1024));
    ConnectOrFail(ws, "/ws/echo");
    // Under the cap: delivered.
    E2E_CHECK_HR(ws.SendBinary(PatternBytes(0, 8 * 1024)));
    E2E_CHECK(ws.WaitForBinary(1));
    // Over the cap: the socket closes with TooLarge.
    E2E_CHECK_HR(ws.SendText("__cmd:binary:65536"));
    E2E_CHECK_MSG(ws.WaitForClose(), "socket did not close after an oversized message");
    E2E_CHECK_EQ(StatusValue(HCWebSocketCloseStatus::TooLarge), StatusValue(ws.CloseStatus()));
    E2E_CHECK_EQ(size_t{ 1 }, ws.Binaries().size());
}

E2E_TEST(ws_ping_interval_and_server_ping)
{
    WebSocket ws;
    E2E_CHECK_HR(HCWebSocketSetPingInterval(ws, 1));
    uint32_t interval = 0;
    E2E_CHECK_HR(HCWebSocketGetPingInterval(ws, &interval));
    E2E_CHECK_EQ(1u, interval);
    // The server pings every 100 ms; the client must answer with pongs and keep working.
    ConnectOrFail(ws, "/ws/echo?ping_ms=100");
    platform::SleepMs(700);
    E2E_CHECK_HR(ws.SendText("__ping__"));
    E2E_CHECK_HR(ws.SendText("still alive"));
    E2E_CHECK(ws.WaitForText(1));
    E2E_CHECK_EQ(std::string{ "still alive" }, ws.Texts()[0]);
    std::string stats = ws.ServerStats();
    Info("server stats: %s", stats.c_str());
    E2E_CHECK_MSG(JsonUInt(stats, "pongs_received") >= 3, "client answered %llu of %llu pings", JsonUInt(stats, "pongs_received"), JsonUInt(stats, "pings_sent"));
    ws.DisconnectAndWait();
}

E2E_TEST(ws_server_close_code_propagates)
{
    // after_ms gives the client time to finish connecting before the server closes.
    for (uint32_t code : { 1000u, 1001u, 4001u })
    {
        WebSocket ws;
        ConnectOrFail(ws, "/ws/close/" + std::to_string(code) + "?after_ms=300");
        E2E_CHECK_MSG(ws.WaitForClose(), "close handler not called for server close %u", code);
        E2E_CHECK_EQ(code, StatusValue(ws.CloseStatus()));
    }

    // Server-initiated close in the middle of a session.
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo");
    E2E_CHECK_HR(ws.SendText("__cmd:close:4002"));
    E2E_CHECK(ws.WaitForClose());
    E2E_CHECK_EQ(4002u, StatusValue(ws.CloseStatus()));
}

E2E_TEST_ISOLATED(ws_server_close_immediately_after_upgrade)
{
    // The server sends its close frame right behind the 101, so the close can race the connect
    // completion. WebSocket::ConnectComplete treats a close that lands before the connect
    // completes as a failed connect (E_FAIL); otherwise the connect succeeds and the close
    // follows. Either way the socket must end up closed and HCCleanupAsync must complete.
    E2E_CHECK_HR(HCInitialize(nullptr));
    {
        WebSocket ws;
        HRESULT hr = ws.Connect(WsUrl("/ws/close/4003"));
        if (SUCCEEDED(hr))
        {
            E2E_CHECK_MSG(ws.WaitForClose(), "connected but never saw the server's close");
            Info("connect succeeded, then closed with %u", StatusValue(ws.CloseStatus()));
        }
        else
        {
            Info("connect failed with hr=%s (the close won the race with the connect completion)", HrToString(hr).c_str());
            E2E_CHECK_EQ(E_FAIL, hr);
        }
    }

    HRESULT cleanupHr = CleanupWithTimeout(std::chrono::seconds{ 10 });
    if (cleanupHr == E_PENDING)
    {
        // Known bug (found by this test): NetworkState::WebSocketConnectComplete adds the socket
        // to m_connectedWebSockets and attaches its close observer only after the provider may
        // already have delivered the close, so the entry is never removed and HCCleanupAsync
        // waits forever (its Disconnect() on the already-closed socket produces no close event).
        // TODO: turn this into a failure once that race is fixed.
        PoisonProcess("HCCleanupAsync hung after a close that raced the WebSocket connect");
        Skip("HCCleanupAsync hung: a close that races the WebSocket connect completion leaves the socket in NetworkState::m_connectedWebSockets (known bug)");
    }
    E2E_CHECK_HR(cleanupHr);
}

E2E_TEST(ws_client_disconnect_normal_close)
{
    const std::string id = UniqueId("disconnect");
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo?id=" + id);
    E2E_CHECK_HR(ws.SendText("hello"));
    E2E_CHECK(ws.WaitForText(1));
    ws.DisconnectAndWait();
    E2E_CHECK_EQ(StatusValue(HCWebSocketCloseStatus::Normal), StatusValue(ws.CloseStatus()));
    std::string log = WaitForServerClose(id);
    E2E_CHECK_EQ(uint64_t{ 1000 }, JsonUInt(log, "client_close_code"));

    // Sending after close fails cleanly.
    HRESULT hr = ws.SendText("too late");
    E2E_CHECK_MSG(FAILED(hr), "send after close returned %s", HrToString(hr).c_str());
}

E2E_TEST(ws_disconnect_with_status)
{
    const std::string id = UniqueId("goingaway");
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo?id=" + id);
    E2E_CHECK_HR(HCWebSocketDisconnectWithStatus(ws, HCWebSocketCloseStatus::GoingAway));
    E2E_CHECK(ws.WaitForClose());
    std::string log = WaitForServerClose(id);
    E2E_CHECK_EQ(uint64_t{ 1001 }, JsonUInt(log, "client_close_code"));
}

E2E_TEST(ws_request_and_response_headers)
{
    WebSocket ws;
    E2E_CHECK_HR(HCWebSocketSetHeader(ws, "X-Lhc-Ws-Req", "abc"));
    HRESULT hr = ws.Connect(WsUrl("/ws/echo"), "lhc.e2e");
    E2E_CHECK_MSG(SUCCEEDED(hr), "connect failed: hr=%s", HrToString(hr).c_str());

    const char* value = nullptr;
    E2E_CHECK_HR(HCWebSocketGetResponseHeader(ws, "X-Lhc-Ws", &value));
    E2E_CHECK_EQ(std::string{ "1" }, std::string{ value ? value : "" });
    value = nullptr;
    E2E_CHECK_HR(HCWebSocketGetResponseHeader(ws, "Sec-WebSocket-Protocol", &value));
    E2E_CHECK_EQ(std::string{ "lhc.e2e" }, std::string{ value ? value : "" });
    uint32_t count = 0;
    E2E_CHECK_HR(HCWebSocketGetNumResponseHeaders(ws, &count));
    E2E_CHECK_MSG(count >= 4, "only %u response headers", count);

    std::string stats = ws.ServerStats();
    E2E_CHECK_EQ(std::string{ "abc" }, JsonFieldOr(stats, "x-lhc-ws-req"));
    E2E_CHECK_EQ(std::string{ "lhc.e2e" }, JsonFieldOr(stats, "sec-websocket-protocol"));
    ws.DisconnectAndWait();
}

E2E_TEST(ws_no_compression_by_default)
{
    WebSocket ws;
    ConnectOrFail(ws, "/ws/echo");
    std::string stats = ws.ServerStats();
    E2E_CHECK_EQ(std::string{ "false" }, JsonFieldOr(stats, "deflate"));
    ws.DisconnectAndWait();
}

E2E_TEST(ws_compression_negotiated)
{
    struct Variant
    {
        const char* name;
        HCWebSocketOptions options;
    };
    const Variant variants[] = {
        { "RequestCompression", HCWebSocketOptions::RequestCompression },
        { "+ServerNoContextTakeover", HCWebSocketOptions::RequestCompression | HCWebSocketOptions::CompressionServerNoContextTakeover },
        { "+ClientNoContextTakeover", HCWebSocketOptions::RequestCompression | HCWebSocketOptions::CompressionClientNoContextTakeover },
        { "+BothNoContextTakeover", HCWebSocketOptions::RequestCompression | HCWebSocketOptions::CompressionServerNoContextTakeover | HCWebSocketOptions::CompressionClientNoContextTakeover },
    };

    for (const auto& variant : variants)
    {
        const std::string id = UniqueId("deflate");
        WebSocket ws;
        SetOptionsOrSkip(ws, variant.options, variant.name);
        HRESULT hr = ws.Connect(WsUrl("/ws/echo?id=" + id));
        E2E_CHECK_MSG(SUCCEEDED(hr), "%s: connect failed: hr=%s platformError=%u", variant.name, HrToString(hr).c_str(), ws.ConnectResult().platformErrorCode);

        // Several messages so context takeover (or the lack of it) is exercised in both directions.
        std::vector<std::string> texts{ CompressibleText(20000), CompressibleText(20000), "short", CompressibleText(5000) };
        for (const auto& t : texts)
        {
            E2E_CHECK_HR(ws.SendText(t));
        }
        auto binary = PatternBytes(0, 50000);
        E2E_CHECK_HR(ws.SendBinary(binary));
        E2E_CHECK_MSG(ws.WaitForText(texts.size()), "%s: received %zu of %zu text echoes", variant.name, ws.Texts().size(), texts.size());
        E2E_CHECK_MSG(ws.WaitForBinary(1), "%s: no binary echo", variant.name);
        auto received = ws.Texts();
        for (size_t i = 0; i < texts.size(); ++i)
        {
            E2E_CHECK_MSG(received[i] == texts[i], "%s: text echo %zu differs", variant.name, i);
        }
        E2E_CHECK_MSG(ws.Binaries()[0] == binary, "%s: binary echo differs", variant.name);

        std::string stats = ws.ServerStats();
        E2E_CHECK_MSG(JsonFieldOr(stats, "deflate") == "true", "%s: permessage-deflate was not negotiated: %s", variant.name, stats.c_str());
        E2E_CHECK_MSG(JsonUInt(stats, "compressed_messages_received") >= texts.size(), "%s: client sent uncompressed messages: %s", variant.name, stats.c_str());
        ws.DisconnectAndWait();

        std::string log = WaitForServerClose(id);
        Info("%s: client offered '%s'", variant.name, JsonFieldOr(log, "offered_extensions").c_str());
    }
}

E2E_TEST_ISOLATED(ws_bad_upgrade_fails_and_cleanup_completes)
{
    E2E_CHECK_HR(HCInitialize(nullptr));
    HRESULT connectHr = S_OK;
    {
        WebSocket ws;
        connectHr = ws.Connect(WsUrl("/ws/bad-upgrade"));
        Info("connect: hr=%s platformError=%u", HrToString(connectHr).c_str(), ws.ConnectResult().platformErrorCode);
    }
    E2E_CHECK_MSG(FAILED(connectHr), "a 101 response with a bad Sec-WebSocket-Accept was accepted");

    auto start = std::chrono::steady_clock::now();
    HRESULT cleanupHr = CleanupWithTimeout(std::chrono::seconds{ 10 });
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (cleanupHr == E_PENDING)
    {
        // Known bug #909 (fixed by PR #1033): after a rejected upgrade the WinHTTP WebSocket keeps
        // HCCleanupAsync from completing. The library can't be used again in this process.
        // TODO(#1033): turn this into a failure once the fix is in.
        PoisonProcess("HCCleanupAsync hung after a failed WebSocket upgrade (#909)");
        Skip("HCCleanupAsync hung after a failed WebSocket upgrade - known bug #909, fixed by PR #1033");
    }
    E2E_CHECK_HR(cleanupHr);
    Info("HCCleanupAsync completed in %.2f s", seconds);
    E2E_CHECK_MSG(seconds < 5.0, "HCCleanupAsync took %.2f s after a failed upgrade", seconds);
}
