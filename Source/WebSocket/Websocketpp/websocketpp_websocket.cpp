// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "websocketpp/logger/levels.hpp"
#include "websocketpp/logger/stub.hpp"

#if !HC_NOWEBSOCKETS

#include "websocketpp_websocket.h"
#include "uri.h"
#include "x509_cert_utilities.hpp"

#if HC_PLATFORM_IS_APPLE
#include "Apple/utils_apple.h"
#endif

// Force websocketpp to use C++ std::error_code instead of Boost.
#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_

#ifdef _WIN32
#pragma warning( push )
#pragma warning( disable : 4100 4127 4512 4996 4701 4267 )
#define _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CONSTEXPR_TOKEN_
#define _SCL_SECURE_NO_WARNINGS
#if (_MSC_VER >= 1900)
#define ASIO_ERROR_CATEGORY_NOEXCEPT noexcept(true)
#endif // (_MSC_VER >= 1900)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif

#pragma warning(disable: 4244) 

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#if HC_PLATFORM == HC_PLATFORM_ANDROID
#include "../HTTP/Android/android_platform_context.h"
#endif

#if defined(_WIN32)
#pragma warning( pop )
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#define SUB_PROTOCOL_HEADER "Sec-WebSocket-Protocol"
#define WSPP_PING_INTERVAL_MS 1000
#define WSPP_SHUTDOWN_TIMEOUT_MS 5000

using namespace xbox::httpclient;

namespace
{

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

template<typename base>
struct custom_config : public base
{
    /// Logging policies
    using alog_type = alevel_logger;
    using elog_type = elevel_logger;

    /// Default static error logging channels
    static const websocketpp::log::level alog_level = websocketpp::log::alevel::all;

    /// Default static access logging channels
    static const websocketpp::log::level elog_level = websocketpp::log::elevel::all;

    struct transport_config : public base::transport_config
    {
        using alog_type = alog_type;
        using elog_type = elog_type;
    };

    using transport_type = websocketpp::transport::asio::endpoint<transport_config>;
};

using ws = custom_config<websocketpp::config::asio_client>;
using wss = custom_config<websocketpp::config::asio_tls_client>;

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
        if (m_uri.Scheme() == "wss")
        {
            m_client = std::unique_ptr<websocketpp_client_base>(new websocketpp_tls_client());

            auto sharedThis{ shared_from_this() };

            // Options specific to TLS client.
            auto &client = m_client->impl<wss>();
            client.set_tls_init_handler([sharedThis](websocketpp::connection_hdl)
            {
                auto sslContext = websocketpp::lib::shared_ptr<asio::ssl::context>(new asio::ssl::context(asio::ssl::context::sslv23));
                sslContext->set_default_verify_paths();
                sslContext->set_options(asio::ssl::context::default_workarounds);
                sslContext->set_verify_mode(asio::ssl::context::verify_peer);

                sharedThis->m_opensslFailed = false;
                sslContext->set_verify_callback([sharedThis](bool preverified, asio::ssl::verify_context &verifyCtx)
                {
                    // allow to use proxies that decrypt https for debugging
                    if (sharedThis->m_hcWebsocketHandle->websocket->ProxyDecryptsHttps())
                    {
                        return true;
                    }

                    // On OS X, iOS, and Android, OpenSSL doesn't have access to where the OS
                    // stores keychains. If OpenSSL fails we will doing verification at the
                    // end using the whole certificate chain so wait until the 'leaf' cert.
                    // For now return true so OpenSSL continues down the certificate chain.
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

                // OpenSSL stores some per thread state that never will be cleaned up until
                // the dll is unloaded. If static linking, like we do, the state isn't cleaned up
                // at all and will be reported as leaks.
                // See http://www.openssl.org/support/faq.html#PROG13
                // This is necessary here because it is called on the user's thread calling connect(...)
                // eventually through websocketpp::client::get_connection(...)
#if HC_PLATFORM == HC_PLATFORM_ANDROID || HC_PLATFORM_IS_APPLE || HC_PLATFORM == HC_PLATFORM_LINUX
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                ERR_remove_thread_state(nullptr);
#pragma clang diagnostic pop
#else 
                ERR_remove_thread_state(nullptr);
#endif // HC_ANDROID_API || HC_PLATFORM_IS_APPLE || HC_PLATFORM_LINUX

                return sslContext;
            });

            // Options specific to underlying socket.
            client.set_socket_init_handler([sharedThis](websocketpp::connection_hdl, asio::ssl::stream<asio::ip::tcp::socket> &ssl_stream)
            {
                // If user specified server name is empty default to use URI host name.
                SSL_set_tlsext_host_name(ssl_stream.native_handle(), sharedThis->m_uri.Host().data());
            });

            return connect_impl<wss>(async);
        }
        else
        {
            m_client = std::unique_ptr<websocketpp_client_base>(new websocketpp_client());
            return connect_impl<ws>(async);
        }
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
            if (m_client->is_tls_client())
            {
                auto &client = m_client->impl<wss>();
                client.close(m_con, static_cast<websocketpp::close::status::value>(status), std::string(), ec);
            }
            else
            {
                auto &client = m_client->impl<ws>();
                client.close(m_con, static_cast<websocketpp::close::status::value>(status), std::string(), ec);
            }

            return ec ? E_FAIL : S_OK;
        }
        else
        {
            return E_UNEXPECTED;
        }
    }

private:
    template<typename WebsocketConfigType>
    HRESULT connect_impl(XAsyncBlock* async)
    {
        if (async->queue)
        {
            XTaskQueuePortHandle worker{ nullptr };
            RETURN_IF_FAILED(XTaskQueueGetPort(async->queue, XTaskQueuePort::Work, &worker));
            RETURN_IF_FAILED(XTaskQueueCreateComposite(worker, worker, &m_backgroundQueue));
        }

        auto &client = m_client->impl<WebsocketConfigType>();

        client.init_asio();
        client.start_perpetual();

        auto sharedThis { shared_from_this() };

        ASSERT(m_state == DISCONNECTED);
        client.set_open_handler([sharedThis, async](websocketpp::connection_hdl)
        {
            ASSERT(sharedThis->m_state == CONNECTING);
            sharedThis->m_state = CONNECTED;
            sharedThis->set_connection_error<WebsocketConfigType>();
            sharedThis->send_ping();
            XAsyncComplete(async, S_OK, sizeof(WebSocketCompletionResult));
        });

        client.set_fail_handler([sharedThis, async](websocketpp::connection_hdl)
        {
            ASSERT(sharedThis->m_state == CONNECTING);
            sharedThis->set_connection_error<WebsocketConfigType>();
            sharedThis->shutdown_wspp_impl<WebsocketConfigType>(
                [
                    sharedThis,
                    async
                ]
            {
                XAsyncComplete(async, S_OK, sizeof(WebSocketCompletionResult));
            });
        });

        client.set_message_handler([sharedThis](websocketpp::connection_hdl, const ws::message_type::ptr &msg)
        {
            HCWebSocketMessageFunction messageFunc{ nullptr };
            HCWebSocketBinaryMessageFunction binaryMessageFunc{ nullptr };
            void* context{ nullptr };
            auto hr = HCWebSocketGetEventFunctions(sharedThis->m_hcWebsocketHandle, &messageFunc, &binaryMessageFunc, nullptr, &context);

            if (SUCCEEDED(hr))
            {
                ASSERT(messageFunc && binaryMessageFunc);

                // TODO: hook up HCWebSocketCloseEventFunction handler upon unexpected disconnect 
                // TODO: verify auto disconnect when closing client's websocket handle

                if (msg->get_opcode() == websocketpp::frame::opcode::text)
                {
                    auto& payload = msg->get_raw_payload();
                    messageFunc(sharedThis->m_hcWebsocketHandle, payload.data(), context);
                }
                else if (msg->get_opcode() == websocketpp::frame::opcode::binary)
                {
                    auto& payload = msg->get_raw_payload();
                    binaryMessageFunc(sharedThis->m_hcWebsocketHandle, (uint8_t*)payload.data(), (uint32_t)payload.size(), context);
                }
            }
        });

        client.set_close_handler([sharedThis](websocketpp::connection_hdl)
        {
            ASSERT(sharedThis->m_state == CONNECTED || sharedThis->m_state == DISCONNECTING);
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
            con->set_proxy(m_hcWebsocketHandle->websocket->ProxyUri().data(), ec);
            if (ec)
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy failed", TO_ULL(m_hcWebsocketHandle->websocket->id));
                return E_FAIL;
            }
        }
#if HC_PLATFORM_IS_MICROSOFT
        else
        {
            // On windows platforms use the IE proxy if the user didn't specify one
            Uri proxyUri;
            auto proxyType = get_ie_proxy_info(proxy_protocol::websocket, proxyUri);

            if (proxyType == proxy_type::named_proxy)
            {
                con->set_proxy(proxyUri.FullPath().data(), ec);
                if (ec)
                {
                    HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy failed", TO_ULL(m_hcWebsocketHandle->websocket->id));
                    return E_FAIL;
                }
            }
        }
#elif HC_PLATFORM_IS_APPLE
    else
    {
        Uri proxyUri;
        std::string username;
        std::string password;
        if (getSystemProxyForUri(m_uri, &proxyUri, &username, &password))
        {
            con->set_proxy(proxyUri.FullPath().data(), ec);
            if (ec)
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy failed", TO_ULL(m_hcWebsocketHandle->websocket->id));
                return E_FAIL;
            }

            if (!username.empty() && !password.empty())
            {
                con->set_proxy_basic_auth(username, password, ec);
                if (ec)
                {
                    HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy_basic_auth failed", TO_ULL(m_hcWebsocketHandle->websocket->id));
                    return E_FAIL;
                }
            }
        }
    }
#endif
        // Initialize the 'connect' XAsyncBlock here, but the actually work will happen on the ASIO background thread below
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
                result->platformErrorCode = context->m_connectError.value();
                result->errorCode = context->m_connectError ? E_FAIL : S_OK;
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
            client.connect(con);

            try
            {
                struct client_context
                {
                    client_context(websocketpp::client<WebsocketConfigType>& _client) : client(_client) {}
                    websocketpp::client<WebsocketConfigType>& client;
                };
                auto context = http_allocate_shared<client_context>(client);

                m_websocketThread = std::thread([context, id{ m_hcWebsocketHandle->websocket->id }]()
                {
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
#if HC_PLATFORM == HC_PLATFORM_ANDROID || HC_PLATFORM_IS_APPLE || HC_PLATFORM == HC_PLATFORM_LINUX
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                    ERR_remove_thread_state(nullptr);
#pragma clang diagnostic pop
#if HC_PLATFORM == HC_PLATFORM_ANDROID
                    javaVm->DetachCurrentThread();
#endif // HC_PLATFORM_ANDROID
#else
                    ERR_remove_thread_state(nullptr);
#endif // HC_PLATFORM_ANDROID || HC_PLATFORM_IS_APPLE || HC_PLATFORM_LINUX

                    HC_TRACE_INFORMATION(WEBSOCKET, "id=%u Wspp client work thread end", id);
                });
                hr = S_OK;
            }
            catch (std::system_error err)
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket: couldn't create background websocket thread (%d)", err.code().value());
                hr = E_FAIL;
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
                        if (m_client->is_tls_client())
                        {
                            m_client->impl<wss>().send(m_con, message.payloadBinary.data(), message.payloadBinary.size(), websocketpp::frame::opcode::binary, message.error);
                        }
                        else
                        {
                            m_client->impl<ws>().send(m_con, message.payloadBinary.data(), message.payloadBinary.size(), websocketpp::frame::opcode::binary, message.error);
                        }
                    }
                    else
                    {
                        hr = E_FAIL;
                    }
                }
                else
                {
                    if (m_client->is_tls_client())
                    {
                        m_client->impl<wss>().send(m_con, message.payload.data(), message.payload.length(), websocketpp::frame::opcode::text, message.error);
                    }
                    else
                    {
                        m_client->impl<ws>().send(m_con, message.payload.data(), message.payload.length(), websocketpp::frame::opcode::text, message.error);
                    }
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
                        if (sharedThis->m_client->is_tls_client())
                        {
                            sharedThis->m_client->impl<wss>().ping(sharedThis->m_con, std::string{});
                        }
                        else
                        {
                            sharedThis->m_client->impl<ws>().ping(sharedThis->m_con, std::string{});
                        }

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
            WSPP_PING_INTERVAL_MS
        );
    }

    template <typename WebsocketConfigType>
    void shutdown_wspp_impl(std::function<void()> shutdownCompleteCallback)
    {
        auto &client = m_client->impl<WebsocketConfigType>();
        const auto &connection = client.get_con_from_hdl(m_con);
        m_closeCode = connection->get_local_close_code();
        client.stop_perpetual();

        // Yield and wait for background thread to finish
        RunAsync(
            [
                sharedThis{ shared_from_this() },
                shutdownCompleteCallback
            ]
        {
            // Wait for background thread to finish
            if (sharedThis->m_websocketThread.joinable())
            {
                auto future = std::async(std::launch::async, &std::thread::join, &sharedThis->m_websocketThread);
                if (future.wait_for(std::chrono::milliseconds(WSPP_SHUTDOWN_TIMEOUT_MS)) == std::future_status::timeout)
                {
                    HC_TRACE_WARNING(WEBSOCKET, "Warning: WSPP client thread didn't complete execution within the expected timeout. Force stopping processing loop.");
                    sharedThis->m_client->impl<WebsocketConfigType>().stop();
                }
            }

            {
                std::lock_guard<std::recursive_mutex> lock{ sharedThis->m_wsppClientLock };

                // Delete client to make sure Websocketpp cleans up all Boost.Asio portions.
                sharedThis->m_client.reset();
                sharedThis->m_state = DISCONNECTED;
            }

            shutdownCompleteCallback();
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
        m_connectError = connection->get_ec();
    }

    // Wrappers for the different types of websocketpp clients.
    // Perform type erasure to set the websocketpp client in use at runtime
    // after construction based on the URI.
    struct websocketpp_client_base
    {
        websocketpp_client_base() noexcept = default;
        virtual ~websocketpp_client_base() noexcept = default;

        template <typename WebsocketConfig>
        websocketpp::client<WebsocketConfig> & impl()
        {
            if (is_tls_client())
            {
                return reinterpret_cast<websocketpp::client<WebsocketConfig> &>(tls_client());
            }
            else
            {
                return reinterpret_cast<websocketpp::client<WebsocketConfig> &>(non_tls_client());
            }
        }

        virtual websocketpp::client<ws> & non_tls_client()
        {
            throw std::bad_cast();
        }
        virtual websocketpp::client<wss> & tls_client()
        {
            throw std::bad_cast();
        }
        virtual bool is_tls_client() const = 0;
    };

    struct websocketpp_client : websocketpp_client_base
    {
        websocketpp::client<ws> & non_tls_client() override
        {
            return m_client;
        }
        bool is_tls_client() const override { return false; }
        websocketpp::client<ws> m_client;
    };

    struct websocketpp_tls_client : websocketpp_client_base
    {
        websocketpp::client<wss> & tls_client() override
        {
            return m_client;
        }
        bool is_tls_client() const override { return true; }
        websocketpp::client<wss> m_client;
    };

    // Asio client has a long running "run" task that we need to provide a thread for
    std::thread m_websocketThread;
    XTaskQueueHandle m_backgroundQueue = nullptr;

    websocketpp::connection_hdl m_con;

    websocketpp::lib::error_code m_connectError{};
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

HRESULT WebSocketppProvider::ConnectAsync(
    String const& uri,
    String const& subprotocol,
    HCWebsocketHandle websocketHandle,
    XAsyncBlock* async
) noexcept
{
    auto wsppSocket{ std::dynamic_pointer_cast<wspp_websocket_impl>(websocketHandle->websocket->impl) };

    if (!wsppSocket)
    {
        wsppSocket = http_allocate_shared<wspp_websocket_impl>(websocketHandle, uri.data(), subprotocol.data());
        websocketHandle->websocket->impl = wsppSocket;
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

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif // !HC_NOWEBSOCKETS
