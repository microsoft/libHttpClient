// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#ifndef HC_NOWEBSOCKETS

#include "websocketpp_websocket.h"
#include "Global/global.h"
#include "uri.h"
#include "WebSocket/websocket_options.h"
#include "websocketpp_configured_permessage_deflate.hpp"
#include "websocketpp_disabled_permessage_deflate.hpp"
#if !HC_PLATFORM_IS_MICROSOFT
#include "x509_cert_utilities.hpp"
#else
#include "wintls_socket.hpp"
#endif

#if HC_PLATFORM == HC_PLATFORM_GDK
#include "XSystem.h"
#endif

#if HC_PLATFORM_IS_APPLE
#include "Apple/utils_apple.h"
#endif

// Force websocketpp to use C++ std::error_code instead of Boost.
#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_

#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4100 4127 4512 4996 4701 4267 4244 )
#if (_MSC_VER >= 1900)
#define ASIO_ERROR_CATEGORY_NOEXCEPT noexcept(true)
#endif // (_MSC_VER >= 1900)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif

#include <websocketpp/config/asio_no_tls_client.hpp>
#if !HC_PLATFORM_IS_MICROSOFT
#include <websocketpp/config/asio_client.hpp>
#endif
#include <websocketpp/client.hpp>
#include <websocketpp/logger/levels.hpp>
#include <websocketpp/logger/stub.hpp>
#include <websocketpp/processors/base.hpp>
#if HC_PLATFORM == HC_PLATFORM_ANDROID
#include "../HTTP/Android/android_platform_context.h"
#endif

#include <limits>
#include <type_traits>

#if defined(_WIN32)
#pragma warning( pop )
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#define SUB_PROTOCOL_HEADER "Sec-WebSocket-Protocol"
#define WSPP_PING_INTERVAL_MS 1000
#define WSPP_SHUTDOWN_TIMEOUT_MS 5000
#define WSPP_SHUTDOWN_POLL_INTERVAL_MS 100

using namespace xbox::httpclient;

namespace
{

constexpr size_t WsppConfiguredMaxMessageSize = websocketpp::config::core_client::max_message_size;
constexpr size_t WsppMaxZlibInputSize = static_cast<size_t>((std::numeric_limits<uInt>::max)());
constexpr size_t WebSocketCallbackPayloadSizeLimit = static_cast<size_t>((std::numeric_limits<uint32_t>::max)());

static_assert(
    WsppConfiguredMaxMessageSize == WEBSOCKET_RECVBUFFER_MAXSIZE_DETERMINISTIC_DEFAULT,
    "deterministic default max message size must stay aligned with websocketpp's configured default");

static_assert(
    WsppConfiguredMaxMessageSize <= WsppMaxZlibInputSize,
    "websocketpp max message size must fit in zlib's uInt input width");
static_assert(
    WsppConfiguredMaxMessageSize <= WebSocketCallbackPayloadSizeLimit,
    "websocketpp max message size must fit in websocket callback size fields");

constexpr HCWebSocketOptions RequestCompressionOptionMask = HCWebSocketOptions::RequestCompression;
constexpr HCWebSocketOptions ServerNoContextTakeoverOptionMask = HCWebSocketOptions::CompressionServerNoContextTakeover;
constexpr HCWebSocketOptions ClientNoContextTakeoverOptionMask = HCWebSocketOptions::CompressionClientNoContextTakeover;

enum class CompressionClientPolicy
{
    Default,
    CompressionServerNoContextTakeover,
    CompressionClientNoContextTakeover,
    ServerAndClientNoContextTakeover
};

bool HasCompressionOption(HCWebSocketOptions options, HCWebSocketOptions optionMask) noexcept
{
    return (options & optionMask) != HCWebSocketOptions::None;
}

bool ShouldUseCompression(HCWebsocketHandle websocketHandle) noexcept
{
#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
    auto const options = websocketHandle->websocket->Options();
    return HasCompressionOption(options, RequestCompressionOptionMask);
#else
    UNREFERENCED_PARAMETER(websocketHandle);
    return false;
#endif
}

bool ShouldRequestServerNoContextTakeover(HCWebsocketHandle websocketHandle) noexcept
{
#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
    return HasCompressionOption(websocketHandle->websocket->Options(), ServerNoContextTakeoverOptionMask);
#else
    UNREFERENCED_PARAMETER(websocketHandle);
    return false;
#endif
}

bool ShouldRequestClientNoContextTakeover(HCWebsocketHandle websocketHandle) noexcept
{
#if defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
    return HasCompressionOption(websocketHandle->websocket->Options(), ClientNoContextTakeoverOptionMask);
#else
    UNREFERENCED_PARAMETER(websocketHandle);
    return false;
#endif
}

CompressionClientPolicy GetCompressionClientPolicy(HCWebsocketHandle websocketHandle) noexcept
{
    bool const requestServerNoContextTakeover = ShouldRequestServerNoContextTakeover(websocketHandle);
    bool const requestClientNoContextTakeover = ShouldRequestClientNoContextTakeover(websocketHandle);

    if (requestServerNoContextTakeover)
    {
        return requestClientNoContextTakeover ?
            CompressionClientPolicy::ServerAndClientNoContextTakeover :
            CompressionClientPolicy::CompressionServerNoContextTakeover;
    }

    return requestClientNoContextTakeover ?
        CompressionClientPolicy::CompressionClientNoContextTakeover :
        CompressionClientPolicy::Default;
}

long ClampWsppPongTimeoutMs(uint32_t pingIntervalSeconds) noexcept
{
    auto const pingIntervalMs = static_cast<uint64_t>(pingIntervalSeconds) * 1000ULL;
    auto const maxPongTimeoutMs = static_cast<uint64_t>((std::numeric_limits<long>::max)());
    return pingIntervalMs > maxPongTimeoutMs ? (std::numeric_limits<long>::max)() : static_cast<long>(pingIntervalMs);
}

size_t ResolveWsppMaxMessageSize(HCWebsocketHandle websocketHandle) noexcept
{
    auto const& websocket = websocketHandle->websocket;
    if (!websocket->UsesDeterministicSemantics())
    {
        return WsppConfiguredMaxMessageSize;
    }

    return websocket->DeterministicMaxReceiveBufferSize();
}

// Note: this logic is intentionally duplicated from TryParseWebSocketProxyUri in
// winhttp_connection.cpp to keep compilation units independent.
bool TryParseProxyUri(
    http_internal_string const& rawProxyUri,
    Uri& proxyUri
)
{
    proxyUri = Uri{ rawProxyUri };
    if (proxyUri.IsValid())
    {
        return true;
    }

    if (rawProxyUri.find("://") == http_internal_string::npos)
    {
        proxyUri = Uri{ "http://" + rawProxyUri };
    }

    return proxyUri.IsValid();
}

websocketpp::close::status::value ResolveObservedCloseCode(
    websocketpp::close::status::value localCloseCode,
    websocketpp::close::status::value remoteCloseCode,
    websocketpp::lib::error_code const& ec) noexcept
{
    auto closeCode = localCloseCode != websocketpp::close::status::blank ? localCloseCode : remoteCloseCode;
    if (closeCode == websocketpp::close::status::blank ||
        closeCode == websocketpp::close::status::abnormal_close)
    {
        auto const mappedCloseCode = websocketpp::processor::error::to_ws(ec);
        if (mappedCloseCode != websocketpp::close::status::blank)
        {
            closeCode = mappedCloseCode;
        }
    }

    return closeCode;
}

http_internal_string BuildProxyEndpointUri(Uri const& proxyUri)
{
    http_internal_string proxyEndpointUri{ proxyUri.Scheme() };
    proxyEndpointUri += "://";
    proxyEndpointUri += proxyUri.Host();
    if (!proxyUri.IsPortDefault() && proxyUri.Port() > 0)
    {
        proxyEndpointUri += ":";
        proxyEndpointUri += std::to_string(proxyUri.Port());
    }

    return proxyEndpointUri;
}

bool TryPercentDecodeUserInfo(http_internal_string const& value, http_internal_string& decoded)
{
    decoded.clear();
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%')
        {
            if (value.size() - i < 3)
            {
                return false;
            }

            uint8_t decodedByte = 0;
            if (!HexDecodePair(value[i + 1], value[i + 2], decodedByte))
            {
                return false;
            }

            decoded.push_back(static_cast<char>(decodedByte));
            i += 2;
            continue;
        }

        decoded.push_back(value[i]);
    }

    return true;
}

bool ParseProxyCredentials(
    Uri const& proxyUri,
    http_internal_string& username,
    http_internal_string& password
)
{
    auto const& userInfo = proxyUri.UserInfo();
    if (userInfo.empty())
    {
        return true;
    }

    auto const separator = userInfo.find(':');
    if (separator == http_internal_string::npos)
    {
        password.clear();
        return TryPercentDecodeUserInfo(userInfo, username);
    }

    return TryPercentDecodeUserInfo(userInfo.substr(0, separator), username) &&
        TryPercentDecodeUserInfo(userInfo.substr(separator + 1), password);
}

template<typename ConnectionPtr>
HRESULT ApplyProxySettings(
    ConnectionPtr const& con,
    Uri const& proxyUri,
    http_internal_string const& username,
    http_internal_string const& password,
    uint64_t websocketId
)
{
    websocketpp::lib::error_code ec;
    auto const proxyEndpointUri = BuildProxyEndpointUri(proxyUri);
    con->set_proxy(proxyEndpointUri.data(), ec);
    if (ec)
    {
        HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy failed", TO_ULL(websocketId));
        return E_FAIL;
    }

    if (!username.empty())
    {
        con->set_proxy_basic_auth(std::string{ username.c_str() }, std::string{ password.c_str() }, ec);
        if (ec)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy_basic_auth failed", TO_ULL(websocketId));
            return E_FAIL;
        }
    }

    return S_OK;
}

HRESULT ResolveEffectiveProxyDecryptsHttpsSetting(
    HCWebsocketHandle websocket,
    bool requestedAllowProxyToDecryptHttps,
    bool& effectiveAllowProxyToDecryptHttps
)
{
    effectiveAllowProxyToDecryptHttps = requestedAllowProxyToDecryptHttps;

#if HC_PLATFORM == HC_PLATFORM_GDK
    if (requestedAllowProxyToDecryptHttps && XSystemGetDeviceType() != XSystemDeviceType::Pc)
    {
        auto httpSingleton = get_http_singleton();
        RETURN_HR_IF(E_HC_NOT_INITIALISED, !httpSingleton);

        HC_TRACE_WARNING(WEBSOCKET, "Websocket [ID %llu]: On GDK console, TLS server validation is enforced regardless of ProxyDecryptsHttps to match the built-in WinHTTP WebSocket path", TO_ULL(websocket->websocket->id));
        if (!httpSingleton->m_disableAssertsForSSLValidationInDevSandboxes)
        {
            HC_TRACE_ERROR(WEBSOCKET, "On GDK console, TLS server validation is enforced regardless of ProxyDecryptsHttps to match the built-in WinHTTP WebSocket path.");
            HC_TRACE_ERROR(WEBSOCKET, "Call HCHttpDisableAssertsForSSLValidationInDevSandboxes() to turn this assert off");
            assert(false && "On GDK console, TLS server validation is enforced regardless of ProxyDecryptsHttps() to match the built-in WinHTTP WebSocket path.  See Output for more detail");
        }

        char sandbox[XSystemXboxLiveSandboxIdMaxBytes] = { 0 };
        HRESULT hr = XSystemGetXboxLiveSandboxId(XSystemXboxLiveSandboxIdMaxBytes, sandbox, nullptr);
        if (ShouldForceTlsValidationForGdkSandbox(hr, sandbox))
        {
            // Fail-closed: if we cannot determine the sandbox, or if the sandbox
            // is RETAIL, enforce TLS server validation unconditionally.
            effectiveAllowProxyToDecryptHttps = false;
        }
    }
#else
    UNREFERENCED_PARAMETER(websocket);
#endif

    return S_OK;
}

HRESULT HResultFromPlatformNetworkError(uint32_t platformErrorCode) noexcept
{
    if (platformErrorCode == 0)
    {
        return S_OK;
    }

    auto const signedPlatformErrorCode = static_cast<int32_t>(platformErrorCode);
    if (signedPlatformErrorCode <= 0)
    {
        return static_cast<HRESULT>(signedPlatformErrorCode);
    }

    return __HRESULT_FROM_WIN32(platformErrorCode);
}

HRESULT HResultFromConnectError(
    websocketpp::lib::error_code const& connectError,
    websocketpp::http::status_code::value connectStatusCode
) noexcept
{
    if (!connectError)
    {
        return S_OK;
    }

    if (connectError == make_error_code(websocketpp::processor::error::invalid_http_status))
    {
        return MAKE_HRESULT(1, FACILITY_HTTP, connectStatusCode);
    }

    if (connectError.category() == std::system_category() ||
        connectError.category() == std::generic_category() ||
        connectError.category() == asio::error::get_system_category())
    {
        return HResultFromPlatformNetworkError(static_cast<uint32_t>(connectError.value()));
    }

    return E_FAIL;
}

struct alevel_logger : websocketpp::log::stub
{
    using websocketpp::log::stub::stub;

#if HC_TRACE_VERBOSE_ENABLE
    void write(websocketpp::log::level level, const std::string& message) noexcept
    {
        write(level, message.c_str());
    }

    void write(websocketpp::log::level /*level*/, const char* message) noexcept
    {
        HC_TRACE_VERBOSE(WEBSOCKET, "%s", message);
    }

    bool static_test(websocketpp::log::level /*level*/) noexcept
    {
        return true;
    }

    bool dyanmic_test(websocketpp::log::level /*level*/) noexcept
    {
        return true;
    }
#endif
};

struct elevel_logger : websocketpp::log::stub
{
    using websocketpp::log::stub::stub;

#if HC_TRACE_ENABLE
    void write(websocketpp::log::level level, const std::string& message) noexcept
    {
        write(level, message.c_str());
    }

    void write(websocketpp::log::level level, const char* message) noexcept
    {
        switch (level)
        {
            case websocketpp::log::elevel::devel:
                HC_TRACE_VERBOSE(WEBSOCKET, "%s", message);
                break;
            case websocketpp::log::elevel::library:
                HC_TRACE_INFORMATION(WEBSOCKET, "%s", message);
                break;
            case websocketpp::log::elevel::info:
                HC_TRACE_IMPORTANT(WEBSOCKET, "%s", message);
                break;
            case websocketpp::log::elevel::warn:
                HC_TRACE_WARNING(WEBSOCKET, "%s", message);
                break;
            case websocketpp::log::elevel::rerror:
            case websocketpp::log::elevel::fatal:
                HC_TRACE_ERROR(WEBSOCKET, "%s", message);
                break;
            case websocketpp::log::elevel::none:
            case websocketpp::log::elevel::all:
            default:
                break;
        }
    }

    bool static_test(websocketpp::log::level /*level*/) noexcept
    {
        return HC_TRACE_ENABLE;
    }

    bool dyanmic_test(websocketpp::log::level /*level*/) noexcept
    {
        return HC_TRACE_ENABLE;
    }
#endif
};

template<typename base_config>
struct httpclient_config : base_config
{
    /// Logging policies
    using alog_type = alevel_logger;
    using elog_type = elevel_logger;

    /// Default static error logging channels
    static const websocketpp::log::level alog_level = websocketpp::log::alevel::all;

    /// Default static access logging channels
    static const websocketpp::log::level elog_level = websocketpp::log::elevel::all;

    struct transport_config : public base_config::transport_config
    {
        using concurrency_type = typename base_config::transport_config::concurrency_type;
        using alog_type = typename httpclient_config::alog_type;
        using elog_type = typename httpclient_config::elog_type;
        using request_type = typename base_config::transport_config::request_type;
        using response_type = typename base_config::transport_config::response_type;
        using socket_type = typename base_config::transport_config::socket_type;
    };

    using transport_type = websocketpp::transport::asio::endpoint<transport_config>;
};

using ws = httpclient_config<websocketpp::config::asio_client>;
#if HC_PLATFORM_IS_MICROSOFT
struct wintls_asio_client_config : public websocketpp::config::core_client
{
    typedef wintls_asio_client_config type;
    typedef websocketpp::config::core_client base;

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

using wss = httpclient_config<wintls_asio_client_config>;
#else
using wss = httpclient_config<websocketpp::config::asio_tls_client>;
#endif

template<typename base_config, bool RequestServerNoContextTakeover, bool RequestClientNoContextTakeover>
struct httpclient_compression_config : httpclient_config<base_config>
{
    struct permessage_deflate_config
    {
        using request_type = typename httpclient_config<base_config>::request_type;

        // Keep websocketpp's response-side negotiation behavior explicit: accept
        // server requests to disable context takeover and reduce the outgoing
        // compression window as far as the vendored extension allows.
        static const bool allow_disabling_context_takeover = true;
        static const uint8_t minimum_outgoing_window_bits = 8;
    };

    using permessage_deflate_type =
        xbox::httpclient::configured_permessage_deflate<
            permessage_deflate_config,
            RequestServerNoContextTakeover,
            RequestClientNoContextTakeover>;
};

using ws_compression = httpclient_compression_config<websocketpp::config::asio_client, false, false>;
using ws_compression_server_no_context_takeover = httpclient_compression_config<websocketpp::config::asio_client, true, false>;
using ws_compression_client_no_context_takeover = httpclient_compression_config<websocketpp::config::asio_client, false, true>;
using ws_compression_server_and_client_no_context_takeover = httpclient_compression_config<websocketpp::config::asio_client, true, true>;
#if HC_PLATFORM_IS_MICROSOFT
using wss_compression = httpclient_compression_config<wintls_asio_client_config, false, false>;
using wss_compression_server_no_context_takeover = httpclient_compression_config<wintls_asio_client_config, true, false>;
using wss_compression_client_no_context_takeover = httpclient_compression_config<wintls_asio_client_config, false, true>;
using wss_compression_server_and_client_no_context_takeover = httpclient_compression_config<wintls_asio_client_config, true, true>;
#else
using wss_compression = httpclient_compression_config<websocketpp::config::asio_tls_client, false, false>;
using wss_compression_server_no_context_takeover = httpclient_compression_config<websocketpp::config::asio_tls_client, true, false>;
using wss_compression_client_no_context_takeover = httpclient_compression_config<websocketpp::config::asio_tls_client, false, true>;
using wss_compression_server_and_client_no_context_takeover = httpclient_compression_config<websocketpp::config::asio_tls_client, true, true>;
#endif

template<bool IsTlsClient>
struct compression_client_config_types;

template<>
struct compression_client_config_types<true>
{
    using default_type = wss_compression;
    using server_no_context_takeover_type = wss_compression_server_no_context_takeover;
    using client_no_context_takeover_type = wss_compression_client_no_context_takeover;
    using server_and_client_no_context_takeover_type = wss_compression_server_and_client_no_context_takeover;
};

template<>
struct compression_client_config_types<false>
{
    using default_type = ws_compression;
    using server_no_context_takeover_type = ws_compression_server_no_context_takeover;
    using client_no_context_takeover_type = ws_compression_client_no_context_takeover;
    using server_and_client_no_context_takeover_type = ws_compression_server_and_client_no_context_takeover;
};

struct websocket_outgoing_message
{
    XAsyncBlock* async{ nullptr };
    http_internal_string payload;
    http_internal_vector<uint8_t> payloadBinary;
    websocketpp::lib::error_code error;
    uint64_t id{ 0 };
};

struct wspp_websocket_impl : public hc_websocket_impl, public std::enable_shared_from_this<wspp_websocket_impl>
{
private:

    enum State {
        CONNECTING,
        CONNECTED,
        DISCONNECTING,
        DISCONNECTED
    };

public:
    wspp_websocket_impl(
        HCWebsocketHandle hcHandle,
        const char* uri,
        const char* subprotocol
    ) :
        m_hcWebsocketHandle{ hcHandle },
        m_uri{ uri },
        m_subprotocol{ subprotocol }
    {
    }

    ~wspp_websocket_impl()
    {
        // Because we pass shared pointers to wspp_client handlers we should never blow up while we are connected
        ASSERT(m_state == DISCONNECTED);

        if (m_backgroundQueue)
        {
            XTaskQueueCloseHandle(m_backgroundQueue);
        }
    }

    HRESULT connect(XAsyncBlock* async)
    {
        bool const enableCompression = ShouldUseCompression(m_hcWebsocketHandle);
        auto const compressionClientPolicy = GetCompressionClientPolicy(m_hcWebsocketHandle);
        bool allowProxyToDecryptHttps = m_hcWebsocketHandle->websocket->ProxyDecryptsHttps();
        RETURN_IF_FAILED(ResolveEffectiveProxyDecryptsHttpsSetting(
            m_hcWebsocketHandle,
            m_hcWebsocketHandle->websocket->ProxyDecryptsHttps(),
            allowProxyToDecryptHttps));

        bool const isTlsClient = m_uri.Scheme() == "wss";

        if (enableCompression)
        {
            if (isTlsClient)
            {
                return create_compression_client<true>(compressionClientPolicy, async, allowProxyToDecryptHttps);
            }

            return create_compression_client<false>(compressionClientPolicy, async, allowProxyToDecryptHttps);
        }

        if (isTlsClient)
        {
            return create_client<wss, true, false>(async, allowProxyToDecryptHttps);
        }

        return create_client<ws, false, false>(async, allowProxyToDecryptHttps);
    }

    HRESULT send(XAsyncBlock* async, const char* payloadPtr)
    {
        if (payloadPtr == nullptr)
        {
            return E_INVALIDARG;
        }

        if (m_state != CONNECTED)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Client not connected");
            return E_UNEXPECTED;
        }

        auto httpSingleton = get_http_singleton();
        if (httpSingleton == nullptr)
        {
            return E_HC_NOT_INITIALISED;
        }

        http_internal_string payload(payloadPtr);
        if (payload.length() == 0)
        {
            return E_INVALIDARG;
        }
        RETURN_HR_IF(E_INVALIDARG, payload.length() > WsppMaxZlibInputSize);

        websocket_outgoing_message message;
        message.async = async;
        message.payload = std::move(payload);
        message.id = ++httpSingleton->m_lastId;

        {
            // Only actually have to take the lock if touching the queue.
            std::lock_guard<std::recursive_mutex> lock(m_outgoingMessageQueueLock);
            m_outgoingMessageQueue.push(std::move(message));
        }

        if (++m_numSends == 1) // No sends in progress
        {
            // Start sending the message
            return send_msg();
        }
        return S_OK;
    }

    HRESULT sendBinary(XAsyncBlock* async, const uint8_t* payloadBytes, uint32_t payloadSize)
    {
        if (payloadBytes == nullptr)
        {
            return E_INVALIDARG;
        }

        if (m_state != CONNECTED)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Client not connected");
            return E_UNEXPECTED;
        }

        auto httpSingleton = get_http_singleton();
        if (httpSingleton == nullptr)
        {
            return E_HC_NOT_INITIALISED;
        }

        if (payloadSize == 0)
        {
            return E_INVALIDARG;
        }
        RETURN_HR_IF(E_INVALIDARG, static_cast<size_t>(payloadSize) > WsppMaxZlibInputSize);

        websocket_outgoing_message message;
        message.async = async;
        message.payloadBinary.assign(payloadBytes, payloadBytes + payloadSize);
        message.id = ++httpSingleton->m_lastId;

        {
            // Only actually have to take the lock if touching the queue.
            std::lock_guard<std::recursive_mutex> lock(m_outgoingMessageQueueLock);
            m_outgoingMessageQueue.push(std::move(message));
        }

        if (++m_numSends == 1) // No sends in progress
        {
            // Start sending the message
            return send_msg();
        }
        return S_OK;
    }

    HRESULT close()
    {
        return close(HCWebSocketCloseStatus::Normal);
    }

    HRESULT close(HCWebSocketCloseStatus status)
    {
        std::lock_guard<std::recursive_mutex> lock(m_wsppClientLock);
        if (m_state == CONNECTED)
        {
            m_state = DISCONNECTING;

            websocketpp::lib::error_code ec{};
            invoke_wspp_client([this, status, &ec](auto& client)
            {
                client.close(m_con, static_cast<websocketpp::close::status::value>(status), std::string(), ec);
            });

            return ec ? E_FAIL : S_OK;
        }
        else
        {
            return E_UNEXPECTED;
        }
    }

private:
    template<typename WebsocketConfigType, bool IsTlsClient, bool UsesCompression>
    HRESULT create_client(XAsyncBlock* async, bool allowProxyToDecryptHttps)
    {
        m_client = std::unique_ptr<websocketpp_client_base>(new websocketpp_client_impl<WebsocketConfigType, IsTlsClient, UsesCompression>());
        return create_client_impl<WebsocketConfigType>(
            async,
            allowProxyToDecryptHttps,
            std::integral_constant<bool, IsTlsClient>{});
    }

    template<bool IsTlsClient>
    HRESULT create_compression_client(
        CompressionClientPolicy compressionClientPolicy,
        XAsyncBlock* async,
        bool allowProxyToDecryptHttps)
    {
        using config_types = compression_client_config_types<IsTlsClient>;

        switch (compressionClientPolicy)
        {
        case CompressionClientPolicy::Default:
            return create_client<typename config_types::default_type, IsTlsClient, true>(async, allowProxyToDecryptHttps);

        case CompressionClientPolicy::CompressionServerNoContextTakeover:
            return create_client<typename config_types::server_no_context_takeover_type, IsTlsClient, true>(async, allowProxyToDecryptHttps);

        case CompressionClientPolicy::CompressionClientNoContextTakeover:
            return create_client<typename config_types::client_no_context_takeover_type, IsTlsClient, true>(async, allowProxyToDecryptHttps);

        case CompressionClientPolicy::ServerAndClientNoContextTakeover:
            return create_client<typename config_types::server_and_client_no_context_takeover_type, IsTlsClient, true>(async, allowProxyToDecryptHttps);
        }

        ASSERT(false);
        return E_FAIL;
    }

    template<typename WebsocketConfigType>
    HRESULT create_client_impl(XAsyncBlock* async, bool allowProxyToDecryptHttps, std::false_type)
    {
        UNREFERENCED_PARAMETER(allowProxyToDecryptHttps);
        return connect_impl<WebsocketConfigType>(async);
    }

    template<typename WebsocketConfigType>
    HRESULT create_client_impl(XAsyncBlock* async, bool allowProxyToDecryptHttps, std::true_type)
    {
        // Configure the TLS-specific websocketpp hooks after connect() has selected the
        // concrete client type for this connection.
#if HC_PLATFORM_IS_MICROSOFT
#if HC_PLATFORM == HC_PLATFORM_GDK
        // Defense-in-depth: never disable cert validation on GDK console,
        // regardless of sandbox. ResolveEffectiveProxyDecryptsHttpsSetting is
        // the primary gate; this is the fail-closed backstop at the point
        // where the WinTLS context is actually configured.
        allowProxyToDecryptHttps = ApplyTlsValidationBackstopForGdkConsole(
            allowProxyToDecryptHttps,
            XSystemGetDeviceType() == XSystemDeviceType::Pc);
#endif
        auto& client = m_client->impl<WebsocketConfigType>();
        client.set_certificate_revocation_check(!allowProxyToDecryptHttps);
        client.set_tls_init_handler([allowProxyToDecryptHttps](websocketpp::connection_hdl)
        {
            auto tlsContext = websocketpp::lib::shared_ptr<wintls::context>(new wintls::context(wintls::method::system_default));
            tlsContext->use_default_certificates(true);
            tlsContext->verify_server_certificate(!allowProxyToDecryptHttps);
            return tlsContext;
        });
#else
        auto sharedThis{ shared_from_this() };
        auto& client = m_client->impl<WebsocketConfigType>();
        client.set_tls_init_handler([sharedThis](websocketpp::connection_hdl)
        {
            auto sslContext = websocketpp::lib::shared_ptr<asio::ssl::context>(new asio::ssl::context(asio::ssl::context::sslv23));
            sslContext->set_default_verify_paths();
            sslContext->set_options(asio::ssl::context::default_workarounds);
            sslContext->set_verify_mode(asio::ssl::context::verify_peer);

            sharedThis->m_opensslFailed = false;
            sslContext->set_verify_callback([sharedThis](bool preverified, asio::ssl::verify_context &verifyCtx)
            {
                // Allow debugging proxies that decrypt HTTPS and re-sign the connection.
                if (sharedThis->m_hcWebsocketHandle->websocket->ProxyDecryptsHttps())
                {
                    return true;
                }

                // Record the first OpenSSL verification failure and keep walking the chain so
                // verify_cert_chain_platform_specific(...) can make the final decision at the leaf.
                if (!preverified)
                {
                    sharedThis->m_opensslFailed = true;
                }
                if (sharedThis->m_opensslFailed)
                {
                    return xbox::httpclient::verify_cert_chain_platform_specific(verifyCtx, sharedThis->m_uri.Host());
                }
                asio::ssl::rfc2818_verification rfc2818(sharedThis->m_uri.Host().data());
                return rfc2818(preverified, verifyCtx);
            });

            // OpenSSL stores some per-thread state that is only reclaimed when the library is
            // unloaded. Because websocketpp::client::get_connection(...) creates the TLS context
            // on the caller's connect(...) thread, clean up that thread-local state here as well
            // as on the background websocketpp thread below.
#if HC_PLATFORM == HC_PLATFORM_ANDROID || HC_PLATFORM_IS_APPLE || HC_PLATFORM == HC_PLATFORM_LINUX
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            ERR_remove_thread_state(nullptr);
#pragma clang diagnostic pop
#else
            ERR_remove_thread_state(nullptr);
#endif

            return sslContext;
        });

        // Configure the underlying TLS socket after the SSL context is ready. libHttpClient does
        // not expose a separate SNI override, so use the URI host name for TLS SNI.
        client.set_socket_init_handler([sharedThis](websocketpp::connection_hdl, asio::ssl::stream<asio::ip::tcp::socket> &ssl_stream)
        {
            SSL_set_tlsext_host_name(ssl_stream.native_handle(), sharedThis->m_uri.Host().data());
        });
#endif

        return connect_impl<WebsocketConfigType>(async);
    }

    template<bool IsTlsClient, typename Operation>
    void invoke_compression_wspp_client(Operation&& operation)
    {
        using config_types = compression_client_config_types<IsTlsClient>;

        switch (GetCompressionClientPolicy(m_hcWebsocketHandle))
        {
        case CompressionClientPolicy::Default:
            operation(m_client->impl<typename config_types::default_type>());
            return;

        case CompressionClientPolicy::CompressionServerNoContextTakeover:
            operation(m_client->impl<typename config_types::server_no_context_takeover_type>());
            return;

        case CompressionClientPolicy::CompressionClientNoContextTakeover:
            operation(m_client->impl<typename config_types::client_no_context_takeover_type>());
            return;

        case CompressionClientPolicy::ServerAndClientNoContextTakeover:
            operation(m_client->impl<typename config_types::server_and_client_no_context_takeover_type>());
            return;
        }

        ASSERT(false);
    }

    template<typename Operation>
    void invoke_compression_wspp_client(Operation&& operation)
    {
        if (m_client->is_tls_client())
        {
            invoke_compression_wspp_client<true>(std::forward<Operation>(operation));
            return;
        }

        invoke_compression_wspp_client<false>(std::forward<Operation>(operation));
    }

    template<typename Operation>
    void invoke_wspp_client(Operation&& operation)
    {
        ASSERT(m_client != nullptr);

        bool const enableCompression = m_client->uses_compression();
        if (enableCompression)
        {
            invoke_compression_wspp_client(std::forward<Operation>(operation));
            return;
        }

        if (m_client->is_tls_client())
        {
            operation(m_client->impl<wss>());
        }
        else
        {
            operation(m_client->impl<ws>());
        }
    }

    template<typename WebsocketConfigType>
    void complete_connect_start_failure(
        XAsyncBlock* async,
        websocketpp::lib::error_code connectError
    )
    {
        {
            std::lock_guard<std::recursive_mutex> lock{ m_wsppClientLock };

            if (m_client != nullptr)
            {
                auto& client = m_client->impl<WebsocketConfigType>();
                client.stop_perpetual();
                client.stop();
                m_client.reset();
            }

            m_state = DISCONNECTED;
        }

        {
            std::lock_guard<std::mutex> lock{ m_websocketThreadStateMutex };
            m_websocketThreadExited = true;
        }
        m_websocketThreadStateCondition.notify_all();

        m_connectError = connectError;
        m_connectStatusCode = websocketpp::http::status_code::value{};
        XAsyncComplete(async, S_OK, sizeof(WebSocketCompletionResult));
    }

    template<typename WebsocketConfigType>
    HRESULT connect_impl(XAsyncBlock* async)
    {
        if (async->queue)
        {
            RETURN_IF_FAILED(XTaskQueueDuplicateHandle(
                async->queue,
                &m_backgroundQueue));
        }

        auto &client = m_client->impl<WebsocketConfigType>();

        // Keep inbound websocketpp payloads within the widths required by zlib and our callback signatures.
        client.set_max_message_size(ResolveWsppMaxMessageSize(m_hcWebsocketHandle));

        const auto pingIntervalMs = ClampWsppPongTimeoutMs(m_hcWebsocketHandle->websocket->PingInterval());
        client.set_pong_timeout(pingIntervalMs); // default ping interval is 0, which disables the timeout

        client.init_asio();
        client.start_perpetual();

        auto sharedThis { shared_from_this() };

        ASSERT(m_state == DISCONNECTED);
        client.set_open_handler([sharedThis, async](websocketpp::connection_hdl hdl)
        {
            ASSERT(sharedThis->m_state == CONNECTING);
            sharedThis->m_state = CONNECTED;
            sharedThis->set_response_headers<WebsocketConfigType>(hdl);
            sharedThis->set_connection_error<WebsocketConfigType>();
            sharedThis->set_connect_status<WebsocketConfigType>();
            sharedThis->send_ping();
            XAsyncComplete(async, S_OK, sizeof(WebSocketCompletionResult));
        });

        client.set_fail_handler([sharedThis, async](websocketpp::connection_hdl hdl)
        {
            ASSERT(sharedThis->m_state == CONNECTING);
            sharedThis->set_response_headers<WebsocketConfigType>(hdl);
            sharedThis->set_connection_error<WebsocketConfigType>();
            sharedThis->set_connect_status<WebsocketConfigType>();
            sharedThis->shutdown_wspp_impl<WebsocketConfigType>(
                [
                    sharedThis,
                    async
                ]
            {
                XAsyncComplete(async, S_OK, sizeof(WebSocketCompletionResult));
            });
        });

        client.set_message_handler([sharedThis](websocketpp::connection_hdl, typename WebsocketConfigType::message_type::ptr const& msg)
        {
            HCWebSocketMessageFunction messageFunc{ nullptr };
            HCWebSocketBinaryMessageFunction binaryMessageFunc{ nullptr };
            void* callbackContext{ nullptr };
            auto hr = HCWebSocketGetEventFunctions(sharedThis->m_hcWebsocketHandle, &messageFunc, &binaryMessageFunc, nullptr, &callbackContext);

            if (SUCCEEDED(hr))
            {
                ASSERT(messageFunc && binaryMessageFunc);

                auto const& payload = msg->get_raw_payload();
                if (msg->get_opcode() == websocketpp::frame::opcode::text)
                {
                    messageFunc(sharedThis->m_hcWebsocketHandle, payload.c_str(), callbackContext);
                }
                else if (msg->get_opcode() == websocketpp::frame::opcode::binary)
                {
                    binaryMessageFunc(sharedThis->m_hcWebsocketHandle, (uint8_t*)payload.data(), (uint32_t)payload.size(), callbackContext);
                }
            }
        });

        client.set_close_handler([sharedThis](websocketpp::connection_hdl)
        {
            ASSERT(sharedThis->m_state == CONNECTED || sharedThis->m_state == DISCONNECTING);
            {
                std::lock_guard<std::recursive_mutex> lock{ sharedThis->m_wsppClientLock };
                if (sharedThis->m_client != nullptr)
                {
                    auto& closeClient = sharedThis->m_client->impl<WebsocketConfigType>();
                    websocketpp::lib::error_code connectionEc{};
                    auto connection = closeClient.get_con_from_hdl(sharedThis->m_con, connectionEc);
                    if (!connectionEc && connection)
                    {
                        sharedThis->m_closeCode = ResolveObservedCloseCode(
                            connection->get_local_close_code(),
                            connection->get_remote_close_code(),
                            connection->get_ec());
                    }
                }
            }

            sharedThis->shutdown_wspp_impl<WebsocketConfigType>([sharedThis]()
                {
                    HCWebSocketCloseEventFunction closeFunc{ nullptr };
                    void* context{ nullptr };

                    HCWebSocketGetEventFunctions(sharedThis->m_hcWebsocketHandle, nullptr, nullptr, &closeFunc, &context);
                    if (closeFunc)
                    {
                        closeFunc(sharedThis->m_hcWebsocketHandle, static_cast<HCWebSocketCloseStatus>(sharedThis->m_closeCode), context);
                    }
                });
        });

        client.set_pong_timeout_handler([sharedThis](websocketpp::connection_hdl, std::string)
        {
            sharedThis->close(HCWebSocketCloseStatus::PolicyViolation);
        });

        // Set User Agent specified by the user. This needs to happen before any connection is created
        const auto& headers = m_hcWebsocketHandle->websocket->Headers();

        auto user_agent_it = headers.find(websocketpp::user_agent);
        if (user_agent_it != headers.end())
        {
            client.set_user_agent(user_agent_it->second.data());
        }

        // Get the connection handle to save for later, have to create temporary
        // because type erasure occurs with connection_hdl.
        websocketpp::lib::error_code ec;
        auto con = client.get_connection(m_uri.FullPath().data(), ec);
        m_con = con;
        if (ec.value() != 0)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp get_connection failed", TO_ULL(m_hcWebsocketHandle->websocket->id));
            return E_FAIL;
        }

        // Add any request headers specified by the user.
        for (const auto & header : headers)
        {
            // Subprotocols are handled separately below
            if (str_icmp(header.first, SUB_PROTOCOL_HEADER) != 0)
            {
                con->append_header(header.first.data(), header.second.data());
            }
        }

        // Add any specified subprotocols.
        if (!m_subprotocol.empty())
        {
            con->add_subprotocol(m_subprotocol.data(), ec);
            if (ec.value())
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: add_subprotocol failed", TO_ULL(m_hcWebsocketHandle->websocket->id));
                return E_FAIL;
            }
        }

        // Setup proxy options.
        if (!m_hcWebsocketHandle->websocket->ProxyUri().empty())
        {
            Uri explicitProxyUri;
            if (!TryParseProxyUri(m_hcWebsocketHandle->websocket->ProxyUri(), explicitProxyUri))
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: explicit proxy URI was invalid", TO_ULL(m_hcWebsocketHandle->websocket->id));
                return E_INVALIDARG;
            }

            http_internal_string username;
            http_internal_string password;
            if (!ParseProxyCredentials(explicitProxyUri, username, password))
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: explicit proxy URI credentials were invalid", TO_ULL(m_hcWebsocketHandle->websocket->id));
                return E_INVALIDARG;
            }

            RETURN_IF_FAILED(ApplyProxySettings(con, explicitProxyUri, username, password, m_hcWebsocketHandle->websocket->id));
        }
#if HC_PLATFORM_IS_MICROSOFT
        else
        {
            // On windows platforms use the IE proxy if the user didn't specify one
            Uri proxyUri;
            auto proxyType = get_ie_proxy_info(proxy_protocol::https, proxyUri);

            if (proxyType == proxy_type::named_proxy)
            {
                RETURN_IF_FAILED(ApplyProxySettings(con, proxyUri, http_internal_string{}, http_internal_string{}, m_hcWebsocketHandle->websocket->id));
            }
        }
#elif HC_PLATFORM_IS_APPLE
    else
    {
        Uri proxyUri;
        http_internal_string username;
        http_internal_string password;
        if (getSystemProxyForUri(m_uri, &proxyUri, &username, &password))
        {
            RETURN_IF_FAILED(ApplyProxySettings(con, proxyUri, username, password, m_hcWebsocketHandle->websocket->id));
        }
    }
#endif
        // Initialize the 'connect' XAsyncBlock here, but the actual work will happen on the ASIO background thread below.
        auto hr = XAsyncBegin(async, shared_ptr_cache::store(shared_from_this()), (void*)HCWebSocketConnectAsync, __FUNCTION__,
            [](XAsyncOp op, const XAsyncProviderData* data)
        {
            if (op == XAsyncOp::GetResult)
            {
                auto context = shared_ptr_cache::fetch<wspp_websocket_impl>(data->context);
                if (context == nullptr)
                {
                    return E_HC_NOT_INITIALISED;
                }

                auto result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                result->websocket = context->m_hcWebsocketHandle;
                result->platformErrorCode = static_cast<uint32_t>(context->m_connectError.value());
                result->errorCode = HResultFromConnectError(context->m_connectError, context->m_connectStatusCode);
            }
            else if (op == XAsyncOp::Cleanup)
            {
                shared_ptr_cache::remove(data->context);
            }
            return S_OK;
        });

        if (SUCCEEDED(hr))
        {
            m_state = CONNECTING;
            {
                std::lock_guard<std::mutex> lock{ m_websocketThreadStateMutex };
                m_websocketThreadExited = false;
            }

            client.connect(con);

            try
            {
                struct client_context
                {
                    client_context(websocketpp::client<WebsocketConfigType>& _client) : client(_client) {}
                    websocketpp::client<WebsocketConfigType>& client;
                };
                auto context = http_allocate_shared<client_context>(client);

                m_websocketThread = std::thread([context, sharedThis, id{ m_hcWebsocketHandle->websocket->id }]()
                {
                    struct thread_exit_guard
                    {
                        std::shared_ptr<wspp_websocket_impl> websocket;

                        ~thread_exit_guard()
                        {
                            std::lock_guard<std::mutex> lock{ websocket->m_websocketThreadStateMutex };
                            websocket->m_websocketThreadExited = true;
                            websocket->m_websocketThreadStateCondition.notify_all();
                        }
                    } threadExitGuard{ sharedThis };

                    HC_TRACE_INFORMATION(WEBSOCKET, "id=%u Wspp client work thread starting", id);

#if HC_PLATFORM == HC_PLATFORM_ANDROID
                    JavaVM* javaVm = nullptr;
                    {   
                        // Allow our singleton to go out of scope quickly once we're done with it
                        auto httpSingleton = xbox::httpclient::get_http_singleton();
                        if (httpSingleton)
                        {
                            auto platformContext = httpSingleton->m_performEnv->androidPlatformContext;
                            javaVm = platformContext->GetJavaVm();
                        }
                    }

                    if (javaVm == nullptr)
                    {
                        HC_TRACE_ERROR(HTTPCLIENT, "javaVm is null");
                        throw std::runtime_error("JavaVm is null");
                    }

                    JNIEnv* jniEnv = nullptr;
                    if (javaVm->AttachCurrentThread(&jniEnv, nullptr) != 0)
                    {
                        assert(false);
                    }
#endif
                    try
                    {
                        context->client.run();
                    }
                    catch (...)
                    {
                        HC_TRACE_ERROR(WEBSOCKET, "Caught exception in wspp client::run!");
                    }

                    // OpenSSL stores some per thread state that never will be cleaned up until
                    // the dll is unloaded. If static linking, like we do, the state isn't cleaned up
                    // at all and will be reported as leaks.
                    // See http://www.openssl.org/support/faq.html#PROG13
#if !HC_PLATFORM_IS_MICROSOFT
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                    ERR_remove_thread_state(nullptr);
#pragma clang diagnostic pop
#if HC_PLATFORM == HC_PLATFORM_ANDROID
                    javaVm->DetachCurrentThread();
#endif // HC_PLATFORM_ANDROID
#endif // !HC_PLATFORM_IS_MICROSOFT

                    HC_TRACE_INFORMATION(WEBSOCKET, "id=%u Wspp client work thread end", id);
                });
                hr = S_OK;
            }
            catch (std::system_error const& err)
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: couldn't create background websocket thread (%d)", TO_ULL(m_hcWebsocketHandle->websocket->id), err.code().value());
                complete_connect_start_failure<WebsocketConfigType>(async, websocketpp::error::make_error_code(websocketpp::error::general));
                return S_OK;
            }
            catch (std::exception const& err)
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: websocket thread startup failed: %s", TO_ULL(m_hcWebsocketHandle->websocket->id), err.what());
                complete_connect_start_failure<WebsocketConfigType>(async, websocketpp::error::make_error_code(websocketpp::error::general));
                return S_OK;
            }
            catch (...)
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: websocket thread startup failed with unknown exception", TO_ULL(m_hcWebsocketHandle->websocket->id));
                complete_connect_start_failure<WebsocketConfigType>(async, websocketpp::error::make_error_code(websocketpp::error::general));
                return S_OK;
            }
        }

        return hr;
    }

    struct send_msg_context
    {
        std::shared_ptr<wspp_websocket_impl> pThis;
        websocket_outgoing_message message;
    };

    HRESULT send_msg_do_work(websocket_outgoing_message& message)
    {
        HRESULT hr = S_OK;

        try
        {
            std::lock_guard<std::recursive_mutex> lock(m_wsppClientLock);

            if (m_state != State::CONNECTED)
            {
                hr = E_UNEXPECTED;
            }
            else
            {
                if (message.payload.empty())
                {
                    if (message.payloadBinary.size() > 0)
                    {
                        invoke_wspp_client([this, &message](auto& client)
                        {
                            client.send(m_con, message.payloadBinary.data(), message.payloadBinary.size(), websocketpp::frame::opcode::binary, message.error);
                        });
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
                else
                {
                    invoke_wspp_client([this, &message](auto& client)
                    {
                        client.send(m_con, message.payload.data(), message.payload.length(), websocketpp::frame::opcode::text, message.error);
                    });
                }
            }

            if (message.error.value() != 0)
            {
                hr = E_FAIL;
            }
            XAsyncComplete(message.async, hr, sizeof(WebSocketCompletionResult));

            if (--m_numSends > 0)
            {
                hr = send_msg();
            }
        }
        catch (...)
        {
            hr = E_FAIL;
            XAsyncComplete(message.async, hr, sizeof(WebSocketCompletionResult));
        }
        return hr;
    }

    // Pull the next message from the queue and send it
    HRESULT send_msg()
    {
        auto sendContext = http_allocate_shared<send_msg_context>();
        sendContext->pThis = shared_from_this();
        {
            std::lock_guard<std::recursive_mutex> lock(m_outgoingMessageQueueLock);
            ASSERT(!m_outgoingMessageQueue.empty());
            sendContext->message = std::move(m_outgoingMessageQueue.front());
            m_outgoingMessageQueue.pop();
        }

        auto rawSendContext = shared_ptr_cache::store(sendContext);
        if (rawSendContext == nullptr)
        {
            XAsyncComplete(sendContext->message.async, E_HC_NOT_INITIALISED, 0);
            return E_HC_NOT_INITIALISED;
        }

        auto hr = XAsyncBegin(sendContext->message.async, rawSendContext, (void*)HCWebSocketSendMessageAsync, __FUNCTION__,
            [](XAsyncOp op, const XAsyncProviderData* data)
        {
            auto httpSingleton = get_http_singleton();
            if (nullptr == httpSingleton)
            {
                return E_HC_NOT_INITIALISED;
            }

            WebSocketCompletionResult* result;
            switch (op)
            {
                case XAsyncOp::DoWork:
                {
                    auto context = shared_ptr_cache::fetch<send_msg_context>(data->context);
                    if (context == nullptr)
                    {
                        return E_HC_NOT_INITIALISED;
                    }
                    return context->pThis->send_msg_do_work(context->message);
                }
            
                case XAsyncOp::GetResult:
                {
                    auto context = shared_ptr_cache::fetch<send_msg_context>(data->context);
                    if (context == nullptr)
                    {
                        return E_HC_NOT_INITIALISED;
                    }
                    result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                    result->websocket = context->pThis->m_hcWebsocketHandle;
                    result->platformErrorCode = context->message.error.value();
                    result->errorCode = XAsyncGetStatus(data->async, false);
                    return S_OK;
                }

                case XAsyncOp::Cleanup:
                {
                    shared_ptr_cache::remove(data->context);
                    return S_OK;
                }

                default:
                    break;
            }

            return S_OK;
        });

        if (SUCCEEDED(hr))
        {
            hr = XAsyncSchedule(sendContext->message.async, 0);
        }
        return hr;
    }

    void send_ping()
    {
        // By design, wspp doesn't raise any sort of event when the client's connection
        // is terminated (i.e. by disconnecting the network cable). Sending periodic ping
        // allows us to detect this situation. See https://github.com/zaphoyd/websocketpp/issues/695.

        // Preserving behavior: if client did not specify a ping interval, default to WSPP_PING_INTERVAL
        const uint64_t pingDelayInMs = m_hcWebsocketHandle->websocket->PingInterval()
            ? m_hcWebsocketHandle->websocket->PingInterval() * 1000
            : WSPP_PING_INTERVAL_MS;

        RunAsync(
            [
                weakThis = std::weak_ptr<wspp_websocket_impl>{ shared_from_this() }
            ]
        {
            auto sharedThis{ weakThis.lock() };
            if (sharedThis)
            {
                try
                {
                    std::lock_guard<std::recursive_mutex> lock{ sharedThis->m_wsppClientLock };

                    if (sharedThis->m_state == CONNECTED)
                    {
                        sharedThis->invoke_wspp_client([sharedThis](auto& client)
                        {
                            client.ping(sharedThis->m_con, std::string{});
                        });

                        sharedThis->send_ping();
                    }
                }
                catch (...)
                {
                    HC_TRACE_ERROR(WEBSOCKET, "Websocket: caught exception in ping!");
                }
            }
        },
            m_backgroundQueue,
            pingDelayInMs
        );
    }

    void complete_wspp_shutdown(AsyncWork const& shutdownCompleteCallback)
    {
        {
            std::lock_guard<std::recursive_mutex> lock{ m_wsppClientLock };

            // Delete client to make sure Websocketpp cleans up all Boost.Asio portions.
            m_client.reset();
            m_state = DISCONNECTED;
        }

        shutdownCompleteCallback();
    }

    struct wspp_shutdown_completion_context
    {
        wspp_shutdown_completion_context(
            std::shared_ptr<wspp_websocket_impl> websocket,
            AsyncWork shutdownCompleteCallback,
            XTaskQueueHandle backgroundQueue) :
            websocket{ std::move(websocket) },
            shutdownCompleteCallback{ std::move(shutdownCompleteCallback) },
            backgroundQueue{ backgroundQueue }
        {
        }

        ~wspp_shutdown_completion_context()
        {
            if (backgroundQueue)
            {
                XTaskQueueCloseHandle(backgroundQueue);
            }
        }

        std::shared_ptr<wspp_websocket_impl> websocket;
        AsyncWork shutdownCompleteCallback;
        XTaskQueueHandle backgroundQueue{ nullptr };
    };

    HRESULT create_wspp_shutdown_completion_context(
        std::shared_ptr<wspp_websocket_impl> const& sharedThis,
        AsyncWork const& shutdownCompleteCallback,
        std::shared_ptr<wspp_shutdown_completion_context>& shutdownContext)
    {
        XTaskQueueHandle backgroundQueue{ nullptr };
        RETURN_IF_FAILED(XTaskQueueDuplicateHandle(sharedThis->m_backgroundQueue, &backgroundQueue));

        shutdownContext = http_allocate_shared<wspp_shutdown_completion_context>(
            sharedThis,
            shutdownCompleteCallback,
            backgroundQueue);
        if (!shutdownContext)
        {
            XTaskQueueCloseHandle(backgroundQueue);
            return E_OUTOFMEMORY;
        }

        return S_OK;
    }

    void schedule_wspp_shutdown_completion(
        std::shared_ptr<wspp_shutdown_completion_context> const& shutdownContext)
    {
        auto hr = RunAsync(
            [
                shutdownContext
            ]
            {
                shutdownContext->websocket->complete_wspp_shutdown(shutdownContext->shutdownCompleteCallback);
            },
            shutdownContext->backgroundQueue,
            0);
        if (FAILED(hr))
        {
            HC_TRACE_WARNING_HR(WEBSOCKET, hr, "Failed to queue websocketpp shutdown completion; running inline");
            shutdownContext->websocket->complete_wspp_shutdown(shutdownContext->shutdownCompleteCallback);
        }
    }

    void schedule_wspp_shutdown_completion_when_thread_exits(
        std::shared_ptr<wspp_shutdown_completion_context> const& shutdownContext)
    {
        auto hr = RunAsync(
            [
                shutdownContext
            ]
            {
                bool websocketThreadExited{ false };
                {
                    std::lock_guard<std::mutex> lock{ shutdownContext->websocket->m_websocketThreadStateMutex };
                    websocketThreadExited = shutdownContext->websocket->m_websocketThreadExited;
                }

                if (!websocketThreadExited)
                {
                    shutdownContext->websocket->schedule_wspp_shutdown_completion_when_thread_exits(shutdownContext);
                    return;
                }

                if (shutdownContext->websocket->m_websocketThread.joinable())
                {
                    shutdownContext->websocket->m_websocketThread.join();
                }

                shutdownContext->websocket->complete_wspp_shutdown(shutdownContext->shutdownCompleteCallback);
            },
            shutdownContext->backgroundQueue,
            WSPP_SHUTDOWN_POLL_INTERVAL_MS);
        if (FAILED(hr))
        {
            HC_TRACE_WARNING_HR(WEBSOCKET, hr, "Failed to queue websocketpp shutdown retry; waiting synchronously for thread completion.");
            if (shutdownContext->websocket->m_websocketThread.joinable())
            {
                shutdownContext->websocket->m_websocketThread.join();
            }

            shutdownContext->websocket->complete_wspp_shutdown(shutdownContext->shutdownCompleteCallback);
        }
    }

    template <typename WebsocketConfigType>
    void shutdown_wspp_impl(std::function<void()> shutdownCompleteCallback)
    {
        auto &client = m_client->impl<WebsocketConfigType>();
        const auto &connection = client.get_con_from_hdl(m_con);
        auto const localCloseCode = connection->get_local_close_code();
        auto const remoteCloseCode = connection->get_remote_close_code();
        m_closeCode = ResolveObservedCloseCode(localCloseCode, remoteCloseCode, connection->get_ec());
        client.stop_perpetual();

        // Yield and wait for background thread to finish
        RunAsync(
            [
                sharedThis{ shared_from_this() },
                shutdownCompleteCallback = AsyncWork{ std::move(shutdownCompleteCallback) }
            ]
        {
            auto joinWebsocketThread = [&sharedThis]()
            {
                if (sharedThis->m_websocketThread.joinable())
                {
                    sharedThis->m_websocketThread.join();
                }
            };

            // Wait for background thread to finish
            if (sharedThis->m_websocketThread.joinable())
            {
                auto waitForThreadExit = [&sharedThis](std::chrono::milliseconds timeout)
                {
                    std::unique_lock<std::mutex> lock{ sharedThis->m_websocketThreadStateMutex };
                    return sharedThis->m_websocketThreadStateCondition.wait_for(
                        lock,
                        timeout,
                        [&sharedThis]()
                        {
                            return sharedThis->m_websocketThreadExited;
                        });
                };

                if (!waitForThreadExit(std::chrono::milliseconds(WSPP_SHUTDOWN_TIMEOUT_MS)))
                {
                    HC_TRACE_WARNING(WEBSOCKET, "Warning: WSPP client thread didn't complete execution within the expected timeout. Force stopping processing loop.");
                    {
                        std::lock_guard<std::recursive_mutex> lock{ sharedThis->m_wsppClientLock };
                        if (sharedThis->m_client != nullptr)
                        {
                            sharedThis->m_client->impl<WebsocketConfigType>().stop();
                        }
                    }

                    if (!waitForThreadExit(std::chrono::milliseconds(WSPP_SHUTDOWN_TIMEOUT_MS)))
                    {
                        HC_TRACE_WARNING(WEBSOCKET, "Warning: WSPP client thread did not exit within the post-stop timeout. Completing shutdown asynchronously after the thread exits.");

                        std::shared_ptr<wspp_shutdown_completion_context> shutdownContext;
                        auto hr = sharedThis->create_wspp_shutdown_completion_context(sharedThis, shutdownCompleteCallback, shutdownContext);
                        if (FAILED(hr))
                        {
                            HC_TRACE_WARNING_HR(WEBSOCKET, hr, "Failed to capture websocketpp shutdown context. Waiting synchronously for thread completion.");
                            joinWebsocketThread();
                            sharedThis->complete_wspp_shutdown(shutdownCompleteCallback);
                            return;
                        }

                        try
                        {
                            // Intentional: the detached thread will block on join() until the
                            // ASIO thread exits, then complete shutdown asynchronously. If the
                            // ASIO thread is genuinely hung, this leaks the impl rather than
                            // deadlocking the caller — that is the intended tradeoff.
                            std::thread(
                                [
                                    shutdownContext
                                ]
                            {
                                if (shutdownContext->websocket->m_websocketThread.joinable())
                                {
                                    shutdownContext->websocket->m_websocketThread.join();
                                }

                                shutdownContext->websocket->schedule_wspp_shutdown_completion(shutdownContext);
                            }).detach();
                        }
                        catch (std::system_error const& err)
                        {
                            HC_TRACE_WARNING(WEBSOCKET, "Warning: WSPP shutdown couldn't create a joiner thread (%d). Waiting asynchronously for thread completion.", err.code().value());
                            sharedThis->schedule_wspp_shutdown_completion_when_thread_exits(shutdownContext);
                        }

                        return;
                    }
                }

                joinWebsocketThread();
            }

            sharedThis->complete_wspp_shutdown(shutdownCompleteCallback);
        },
            m_backgroundQueue,
            0
        );
    }

    template <typename WebsocketConfigType>
    inline void set_connection_error()
    {
        auto &client = m_client->impl<WebsocketConfigType>();
        const auto &connection = client.get_con_from_hdl(m_con);
        auto const connectError = connection->get_ec();
        auto const transportError = connection->get_transport_ec();

        if (transportError &&
            (connectError == websocketpp::transport::asio::error::make_error_code(websocketpp::transport::asio::error::general) ||
             connectError == websocketpp::transport::asio::error::make_error_code(websocketpp::transport::asio::error::pass_through) ||
             connectError == websocketpp::error::make_error_code(websocketpp::error::general)))
        {
            m_connectError = transportError;
            return;
        }

        m_connectError = connectError;
    }

    template <typename WebsocketConfigType>
    inline void set_connect_status()
    {
        auto& client = m_client->impl<WebsocketConfigType>();
        const auto& connection = client.get_con_from_hdl(m_con);
        m_connectStatusCode = connection->get_response_code();
    }

    template <typename WebsocketConfigType>
    inline void set_response_headers(websocketpp::connection_hdl hdl)
    {
        auto& client = m_client->impl<WebsocketConfigType>();
        websocketpp::lib::error_code ec;
        auto connection = client.get_con_from_hdl(hdl, ec);
        if (ec || !connection)
        {
            HC_TRACE_WARNING(WEBSOCKET, "Websocket [ID %llu]: failed to get websocketpp connection for response headers", TO_ULL(m_hcWebsocketHandle->websocket->id));
            return;
        }

        HttpHeaders responseHeaders;
        for (auto const& header : connection->get_response().get_headers())
        {
            http_internal_string name{ header.first.data(), header.first.size() };
            http_internal_string value{ header.second.data(), header.second.size() };
            responseHeaders[std::move(name)] = std::move(value);
        }

        HRESULT hr = m_hcWebsocketHandle->websocket->SetResponseHeaders(std::move(responseHeaders));
        if (FAILED(hr))
        {
            HC_TRACE_WARNING(WEBSOCKET, "Websocket [ID %llu]: failed to cache upgrade response headers 0x%0.8x", TO_ULL(m_hcWebsocketHandle->websocket->id), hr);
        }
    }

    // Wrappers for the different types of websocketpp clients.
    // Perform type erasure to set the websocketpp client in use at runtime
    // at connect time based on the URI and compression options.
    struct websocketpp_client_base
    {
        websocketpp_client_base() noexcept = default;
        virtual ~websocketpp_client_base() noexcept = default;

        template <typename WebsocketConfig>
        websocketpp::client<WebsocketConfig>& impl()
        {
            ASSERT(m_typeTag == typeid(websocketpp::client<WebsocketConfig>).hash_code()
                && "Config type mismatch in wspp client type erasure");
            return *reinterpret_cast<websocketpp::client<WebsocketConfig>*>(client_storage());
        }

        virtual void* client_storage() noexcept = 0;
        virtual bool is_tls_client() const = 0;
        virtual bool uses_compression() const = 0;

    protected:
        size_t m_typeTag{ 0 };
    };

    template<typename WebsocketConfig, bool IsTlsClient, bool UsesCompression>
    struct websocketpp_client_impl : websocketpp_client_base
    {
        websocketpp_client_impl()
        {
            m_typeTag = typeid(websocketpp::client<WebsocketConfig>).hash_code();
        }

        void* client_storage() noexcept override
        {
            return &m_client;
        }

        bool is_tls_client() const override { return IsTlsClient; }
        bool uses_compression() const override { return UsesCompression; }
        websocketpp::client<WebsocketConfig> m_client;
    };

    // Asio client has a long running "run" task that we need to provide a thread for
    std::thread m_websocketThread;
    std::mutex m_websocketThreadStateMutex;
    std::condition_variable m_websocketThreadStateCondition;
    bool m_websocketThreadExited{ true };
    XTaskQueueHandle m_backgroundQueue = nullptr;

    websocketpp::connection_hdl m_con;

    websocketpp::lib::error_code m_connectError{};
    websocketpp::http::status_code::value m_connectStatusCode{};
    websocketpp::close::status::value m_closeCode{};

    // Used to safe guard the wspp client.
    std::recursive_mutex m_wsppClientLock;
    std::atomic<State> m_state{ State::DISCONNECTED };
    std::unique_ptr<websocketpp_client_base> m_client;

    // Guards access to m_outgoing_msg_queue
    std::recursive_mutex m_outgoingMessageQueueLock;

    // Queue to order the sends
    http_internal_queue<websocket_outgoing_message> m_outgoingMessageQueue;

    // Number of sends in progress and queued up.
    std::atomic<int> m_numSends{ 0 };

    // Used to track if any of the OpenSSL server certificate verifications
    // failed. This can safely be tracked at the client level since connections
    // only happen once for each client.
    bool m_opensslFailed{ false };

    HCWebsocketHandle m_hcWebsocketHandle{ nullptr };

    Uri m_uri;
    http_internal_string m_subprotocol;
};

}

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

bool IsWebSocketppConnection(std::shared_ptr<hc_websocket_impl> const& connection) noexcept
{
    return std::dynamic_pointer_cast<wspp_websocket_impl>(connection) != nullptr;
}

HRESULT WebSocketppProvider::ConnectAsync(
    String const& uri,
    String const& subprotocol,
    HCWebsocketHandle websocketHandle,
    XAsyncBlock* async
) noexcept
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    RETURN_HR_IF(E_HC_NETWORK_NOT_INITIALIZED, m_isSuspended.load());
#endif

    auto wsppSocket{ std::dynamic_pointer_cast<wspp_websocket_impl>(websocketHandle->websocket->impl) };

    if (!wsppSocket)
    {
        wsppSocket = http_allocate_shared<wspp_websocket_impl>(websocketHandle, uri.data(), subprotocol.data());
        websocketHandle->websocket->impl = wsppSocket;
        TrackConnection(wsppSocket);
    }

    return wsppSocket->connect(async);
}

HRESULT WebSocketppProvider::SendAsync(
    HCWebsocketHandle websocketHandle,
    const char* message,
    XAsyncBlock* async
) noexcept
{
    std::shared_ptr<wspp_websocket_impl> wsppSocket = std::dynamic_pointer_cast<wspp_websocket_impl>(websocketHandle->websocket->impl);
    if (wsppSocket == nullptr)
    {
        return E_UNEXPECTED;
    }
    return wsppSocket->send(async, message);
}

HRESULT WebSocketppProvider::SendBinaryAsync(
    HCWebsocketHandle websocketHandle,
    const uint8_t* payloadBytes,
    uint32_t payloadSize,
    XAsyncBlock* async
) noexcept
{
    std::shared_ptr<wspp_websocket_impl> wsppSocket = std::dynamic_pointer_cast<wspp_websocket_impl>(websocketHandle->websocket->impl);
    if (wsppSocket == nullptr)
    {
        return E_UNEXPECTED;
    }
    return wsppSocket->sendBinary(async, payloadBytes, payloadSize);
}

HRESULT WebSocketppProvider::Disconnect(
    HCWebsocketHandle websocketHandle,
    HCWebSocketCloseStatus closeStatus
) noexcept
{
    if (websocketHandle == nullptr)
    {
        return E_INVALIDARG;
    }

    std::shared_ptr<wspp_websocket_impl> wsppSocket = std::dynamic_pointer_cast<wspp_websocket_impl>(websocketHandle->websocket->impl);
    if (wsppSocket == nullptr)
    {
        return E_UNEXPECTED;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", TO_ULL(websocketHandle->websocket->id));
    return wsppSocket->close(closeStatus);
}

HRESULT WebSocketppProvider::OptionsResult(HCWebSocketOptions options) const noexcept
{
#if !defined(HC_ENABLE_WEBSOCKET_COMPRESSION)
    return options == HCWebSocketOptions::None ? S_OK : E_NOT_SUPPORTED;
#else
    if (HasUnsupportedWebSocketOptions(options) || RequestsLegacyWebSocketSemantics(options))
    {
        return E_NOT_SUPPORTED;
    }

    if (options != HCWebSocketOptions::None && !RequestsWebSocketCompression(options))
    {
        return E_NOT_SUPPORTED;
    }

    return S_OK;
#endif
}

void WebSocketppProvider::OnSuspending() noexcept
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    m_isSuspended.store(true);

    std::vector<std::shared_ptr<wspp_websocket_impl>> activeConnections;
    {
        std::lock_guard<std::mutex> lock{ m_connectionsMutex };
        auto it = m_connections.begin();
        while (it != m_connections.end())
        {
            auto connection = it->lock();
            if (!connection)
            {
                it = m_connections.erase(it);
                continue;
            }

            auto wsppConnection = std::dynamic_pointer_cast<wspp_websocket_impl>(connection);
            if (wsppConnection)
            {
                activeConnections.push_back(std::move(wsppConnection));
            }

            ++it;
        }
    }

    for (auto const& connection : activeConnections)
    {
        auto hr = connection->close(HCWebSocketCloseStatus::GoingAway);
        if (FAILED(hr) && hr != E_UNEXPECTED)
        {
            HC_TRACE_WARNING_HR(WEBSOCKET, hr, "WebSocketppProvider suspend close failed");
        }
    }
#endif
}

void WebSocketppProvider::OnResuming() noexcept
{
#if HC_PLATFORM == HC_PLATFORM_GDK
    m_isSuspended.store(false);
#endif
}

void WebSocketppProvider::TrackConnection(std::shared_ptr<hc_websocket_impl> connection) noexcept
{
    std::lock_guard<std::mutex> lock{ m_connectionsMutex };
    m_connections.erase(
        std::remove_if(
            m_connections.begin(),
            m_connections.end(),
            [](std::weak_ptr<hc_websocket_impl> const& candidate)
            {
                return candidate.expired();
            }),
        m_connections.end());
    m_connections.push_back(std::move(connection));
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif // !HC_NOWEBSOCKETS
