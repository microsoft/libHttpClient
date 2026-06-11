// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_

#pragma warning( push )
#pragma warning( disable : 4100 4127 4512 4996 4701 4267 )
#define _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CONSTEXPR_TOKEN_
#define _SCL_SECURE_NO_WARNINGS

#if defined(_MSC_VER) && (_MSC_VER >= 1900)
#define ASIO_ERROR_CATEGORY_NOEXCEPT noexcept(true)
#endif

#include <httpClient/async.h>
#include <httpClient/httpClient.h>
#include <httpClient/trace.h>

#include <cerrno>

// Trust-store manipulation can trigger Windows confirmation UI, so those cases stay in the
// dedicated Win32 integration-test binary rather than the default CI-facing test binary.
#if defined(HC_ENABLE_WSS_CERT_STORE_TESTS) && HC_PLATFORM == HC_PLATFORM_WIN32
#define HC_RUN_WSS_CERT_STORE_TESTS 1
#else
#define HC_RUN_WSS_CERT_STORE_TESTS 0
#endif

#if HC_RUN_WSS_CERT_STORE_TESTS
#include "../../External/boost-wintls/test/certificate.hpp"
#include "../../Source/WebSocket/Websocketpp/wintls_socket.hpp"
#endif

#include "../../Source/WebSocket/Websocketpp/websocketpp_disabled_permessage_deflate.hpp"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/extensions/permessage_deflate/enabled.hpp>
#include <websocketpp/server.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

constexpr int SkipReturnCode = 125;
constexpr uint16_t TestPort = 39002;
#if HC_RUN_WSS_CERT_STORE_TESTS
constexpr uint16_t WssTestPort = 39004;
#endif
constexpr std::chrono::seconds Timeout{ 10 };

enum class CompressionExpectation
{
    Negotiated,
    Unsupported
};

constexpr size_t ReceiveBufferSize = 4096;

void PrintHr(char const* operation, HRESULT hr)
{
    std::printf("%s: 0x%08x\n", operation, static_cast<unsigned int>(hr));
}

bool HeaderContainsToken(std::string const& headerValue, char const* token)
{
    return headerValue.find(token) != std::string::npos;
}

bool ValidateRequestedCompressionExtensionHeader(
    std::string const& headerValue,
    char const* headerLabel,
    char const* scenarioLabel,
    bool expectServerNoContextTakeover,
    bool expectClientNoContextTakeover)
{
    if (!HeaderContainsToken(headerValue, "permessage-deflate"))
    {
        std::printf("[FAIL] %s did not advertise permessage-deflate for %s.\n", headerLabel, scenarioLabel);
        return false;
    }

    bool const hasServerNoContextTakeover = HeaderContainsToken(headerValue, "server_no_context_takeover");
    if (hasServerNoContextTakeover != expectServerNoContextTakeover)
    {
        std::printf(
            "[FAIL] %s %s server_no_context_takeover for %s.\n",
            headerLabel,
            expectServerNoContextTakeover ? "did not advertise" : "unexpectedly advertised",
            scenarioLabel);
        return false;
    }

    bool const hasClientNoContextTakeover = HeaderContainsToken(headerValue, "client_no_context_takeover");
    if (hasClientNoContextTakeover != expectClientNoContextTakeover)
    {
        std::printf(
            "[FAIL] %s %s client_no_context_takeover for %s.\n",
            headerLabel,
            expectClientNoContextTakeover ? "did not advertise" : "unexpectedly advertised",
            scenarioLabel);
        return false;
    }

    return true;
}

bool ValidateNegotiatedCompressionExtensionHeader(
    std::string const& headerValue,
    char const* headerLabel,
    char const* scenarioLabel)
{
    if (!HeaderContainsToken(headerValue, "permessage-deflate"))
    {
        std::printf("[FAIL] %s did not advertise permessage-deflate for %s.\n", headerLabel, scenarioLabel);
        return false;
    }

    return true;
}

struct ScopeGuard
{
    explicit ScopeGuard(std::function<void()> cleanup) : m_cleanup(std::move(cleanup)) {}

    ~ScopeGuard()
    {
        if (m_cleanup)
        {
            m_cleanup();
        }
    }

    ScopeGuard(ScopeGuard const&) = delete;
    ScopeGuard& operator=(ScopeGuard const&) = delete;

private:
    std::function<void()> m_cleanup;
};

struct CapturedTraceMessage
{
    std::string area;
    HCTraceLevel level;
    std::string message;
};

class ScopedTraceCapture
{
public:
    ScopedTraceCapture()
    {
        m_restoreTraceLevel = SUCCEEDED(HCSettingsGetTraceLevel(&m_previousTraceLevel));

        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            s_activeCapture = this;
            HCTraceSetClientCallback(&ScopedTraceCapture::TraceCallback);
        }

        HCSettingsSetTraceLevel(HCTraceLevel::Verbose);
    }

    ~ScopedTraceCapture()
    {
        {
            std::lock_guard<std::mutex> lock(s_callbackMutex);
            if (s_activeCapture == this)
            {
                s_activeCapture = nullptr;
                HCTraceSetClientCallback(nullptr);
            }
        }

        if (m_restoreTraceLevel)
        {
            HCSettingsSetTraceLevel(m_previousTraceLevel);
        }
    }

    ScopedTraceCapture(ScopedTraceCapture const&) = delete;
    ScopedTraceCapture& operator=(ScopedTraceCapture const&) = delete;

    bool ContainsWebsocketConnectError(uint32_t expectedPlatformErrorCode) const
    {
        auto const expectedCode = std::to_string(expectedPlatformErrorCode);
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::any_of(m_messages.begin(), m_messages.end(), [&expectedCode](CapturedTraceMessage const& message)
        {
            return message.area == "WEBSOCKET" &&
                message.message.find("asio async_connect error:") != std::string::npos &&
                message.message.find(expectedCode) != std::string::npos;
        });
    }

    void DumpWebsocketMessages() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto const& message : m_messages)
        {
            if (message.area == "WEBSOCKET")
            {
                std::printf("[INFO] captured trace (%d): %s\n", static_cast<int>(message.level), message.message.c_str());
            }
        }
    }

private:
    static void CALLBACK TraceCallback(
        _In_z_ char const* areaName,
        _In_ HCTraceLevel level,
        _In_ uint64_t,
        _In_ uint64_t,
        _In_z_ char const* message
    ) noexcept
    {
        std::lock_guard<std::mutex> lock(s_callbackMutex);
        if (s_activeCapture == nullptr)
        {
            return;
        }

        s_activeCapture->Append(areaName, level, message);
    }

    void Append(char const* areaName, HCTraceLevel level, char const* message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_messages.push_back(CapturedTraceMessage
        {
            areaName != nullptr ? areaName : "",
            level,
            message != nullptr ? message : ""
        });
    }

    inline static std::mutex s_callbackMutex{};
    inline static ScopedTraceCapture* s_activeCapture{ nullptr };

    mutable std::mutex m_mutex;
    std::vector<CapturedTraceMessage> m_messages;
    HCTraceLevel m_previousTraceLevel{};
    bool m_restoreTraceLevel{ false };
};

struct CompressionServerConfig : public websocketpp::config::asio
{
    typedef CompressionServerConfig type;
    typedef websocketpp::config::asio base;

    typedef base::concurrency_type concurrency_type;
    typedef base::request_type request_type;
    typedef base::response_type response_type;
    typedef base::message_type message_type;
    typedef base::con_msg_manager_type con_msg_manager_type;
    typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;
    typedef base::alog_type alog_type;
    typedef base::elog_type elog_type;
    typedef base::rng_type rng_type;

    struct transport_config : public base::transport_config
    {
        typedef type::concurrency_type concurrency_type;
        typedef type::alog_type alog_type;
        typedef type::elog_type elog_type;
        typedef type::request_type request_type;
        typedef type::response_type response_type;
        typedef websocketpp::transport::asio::basic_socket::endpoint socket_type;
    };

    typedef websocketpp::transport::asio::endpoint<transport_config> transport_type;

    struct permessage_deflate_config
    {
        static const bool allow_disabling_context_takeover = true;
        static const uint8_t minimum_outgoing_window_bits = 8;
    };

    typedef websocketpp::extensions::permessage_deflate::enabled<permessage_deflate_config> permessage_deflate_type;
};

#if HC_RUN_WSS_CERT_STORE_TESTS
struct WinTlsServerConfig : public websocketpp::config::core
{
    typedef WinTlsServerConfig type;
    typedef websocketpp::config::core base;

    typedef base::concurrency_type concurrency_type;
    typedef base::request_type request_type;
    typedef base::response_type response_type;
    typedef base::message_type message_type;
    typedef base::con_msg_manager_type con_msg_manager_type;
    typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;
    typedef base::alog_type alog_type;
    typedef base::elog_type elog_type;
    typedef base::rng_type rng_type;

    struct transport_config : public base::transport_config
    {
        typedef type::concurrency_type concurrency_type;
        typedef type::alog_type alog_type;
        typedef type::elog_type elog_type;
        typedef type::request_type request_type;
        typedef type::response_type response_type;
        typedef websocketpp::transport::asio::wintls_socket::endpoint socket_type;
    };

    typedef websocketpp::transport::asio::endpoint<transport_config> transport_type;
};
#endif

class CompressionEchoServer
{
public:
    using server = websocketpp::server<CompressionServerConfig>;
    using message_ptr = server::message_ptr;

    bool Start(uint16_t port)
    {
        m_server.clear_access_channels(websocketpp::log::alevel::all);
        m_server.clear_error_channels(websocketpp::log::elevel::all);
        m_server.init_asio();
        m_server.set_reuse_addr(true);
        m_server.set_open_handler([this](websocketpp::connection_hdl hdl)
        {
            auto connection = m_server.get_con_from_hdl(hdl);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_connection = hdl;
                m_connected = true;
                m_requestedExtensions = connection->get_request_header("Sec-WebSocket-Extensions");
                m_negotiatedExtensions = connection->get_response_header("Sec-WebSocket-Extensions");
            }
            m_cv.notify_all();
        });
        m_server.set_fail_handler([this](websocketpp::connection_hdl hdl)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            try
            {
                m_error = m_server.get_con_from_hdl(hdl)->get_ec().message();
            }
            catch (...)
            {
                m_error = "connection failed";
            }
            m_cv.notify_all();
        });
        m_server.set_message_handler([this](websocketpp::connection_hdl hdl, message_ptr const& msg)
        {
            websocketpp::lib::error_code ec;
            m_server.send(hdl, msg->get_payload(), msg->get_opcode(), ec);
            if (ec)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_error = ec.message();
                m_cv.notify_all();
            }
        });

        websocketpp::lib::error_code ec;
        m_server.listen(port, ec);
        if (ec)
        {
            std::printf("[FAIL] listen failed: %s\n", ec.message().c_str());
            return false;
        }

        m_server.start_accept(ec);
        if (ec)
        {
            std::printf("[FAIL] start_accept failed: %s\n", ec.message().c_str());
            return false;
        }

        m_thread = std::thread([this]()
        {
            try
            {
                m_server.run();
            }
            catch (std::exception const& e)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_error = e.what();
                m_cv.notify_all();
            }
        });

        return true;
    }

    void Stop()
    {
        websocketpp::lib::error_code ec;
        m_server.stop_listening(ec);
        if (m_connected)
        {
            m_server.close(m_connection, websocketpp::close::status::normal, "test complete", ec);
        }
        m_server.stop();

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    bool WaitForConnection() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, Timeout, [this]()
        {
            return m_connected || !m_error.empty();
        });
    }

    std::string RequestedExtensions() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_requestedExtensions;
    }

    std::string NegotiatedExtensions() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_negotiatedExtensions;
    }

    std::string Error() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_error;
    }

    void ResetObservedConnection()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connected = false;
        m_requestedExtensions.clear();
        m_negotiatedExtensions.clear();
        m_error.clear();
    }

private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    server m_server;
    std::thread m_thread;
    websocketpp::connection_hdl m_connection;
    bool m_connected{ false };
    std::string m_requestedExtensions;
    std::string m_negotiatedExtensions;
    std::string m_error;
};

#if HC_RUN_WSS_CERT_STORE_TESTS
struct StoreCloser
{
    void operator()(void* store) const noexcept
    {
        if (store != nullptr)
        {
            CertCloseStore(reinterpret_cast<HCERTSTORE>(store), 0);
        }
    }
};

using store_ptr = std::unique_ptr<void, StoreCloser>;

store_ptr OpenCurrentUserRootStore()
{
    auto store = store_ptr(
        CertOpenStore(
            CERT_STORE_PROV_SYSTEM_A,
            0,
            0,
            CERT_SYSTEM_STORE_CURRENT_USER,
            "ROOT"),
        StoreCloser{});

    if (!store)
    {
        throw std::runtime_error("CertOpenStore(ROOT) failed");
    }

    return store;
}

bool CertificatesMatch(CERT_CONTEXT const* lhs, CERT_CONTEXT const* rhs)
{
    return lhs != nullptr &&
        rhs != nullptr &&
        lhs->cbCertEncoded == rhs->cbCertEncoded &&
        std::memcmp(lhs->pbCertEncoded, rhs->pbCertEncoded, lhs->cbCertEncoded) == 0;
}

void RemoveMatchingCertificatesFromStore(HCERTSTORE store, CERT_CONTEXT const* cert)
{
    PCCERT_CONTEXT current = nullptr;
    while ((current = CertEnumCertificatesInStore(store, current)) != nullptr)
    {
        if (!CertificatesMatch(current, cert))
        {
            continue;
        }

        PCCERT_CONTEXT duplicate = CertDuplicateCertificateContext(current);
        if (duplicate == nullptr || !CertDeleteCertificateFromStore(duplicate))
        {
            throw std::runtime_error("CertDeleteCertificateFromStore failed");
        }
    }
}

class CurrentUserRootCertificateScope
{
public:
    CurrentUserRootCertificateScope() = default;

    void Install(CERT_CONTEXT const* cert)
    {
        if (cert == nullptr)
        {
            throw std::runtime_error("Attempted to trust a null certificate");
        }

        auto store = OpenCurrentUserRootStore();
        RemoveMatchingCertificatesFromStore(reinterpret_cast<HCERTSTORE>(store.get()), cert);
        if (!CertAddCertificateContextToStore(reinterpret_cast<HCERTSTORE>(store.get()), cert, CERT_STORE_ADD_REPLACE_EXISTING, nullptr))
        {
            throw std::runtime_error("CertAddCertificateContextToStore failed");
        }

        m_installed = true;
        m_cert.reset(CertDuplicateCertificateContext(cert));
        if (!m_cert)
        {
            throw std::runtime_error("CertDuplicateCertificateContext failed");
        }
    }

    void Uninstall()
    {
        if (!m_installed || !m_cert)
        {
            return;
        }

        auto store = OpenCurrentUserRootStore();
        RemoveMatchingCertificatesFromStore(reinterpret_cast<HCERTSTORE>(store.get()), m_cert.get());
        m_installed = false;
        m_cert.reset();
    }

    ~CurrentUserRootCertificateScope()
    {
        try
        {
            Uninstall();
        }
        catch (...)
        {
        }
    }

    CurrentUserRootCertificateScope(CurrentUserRootCertificateScope const&) = delete;
    CurrentUserRootCertificateScope& operator=(CurrentUserRootCertificateScope const&) = delete;

private:
    struct CertDeleter
    {
        void operator()(CERT_CONTEXT const* cert) const noexcept
        {
            if (cert != nullptr)
            {
                CertFreeCertificateContext(cert);
            }
        }
    };

    bool m_installed{ false };
    std::unique_ptr<CERT_CONTEXT const, CertDeleter> m_cert;
};

class ImportedServerCertificate
{
public:
    ImportedServerCertificate()
        : m_keyName("libHttpClient-wss-test-key-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()))
    {
        wintls::error_code ignored{};
        wintls::delete_private_key(m_keyName, ignored);

        m_cert = wintls::x509_to_cert_context(wintls::net::buffer(test_certificate), wintls::file_format::pem);
        wintls::import_private_key(wintls::net::buffer(test_key), wintls::file_format::pem, m_keyName);
        m_privateKeyImported = true;
        wintls::assign_private_key(m_cert.get(), m_keyName);
    }

    ~ImportedServerCertificate()
    {
        if (m_privateKeyImported)
        {
            wintls::error_code ignored{};
            wintls::delete_private_key(m_keyName, ignored);
        }
    }

    ImportedServerCertificate(ImportedServerCertificate const&) = delete;
    ImportedServerCertificate& operator=(ImportedServerCertificate const&) = delete;

    CERT_CONTEXT const* Context() const noexcept
    {
        return m_cert.get();
    }

private:
    std::string m_keyName;
    bool m_privateKeyImported{ false };
    wintls::cert_context_ptr m_cert;
};

class WssEchoServer
{
public:
    using server = websocketpp::server<WinTlsServerConfig>;
    using message_ptr = server::message_ptr;

    explicit WssEchoServer(CERT_CONTEXT const* certificate)
        : m_certificate(certificate)
    {
    }

    bool Start(uint16_t port)
    {
        m_server.clear_access_channels(websocketpp::log::alevel::all);
        m_server.clear_error_channels(websocketpp::log::elevel::all);
        m_server.init_asio();
        m_server.set_reuse_addr(true);
        m_server.set_tls_init_handler([this](websocketpp::connection_hdl)
        {
            auto context = websocketpp::lib::shared_ptr<wintls::context>(new wintls::context(wintls::method::system_default));
            context->use_certificate(m_certificate);
            return context;
        });
        m_server.set_open_handler([this](websocketpp::connection_hdl hdl)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_connection = hdl;
                m_connected = true;
                ++m_openCount;
            }
            m_cv.notify_all();
        });
        m_server.set_fail_handler([this](websocketpp::connection_hdl hdl)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            try
            {
                m_error = m_server.get_con_from_hdl(hdl)->get_ec().message();
            }
            catch (...)
            {
                m_error = "connection failed";
            }
            m_cv.notify_all();
        });
        m_server.set_message_handler([this](websocketpp::connection_hdl hdl, message_ptr const& msg)
        {
            websocketpp::lib::error_code ec;
            m_server.send(hdl, msg->get_payload(), msg->get_opcode(), ec);
            if (ec)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_error = ec.message();
                m_cv.notify_all();
            }
        });

        websocketpp::lib::error_code ec;
        m_server.listen(port, ec);
        if (ec)
        {
            std::printf("[FAIL] WSS listen failed: %s\n", ec.message().c_str());
            return false;
        }

        m_server.start_accept(ec);
        if (ec)
        {
            std::printf("[FAIL] WSS start_accept failed: %s\n", ec.message().c_str());
            return false;
        }

        m_thread = std::thread([this]()
        {
            try
            {
                m_server.run();
            }
            catch (std::exception const& e)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_error = e.what();
                m_cv.notify_all();
            }
        });

        return true;
    }

    void Stop()
    {
        websocketpp::lib::error_code ec;
        m_server.stop_listening(ec);
        if (m_connected)
        {
            m_server.close(m_connection, websocketpp::close::status::normal, "test complete", ec);
        }
        m_server.stop();

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    bool WaitForOpenCount(size_t expectedOpenCount) const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, Timeout, [this, expectedOpenCount]()
        {
            return m_openCount >= expectedOpenCount || !m_error.empty();
        });
    }

    std::string Error() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_error;
    }

private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_cv;
    server m_server;
    std::thread m_thread;
    websocketpp::connection_hdl m_connection;
    CERT_CONTEXT const* m_certificate{ nullptr };
    bool m_connected{ false };
    size_t m_openCount{ 0 };
    std::string m_error;
};
#endif

struct ClientState
{
    std::mutex mutex;
    std::condition_variable cv;
    size_t textMessagesReceived{ 0 };
    std::string lastMessage;
    size_t closeEventsReceived{ 0 };
    HCWebSocketCloseStatus lastCloseStatus{};
};

void CALLBACK OnTextMessage(
    _In_ HCWebsocketHandle,
    _In_z_ char const* incomingBodyString,
    _In_ void* context
) noexcept
{
    auto* state = static_cast<ClientState*>(context);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->textMessagesReceived;
        state->lastMessage = incomingBodyString != nullptr ? incomingBodyString : "";
    }
    state->cv.notify_all();
}

std::string BuildLargePayload()
{
    std::string payload(ReceiveBufferSize - 1, 'A');
    payload += "\xF0\x9F\x98\x80";
    payload += std::string(128, 'B');
    return payload;
}

void CALLBACK OnClose(
    _In_ HCWebsocketHandle,
    _In_ HCWebSocketCloseStatus status,
    _In_ void* context
) noexcept
{
    auto* state = static_cast<ClientState*>(context);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->closeEventsReceived;
        state->lastCloseStatus = status;
    }
    state->cv.notify_all();
}

bool WaitForEcho(ClientState& state, size_t expectedTextMessagesReceived, std::string const& expected)
{
    std::unique_lock<std::mutex> lock(state.mutex);
    bool signaled = state.cv.wait_for(lock, Timeout, [&state, expectedTextMessagesReceived]()
    {
        return state.textMessagesReceived >= expectedTextMessagesReceived;
    });

    return signaled && state.lastMessage == expected;
}

bool WaitForClose(ClientState& state, size_t expectedCloseEventsReceived, HCWebSocketCloseStatus expectedStatus)
{
    std::unique_lock<std::mutex> lock(state.mutex);
    bool const signaled = state.cv.wait_for(lock, Timeout, [&state, expectedCloseEventsReceived]()
    {
        return state.closeEventsReceived >= expectedCloseEventsReceived;
    });

    if (!signaled)
    {
        return false;
    }

    if (state.lastCloseStatus != expectedStatus)
    {
        std::printf(
            "[INFO] Observed close status %u but expected %u.\n",
            static_cast<unsigned int>(state.lastCloseStatus),
            static_cast<unsigned int>(expectedStatus));
        return false;
    }

    return true;
}

HRESULT ConfigureTestWebSocket(HCWebsocketHandle websocket, ClientState& state)
{
    (void)websocket;
    (void)state;

    return S_OK;
}

#if HC_RUN_WSS_CERT_STORE_TESTS
HRESULT ConfigureCompressionWebSocket(HCWebsocketHandle websocket, ClientState& state)
{
    HRESULT hr = ConfigureTestWebSocket(websocket, state);
    if (FAILED(hr))
    {
        return hr;
    }

    return HCWebSocketSetOptions(websocket, HCWebSocketOptions::RequestCompression);
}

bool CreateCompressionRequestedWebSocket(HCWebsocketHandle& websocket, ClientState& state)
{
    HRESULT hr = HCWebSocketCreate(&websocket, OnTextMessage, nullptr, OnClose, &state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketCreate", hr);
        return false;
    }

    hr = ConfigureCompressionWebSocket(websocket, state);
    if (hr != S_OK)
    {
        PrintHr("[FAIL] Expected compression-capable websocket provider for WSS validation", hr);
        return false;
    }

    return true;
}

bool ConnectWebSocketAndGetResult(
    XTaskQueueHandle queue,
    char const* uri,
    HCWebsocketHandle websocket,
    WebSocketCompletionResult& connectResult)
{
    XAsyncBlock connectAsync{};
    connectAsync.queue = queue;

    HRESULT hr = HCWebSocketConnectAsync(uri, "", websocket, &connectAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketConnectAsync", hr);
        return false;
    }

    hr = XAsyncGetStatus(&connectAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(connect)", hr);
        return false;
    }

    hr = HCGetWebSocketConnectResult(&connectAsync, &connectResult);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCGetWebSocketConnectResult", hr);
        return false;
    }

    return true;
}

bool SendWebSocketMessageAndValidateEcho(HCWebsocketHandle websocket, XTaskQueueHandle queue, ClientState& state, char const* message)
{
    XAsyncBlock sendAsync{};
    sendAsync.queue = queue;

    HRESULT hr = HCWebSocketSendMessageAsync(websocket, message, &sendAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSendMessageAsync", hr);
        return false;
    }

    hr = XAsyncGetStatus(&sendAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(send)", hr);
        return false;
    }

    WebSocketCompletionResult sendResult{};
    hr = HCGetWebSocketSendMessageResult(&sendAsync, &sendResult);
    if (FAILED(hr) || FAILED(sendResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketSendMessageResult", hr);
        PrintHr("[FAIL] Send result", sendResult.errorCode);
        return false;
    }

    if (!WaitForEcho(state, 1, message))
    {
        std::printf("[FAIL] Timed out waiting for echoed WSS message.\n");
        return false;
    }

    return true;
}

bool DisconnectWebSocketAndValidateClose(HCWebsocketHandle websocket, ClientState& state)
{
    HRESULT hr = HCWebSocketDisconnect(websocket);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketDisconnect", hr);
        return false;
    }

    if (!WaitForClose(state, 1, HCWebSocketCloseStatus::Normal))
    {
        std::printf("[FAIL] Timed out waiting for WSS close callback after disconnect.\n");
        return false;
    }

    return true;
}

bool DisconnectWebSocket(HCWebsocketHandle websocket)
{
    HRESULT hr = HCWebSocketDisconnect(websocket);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketDisconnect", hr);
        return false;
    }

    return true;
}

bool ValidateWssCertificateValidation(XTaskQueueHandle queue)
{
    ImportedServerCertificate certificate;
    CurrentUserRootCertificateScope trustedRoot;
    WssEchoServer server{ certificate.Context() };
    if (!server.Start(WssTestPort))
    {
        return false;
    }

    ScopeGuard serverGuard([&server]()
    {
        server.Stop();
    });

    trustedRoot.Install(certificate.Context());

    {
        ClientState state;
        HCWebsocketHandle websocket{ nullptr };
        ScopeGuard cleanup([&]()
        {
            if (websocket != nullptr)
            {
                HCWebSocketCloseHandle(websocket);
            }
        });

        if (!CreateCompressionRequestedWebSocket(websocket, state))
        {
            return false;
        }

        WebSocketCompletionResult connectResult{};
        if (!ConnectWebSocketAndGetResult(queue, "wss://localhost:39004", websocket, connectResult))
        {
            return false;
        }

        if (FAILED(connectResult.errorCode))
        {
            PrintHr("[FAIL] Trusted localhost WSS connect result", connectResult.errorCode);
            return false;
        }

        if (!server.WaitForOpenCount(1))
        {
            std::printf("[FAIL] Timed out waiting for trusted WSS server connection.\n");
            if (!server.Error().empty())
            {
                std::printf("[FAIL] WSS server error: %s\n", server.Error().c_str());
            }
            return false;
        }

        if (!SendWebSocketMessageAndValidateEcho(websocket, queue, state, "secure-hello"))
        {
            return false;
        }

        if (!DisconnectWebSocket(websocket))
        {
            return false;
        }

        std::printf("[INFO] Trusted localhost WSS validation passed.\n");
    }

    {
        ClientState state;
        HCWebsocketHandle websocket{ nullptr };
        ScopeGuard cleanup([&]()
        {
            if (websocket != nullptr)
            {
                HCWebSocketCloseHandle(websocket);
            }
        });

        if (!CreateCompressionRequestedWebSocket(websocket, state))
        {
            return false;
        }

        WebSocketCompletionResult connectResult{};
        if (!ConnectWebSocketAndGetResult(queue, "wss://127.0.0.1:39004", websocket, connectResult))
        {
            return false;
        }

        if (connectResult.errorCode != CERT_E_CN_NO_MATCH)
        {
            std::printf("[FAIL] Expected wrong-host WSS failure CERT_E_CN_NO_MATCH.\n");
            PrintHr("[FAIL] Wrong-host WSS connect result", connectResult.errorCode);
            return false;
        }

        std::printf("[INFO] Wrong-host WSS validation failed as expected with CERT_E_CN_NO_MATCH.\n");
    }

    trustedRoot.Uninstall();

    {
        ClientState state;
        HCWebsocketHandle websocket{ nullptr };
        ScopeGuard cleanup([&]()
        {
            if (websocket != nullptr)
            {
                HCWebSocketCloseHandle(websocket);
            }
        });

        if (!CreateCompressionRequestedWebSocket(websocket, state))
        {
            return false;
        }

        WebSocketCompletionResult connectResult{};
        if (!ConnectWebSocketAndGetResult(queue, "wss://localhost:39004", websocket, connectResult))
        {
            return false;
        }

        if (connectResult.errorCode != CERT_E_UNTRUSTEDROOT)
        {
            std::printf("[FAIL] Expected untrusted-root WSS failure CERT_E_UNTRUSTEDROOT.\n");
            PrintHr("[FAIL] Untrusted-root WSS connect result", connectResult.errorCode);
            return false;
        }

        std::printf("[INFO] Untrusted-root WSS validation failed as expected with CERT_E_UNTRUSTEDROOT.\n");
    }

    return true;
}
#endif

bool ValidateWssFailureDiagnostics(XTaskQueueHandle queue)
{
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
    constexpr uint32_t ConnectionRefusedError = static_cast<uint32_t>(WSAECONNREFUSED);
#else
    constexpr uint32_t ConnectionRefusedError = static_cast<uint32_t>(ECONNREFUSED);
#endif

    ClientState state;
    HCWebsocketHandle websocket{ nullptr };
    ScopeGuard cleanup([&]()
    {
        if (websocket != nullptr)
        {
            HCWebSocketCloseHandle(websocket);
        }
    });

    HRESULT hr = HCWebSocketCreate(&websocket, OnTextMessage, nullptr, OnClose, &state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketCreate(wss failure diagnostics)", hr);
        return false;
    }

    hr = HCWebSocketSetOptions(websocket, HCWebSocketOptions::RequestCompression);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSetOptions(wss failure diagnostics)", hr);
        return false;
    }

    ScopedTraceCapture traceCapture;

    XAsyncBlock connectAsync{};
    connectAsync.queue = queue;

    hr = HCWebSocketConnectAsync("wss://127.0.0.1:39003", "", websocket, &connectAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketConnectAsync(wss failure diagnostics)", hr);
        return false;
    }

    hr = XAsyncGetStatus(&connectAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(wss failure diagnostics)", hr);
        return false;
    }

    WebSocketCompletionResult connectResult{};
    hr = HCGetWebSocketConnectResult(&connectAsync, &connectResult);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCGetWebSocketConnectResult(wss failure diagnostics)", hr);
        return false;
    }

    if (SUCCEEDED(connectResult.errorCode))
    {
        std::printf("[FAIL] Expected WSS connect diagnostics test to fail against an unused localhost port.\n");
        return false;
    }

    bool const hasExactPublicDiagnostic = connectResult.platformErrorCode == ConnectionRefusedError;
    bool const hasExactTraceDiagnostic = traceCapture.ContainsWebsocketConnectError(ConnectionRefusedError);
    if (!hasExactPublicDiagnostic && !hasExactTraceDiagnostic)
    {
        std::printf("[FAIL] Expected either an exact refused-connect platform error or a websocket trace with the refused-connect error.\n");
        std::printf("[INFO] WSS connect result HRESULT: 0x%08x (platform=%u)\n",
            static_cast<unsigned int>(connectResult.errorCode),
            static_cast<unsigned int>(connectResult.platformErrorCode));
        traceCapture.DumpWebsocketMessages();
        return false;
    }

    if (hasExactPublicDiagnostic)
    {
        std::printf("[INFO] WSS connect failure exposed exact platform error %u through the public result surface.\n",
            static_cast<unsigned int>(connectResult.platformErrorCode));
    }
    else
    {
        std::printf("[INFO] WSS connect returned HRESULT 0x%08x (platform=%u); verified exact refused-connect detail via websocket trace.\n",
            static_cast<unsigned int>(connectResult.errorCode),
            static_cast<unsigned int>(connectResult.platformErrorCode));
    }

    return true;
}

bool ValidateUpgradeResponseHeaders(
    HCWebsocketHandle websocket,
    CompressionExpectation compressionExpectation,
    char const* scenarioLabel)
{
    uint32_t numHeaders{};
    HRESULT hr = HCWebSocketGetNumResponseHeaders(websocket, &numHeaders);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketGetNumResponseHeaders", hr);
        return false;
    }

    if (numHeaders == 0)
    {
        std::printf("[FAIL] Expected websocket upgrade response headers but found none.\n");
        return false;
    }

    char const* headerName{};
    char const* headerValue{};
    hr = HCWebSocketGetResponseHeaderAtIndex(websocket, 0, &headerName, &headerValue);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketGetResponseHeaderAtIndex", hr);
        return false;
    }

    if (headerName == nullptr || headerValue == nullptr)
    {
        std::printf("[FAIL] Expected a response header at index 0.\n");
        return false;
    }

    char const* acceptHeader{};
    hr = HCWebSocketGetResponseHeader(websocket, "Sec-WebSocket-Accept", &acceptHeader);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketGetResponseHeader(Sec-WebSocket-Accept)", hr);
        return false;
    }

    if (acceptHeader == nullptr || acceptHeader[0] == '\0')
    {
        std::printf("[FAIL] Expected Sec-WebSocket-Accept in the upgrade response.\n");
        return false;
    }

    std::printf("[INFO] Upgrade response headers: %u\n", numHeaders);
    std::printf("[INFO] Sec-WebSocket-Accept: %s\n", acceptHeader);

    if (compressionExpectation == CompressionExpectation::Negotiated)
    {
        char const* extensionsHeader{};
        hr = HCWebSocketGetResponseHeader(websocket, "Sec-WebSocket-Extensions", &extensionsHeader);
        if (FAILED(hr))
        {
            PrintHr("[FAIL] HCWebSocketGetResponseHeader(Sec-WebSocket-Extensions)", hr);
            return false;
        }

        auto const extensions = extensionsHeader != nullptr ? std::string{ extensionsHeader } : std::string{};
        if (!ValidateNegotiatedCompressionExtensionHeader(
            extensions,
            "upgrade response headers",
            scenarioLabel))
        {
            return false;
        }
    }

    return true;
}

bool ValidateDefaultCompressionOptionsBehavior()
{
    ClientState state;
    HCWebsocketHandle websocket{ nullptr };
    ScopeGuard cleanup([&]()
    {
        if (websocket != nullptr)
        {
            HCWebSocketCloseHandle(websocket);
        }
    });

    HRESULT hr = HCWebSocketCreate(&websocket, OnTextMessage, nullptr, OnClose, &state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketCreate(default compression options validation)", hr);
        return false;
    }

    hr = ConfigureTestWebSocket(websocket, state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] ConfigureTestWebSocket(default compression options validation)", hr);
        return false;
    }

    hr = HCWebSocketSetOptions(websocket, HCWebSocketOptions::None);
    if (hr != S_OK)
    {
        PrintHr("[FAIL] Expected S_OK for HCWebSocketSetOptions(None)", hr);
        return false;
    }

    return true;
}

bool ValidateDeterministicReceiveLimit(
    CompressionEchoServer& server,
    XTaskQueueHandle queue)
{
    ClientState state;
    HCWebsocketHandle websocket{ nullptr };
    ScopeGuard cleanup([&]()
    {
        if (websocket != nullptr)
        {
            HCWebSocketCloseHandle(websocket);
        }
    });

    HRESULT hr = HCWebSocketCreate(&websocket, OnTextMessage, nullptr, OnClose, &state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketCreate(deterministic receive limit)", hr);
        return false;
    }

    hr = ConfigureTestWebSocket(websocket, state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] ConfigureTestWebSocket(deterministic receive limit)", hr);
        return false;
    }

    hr = HCWebSocketSetMaxReceiveBufferSize(websocket, ReceiveBufferSize);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSetMaxReceiveBufferSize(deterministic receive limit)", hr);
        return false;
    }

    hr = HCWebSocketSetOptions(websocket, HCWebSocketOptions::None);
    if (hr == E_NOT_SUPPORTED)
    {
        PrintHr("[INFO] HCWebSocketSetOptions(None)", hr);
        return true;
    }

    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSetOptions(None)", hr);
        return false;
    }

    server.ResetObservedConnection();

    XAsyncBlock connectAsync{};
    connectAsync.queue = queue;

    hr = HCWebSocketConnectAsync("ws://127.0.0.1:39002", "", websocket, &connectAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketConnectAsync(deterministic receive limit)", hr);
        return false;
    }

    hr = XAsyncGetStatus(&connectAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(connect deterministic receive limit)", hr);
        return false;
    }

    WebSocketCompletionResult connectResult{};
    hr = HCGetWebSocketConnectResult(&connectAsync, &connectResult);
    if (FAILED(hr) || FAILED(connectResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketConnectResult(deterministic receive limit)", hr);
        PrintHr("[FAIL] Connect result(deterministic receive limit)", connectResult.errorCode);
        return false;
    }

    if (!server.WaitForConnection())
    {
        std::printf("[FAIL] Timed out waiting for deterministic receive-limit connection.\n");
        if (!server.Error().empty())
        {
            std::printf("[FAIL] Server error: %s\n", server.Error().c_str());
        }
        return false;
    }

    if (!ValidateUpgradeResponseHeaders(
        websocket,
        CompressionExpectation::Unsupported,
        "DeterministicReceiveLimit"))
    {
        return false;
    }

    XAsyncBlock sendAsync{};
    sendAsync.queue = queue;

    hr = HCWebSocketSendMessageAsync(websocket, "hello", &sendAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSendMessageAsync(deterministic receive limit)", hr);
        return false;
    }

    hr = XAsyncGetStatus(&sendAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(send deterministic receive limit)", hr);
        return false;
    }

    WebSocketCompletionResult sendResult{};
    hr = HCGetWebSocketSendMessageResult(&sendAsync, &sendResult);
    if (FAILED(hr) || FAILED(sendResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketSendMessageResult(deterministic receive limit)", hr);
        PrintHr("[FAIL] Send result(deterministic receive limit)", sendResult.errorCode);
        return false;
    }

    if (!WaitForEcho(state, 1, "hello"))
    {
        std::printf("[FAIL] Timed out waiting for deterministic receive-limit baseline echo.\n");
        return false;
    }

    std::string const largePayload = BuildLargePayload();
    sendAsync = {};
    sendAsync.queue = queue;

    hr = HCWebSocketSendMessageAsync(websocket, largePayload.c_str(), &sendAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSendMessageAsync(large deterministic receive limit)", hr);
        return false;
    }

    hr = XAsyncGetStatus(&sendAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(send large deterministic receive limit)", hr);
        return false;
    }

    sendResult = {};
    hr = HCGetWebSocketSendMessageResult(&sendAsync, &sendResult);
    if (FAILED(hr) || FAILED(sendResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketSendMessageResult(large deterministic receive limit)", hr);
        PrintHr("[FAIL] Send large result(deterministic receive limit)", sendResult.errorCode);
        return false;
    }

    if (!WaitForClose(state, 1, HCWebSocketCloseStatus::TooLarge))
    {
        std::printf("[FAIL] Timed out waiting for deterministic receive-limit close.\n");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.textMessagesReceived != 1)
        {
            std::printf("[FAIL] Oversized deterministic receive unexpectedly surfaced a full-message callback.\n");
            return false;
        }
    }

    return true;
}

bool ValidateCompressionNegotiationScenario(
    CompressionEchoServer& server,
    XTaskQueueHandle queue,
    char const* scenarioLabel,
    HCWebSocketOptions options,
    bool expectServerNoContextTakeover,
    bool expectClientNoContextTakeover)
{
    ClientState state;
    HCWebsocketHandle websocket{ nullptr };
    ScopeGuard cleanup([&]()
    {
        if (websocket != nullptr)
        {
            HCWebSocketCloseHandle(websocket);
        }
    });

    HRESULT hr = HCWebSocketCreate(&websocket, OnTextMessage, nullptr, OnClose, &state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketCreate(negotiation scenario)", hr);
        return false;
    }

    hr = ConfigureTestWebSocket(websocket, state);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] ConfigureTestWebSocket(negotiation scenario)", hr);
        return false;
    }

    hr = HCWebSocketSetOptions(websocket, options);
    if (hr != S_OK)
    {
        PrintHr("[FAIL] Expected S_OK from HCWebSocketSetOptions(negotiation scenario)", hr);
        return false;
    }

    server.ResetObservedConnection();

    XAsyncBlock connectAsync{};
    connectAsync.queue = queue;
    hr = HCWebSocketConnectAsync("ws://127.0.0.1:39002", "", websocket, &connectAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketConnectAsync(negotiation scenario)", hr);
        return false;
    }

    hr = XAsyncGetStatus(&connectAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(connect negotiation scenario)", hr);
        return false;
    }

    WebSocketCompletionResult connectResult{};
    hr = HCGetWebSocketConnectResult(&connectAsync, &connectResult);
    if (FAILED(hr) || FAILED(connectResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketConnectResult(negotiation scenario)", hr);
        PrintHr("[FAIL] Connect result(negotiation scenario)", connectResult.errorCode);
        return false;
    }

    if (!server.WaitForConnection())
    {
        std::printf("[FAIL] Timed out waiting for server connection state for %s.\n", scenarioLabel);
        if (!server.Error().empty())
        {
            std::printf("[FAIL] Server error: %s\n", server.Error().c_str());
        }
        return false;
    }

    if (!server.Error().empty())
    {
        std::printf("[FAIL] Server error for %s: %s\n", scenarioLabel, server.Error().c_str());
        return false;
    }

    auto const requestedExtensions = server.RequestedExtensions();
    auto const negotiatedExtensions = server.NegotiatedExtensions();

    std::printf("[INFO] %s requested extensions: %s\n", scenarioLabel, requestedExtensions.empty() ? "<none>" : requestedExtensions.c_str());
    std::printf("[INFO] %s negotiated extensions: %s\n", scenarioLabel, negotiatedExtensions.empty() ? "<none>" : negotiatedExtensions.c_str());

    if (!ValidateRequestedCompressionExtensionHeader(
        requestedExtensions,
        "requested extensions",
        scenarioLabel,
        expectServerNoContextTakeover,
        expectClientNoContextTakeover))
    {
        return false;
    }

    if (!ValidateNegotiatedCompressionExtensionHeader(
        negotiatedExtensions,
        "negotiated extensions",
        scenarioLabel))
    {
        return false;
    }

    if (!ValidateUpgradeResponseHeaders(
        websocket,
        CompressionExpectation::Negotiated,
        scenarioLabel))
    {
        return false;
    }

    hr = HCWebSocketDisconnect(websocket);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketDisconnect(negotiation scenario)", hr);
        return false;
    }

    if (!WaitForClose(state, 1, HCWebSocketCloseStatus::Normal))
    {
        std::printf("[FAIL] Timed out waiting for WebSocket close callback after %s.\n", scenarioLabel);
        return false;
    }

    return true;
}

} // namespace

int main()
{
    HRESULT hr = HCInitialize(nullptr);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCInitialize", hr);
        return 1;
    }

    HCWebsocketHandle websocket{ nullptr };
    XTaskQueueHandle queue{ nullptr };

    ScopeGuard cleanup([&]()
    {
        if (websocket != nullptr)
        {
            HCWebSocketCloseHandle(websocket);
        }

        if (queue != nullptr)
        {
            XTaskQueueCloseHandle(queue);
        }

        HCCleanup();
    });

    hr = XTaskQueueCreate(
        XTaskQueueDispatchMode::ThreadPool,
        XTaskQueueDispatchMode::ThreadPool,
        &queue
    );
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XTaskQueueCreate", hr);
        return 1;
    }

    ClientState clientState;
    hr = HCWebSocketCreate(&websocket, OnTextMessage, nullptr, OnClose, &clientState);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketCreate", hr);
        return 1;
    }

#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
    hr = ConfigureTestWebSocket(websocket, clientState);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] ConfigureTestWebSocket", hr);
        return 1;
    }
#endif

    CompressionExpectation compressionExpectation{};
    hr = HCWebSocketSetOptions(websocket, HCWebSocketOptions::RequestCompression);
    if (hr == S_OK)
    {
        compressionExpectation = CompressionExpectation::Negotiated;
        PrintHr("[INFO] HCWebSocketSetOptions", hr);
    }
    else if (hr == E_NOT_SUPPORTED)
    {
#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
        compressionExpectation = CompressionExpectation::Unsupported;
        PrintHr("[INFO] HCWebSocketSetOptions", hr);
#else
        std::printf("[SKIP] Compression request not supported in this build/backend.\n");
        PrintHr("[SKIP] HCWebSocketSetOptions", hr);
        return SkipReturnCode;
#endif
    }
    else if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSetOptions", hr);
        return 1;
    }
    else
    {
        PrintHr("[FAIL] Unexpected HCWebSocketSetOptions result", hr);
        return 1;
    }

    if (compressionExpectation == CompressionExpectation::Negotiated)
    {
#if HC_RUN_WSS_CERT_STORE_TESTS
        if (!ValidateWssCertificateValidation(queue))
        {
            return 1;
        }
#endif

        if (!ValidateWssFailureDiagnostics(queue))
        {
            return 1;
        }

        if (!ValidateDefaultCompressionOptionsBehavior())
        {
            return 1;
        }
    }

    CompressionEchoServer server;
    if (!server.Start(TestPort))
    {
        return 1;
    }

    ScopeGuard serverGuard([&server]()
    {
        server.Stop();
    });

    if (!ValidateDeterministicReceiveLimit(server, queue))
    {
        return 1;
    }

    char const* uri = "ws://127.0.0.1:39002";
    XAsyncBlock connectAsync{};
    connectAsync.queue = queue;

    hr = HCWebSocketConnectAsync(uri, "", websocket, &connectAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketConnectAsync", hr);
        return 1;
    }

    hr = XAsyncGetStatus(&connectAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(connect)", hr);
        return 1;
    }

    WebSocketCompletionResult connectResult{};
    hr = HCGetWebSocketConnectResult(&connectAsync, &connectResult);
    if (FAILED(hr) || FAILED(connectResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketConnectResult", hr);
        PrintHr("[FAIL] Connect result", connectResult.errorCode);
        return 1;
    }

    if (!server.WaitForConnection())
    {
        std::printf("[FAIL] Timed out waiting for server connection state.\n");
        if (!server.Error().empty())
        {
            std::printf("[FAIL] Server error: %s\n", server.Error().c_str());
        }
        return 1;
    }

    if (!server.Error().empty())
    {
        std::printf("[FAIL] Server error: %s\n", server.Error().c_str());
        return 1;
    }

    auto const requestedExtensions = server.RequestedExtensions();
    auto const negotiatedExtensions = server.NegotiatedExtensions();

    std::printf("[INFO] Requested extensions: %s\n", requestedExtensions.empty() ? "<none>" : requestedExtensions.c_str());
    std::printf("[INFO] Negotiated extensions: %s\n", negotiatedExtensions.empty() ? "<none>" : negotiatedExtensions.c_str());

    if (!ValidateUpgradeResponseHeaders(
        websocket,
        compressionExpectation,
        "RequestCompression"))
    {
        return 1;
    }

    if (compressionExpectation == CompressionExpectation::Negotiated)
    {
        if (!ValidateRequestedCompressionExtensionHeader(
            requestedExtensions,
            "requested extensions",
            "RequestCompression",
            false,
            false))
        {
            return 1;
        }

        if (!ValidateNegotiatedCompressionExtensionHeader(
            negotiatedExtensions,
            "negotiated extensions",
            "RequestCompression"))
        {
            return 1;
        }

        std::printf("[INFO] RequestCompression negotiated permessage-deflate without no-context-takeover flags.\n");
    }
    else
    {
        std::printf("[INFO] Compression request unsupported on this backend; continuing with non-compression validation.\n");
    }

    XAsyncBlock sendAsync{};
    sendAsync.queue = queue;

    hr = HCWebSocketSendMessageAsync(websocket, "hello", &sendAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSendMessageAsync", hr);
        return 1;
    }

    hr = XAsyncGetStatus(&sendAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(send)", hr);
        return 1;
    }

    WebSocketCompletionResult sendResult{};
    hr = HCGetWebSocketSendMessageResult(&sendAsync, &sendResult);
    if (FAILED(hr) || FAILED(sendResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketSendMessageResult", hr);
        PrintHr("[FAIL] Send result", sendResult.errorCode);
        return 1;
    }

    if (!WaitForEcho(clientState, 1, "hello"))
    {
        std::printf("[FAIL] Timed out waiting for echoed message.\n");
        return 1;
    }

    std::string const largePayload = BuildLargePayload();
    sendAsync = {};
    sendAsync.queue = queue;

    hr = HCWebSocketSendMessageAsync(websocket, largePayload.c_str(), &sendAsync);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketSendMessageAsync(large)", hr);
        return 1;
    }

    hr = XAsyncGetStatus(&sendAsync, true);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] XAsyncGetStatus(send large)", hr);
        return 1;
    }

    sendResult = {};
    hr = HCGetWebSocketSendMessageResult(&sendAsync, &sendResult);
    if (FAILED(hr) || FAILED(sendResult.errorCode))
    {
        PrintHr("[FAIL] HCGetWebSocketSendMessageResult(large)", hr);
        PrintHr("[FAIL] Send large result", sendResult.errorCode);
        return 1;
    }

    if (!WaitForEcho(clientState, 2, largePayload))
    {
        std::printf("[FAIL] Timed out waiting for oversized text echo.\n");
        return 1;
    }

    hr = HCWebSocketDisconnect(websocket);
    if (FAILED(hr))
    {
        PrintHr("[FAIL] HCWebSocketDisconnect", hr);
        return 1;
    }

    if (!WaitForClose(clientState, 1, HCWebSocketCloseStatus::Normal))
    {
        std::printf("[FAIL] Timed out waiting for WebSocket close callback after disconnect.\n");
        return 1;
    }

    if (compressionExpectation == CompressionExpectation::Negotiated)
    {
        if (!ValidateCompressionNegotiationScenario(
            server,
            queue,
            "RequestCompression|CompressionServerNoContextTakeover",
            HCWebSocketOptions::RequestCompression |
                HCWebSocketOptions::CompressionServerNoContextTakeover,
            true,
            false))
        {
            return 1;
        }

        if (!ValidateCompressionNegotiationScenario(
            server,
            queue,
            "RequestCompression|CompressionClientNoContextTakeover",
            HCWebSocketOptions::RequestCompression |
                HCWebSocketOptions::CompressionClientNoContextTakeover,
            false,
            true))
        {
            return 1;
        }

        if (!ValidateCompressionNegotiationScenario(
            server,
            queue,
            "RequestCompression|CompressionServerNoContextTakeover|CompressionClientNoContextTakeover",
            HCWebSocketOptions::RequestCompression |
                HCWebSocketOptions::CompressionServerNoContextTakeover |
                HCWebSocketOptions::CompressionClientNoContextTakeover,
            true,
            true))
        {
            return 1;
        }
    }

    std::printf("[PASS] WebSocket integration behavior validated successfully.\n");
    return 0;
}
