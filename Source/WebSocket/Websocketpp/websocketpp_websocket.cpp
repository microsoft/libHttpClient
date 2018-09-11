// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "hcwebsocket.h"
#include "uri.h"
#include "x509_cert_utilities.hpp"

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
#endif

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#if defined(_WIN32)
#pragma warning( pop )
#endif

#define SUB_PROTOCOL_HEADER "Sec-WebSocket-Protocol"

using namespace xbox::httpclient;

struct websocket_outgoing_message
{
    AsyncBlock* async;
    http_internal_string payload;
    websocketpp::lib::error_code error;
    uint64_t id;
};

struct wspp_websocket_impl : public hc_websocket_impl, public std::enable_shared_from_this<wspp_websocket_impl>
{
private:
    enum State {
        CREATED,
        CONNECTING,
        CONNECTED,
        CLOSING,
        CLOSED,
        DESTROYED
    };

public:
    wspp_websocket_impl(hc_websocket_handle_t hcHandle)
        : m_backgroundAsync(nullptr),
        m_backgroundQueue(nullptr),
        m_state(CREATED),
        m_numSends(0),
        m_opensslFailed(false),
        m_uri(hcHandle->uri)
    {
        m_hcWebsocketHandle = HCWebSocketDuplicateHandle(hcHandle);
    }

    ~wspp_websocket_impl()
    {
        ASSERT(m_state < DESTROYED);

        // Now, what states could we be in?
        switch (m_state)
        {
        case DESTROYED:
            // This should be impossible
            std::abort();
        case CREATED:
        case CLOSED:
            // No work to do in these states
            break;
        case CONNECTING:
            try
            {
                // Wait for pending connection to finish
                while (m_state < CONNECTED) { std::this_thread::sleep_for(std::chrono::milliseconds{ 10 }); }
            }
            catch (...) {}
            // intentional fallthrough
        case CONNECTED:
        case CLOSING:
            try
            {
                // This will do nothing in the already-closing case
                close();
                while (m_state < CLOSED) { std::this_thread::sleep_for(std::chrono::milliseconds{ 10 }); }
            }
            catch (...) {}
            break;
        }

        // At this point, there should be no more references to me.
        m_state = DESTROYED;
        CloseAsyncQueue(m_backgroundQueue);
    }

    HRESULT connect(AsyncBlock* async)
    {
        if (m_uri.Scheme() == "wss")
        {
            m_client = std::unique_ptr<websocketpp_client_base>(new websocketpp_tls_client());

            // Options specific to TLS client.
            auto &client = m_client->client<websocketpp::config::asio_tls_client>();
            client.set_tls_init_handler([this](websocketpp::connection_hdl)
            {
                auto sslContext = websocketpp::lib::shared_ptr<asio::ssl::context>(new asio::ssl::context(asio::ssl::context::sslv23));
                sslContext->set_default_verify_paths();
                sslContext->set_options(asio::ssl::context::default_workarounds);
                sslContext->set_verify_mode(asio::ssl::context::verify_peer);


                m_opensslFailed = false;
                sslContext->set_verify_callback([this](bool preverified, asio::ssl::verify_context &verifyCtx)
                {
                    // On OS X, iOS, and Android, OpenSSL doesn't have access to where the OS
                    // stores keychains. If OpenSSL fails we will doing verification at the
                    // end using the whole certificate chain so wait until the 'leaf' cert.
                    // For now return true so OpenSSL continues down the certificate chain.
                    if (!preverified)
                    {
                        m_opensslFailed = true;
                    }
                    if (m_opensslFailed)
                    {
                        return xbox::httpclient::verify_cert_chain_platform_specific(verifyCtx, m_uri.Host());
                    }
                    asio::ssl::rfc2818_verification rfc2818(m_uri.Host().data());
                    return rfc2818(preverified, verifyCtx);
                });

                // OpenSSL stores some per thread state that never will be cleaned up until
                // the dll is unloaded. If static linking, like we do, the state isn't cleaned up
                // at all and will be reported as leaks.
                // See http://www.openssl.org/support/faq.html#PROG13
                // This is necessary here because it is called on the user's thread calling connect(...)
                // eventually through websocketpp::client::get_connection(...)
#if HC_PLATFORM == HC_PLATFORM_ANDROID
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"     
                ERR_remove_thread_state(nullptr);
#pragma clang diagnostic pop
#else 
                ERR_remove_thread_state(nullptr);
#endif // HC_ANDROID_API

                return sslContext;
            });

            // Options specific to underlying socket.
            client.set_socket_init_handler([this](websocketpp::connection_hdl, asio::ssl::stream<asio::ip::tcp::socket> &ssl_stream)
            {
                // If user specified server name is empty default to use URI host name.
                Uri uri(this->m_hcWebsocketHandle->uri);
                SSL_set_tlsext_host_name(ssl_stream.native_handle(), uri.Host().data());
            });

            return connect_impl<websocketpp::config::asio_tls_client>(async);
        }
        else
        {
            m_client = std::unique_ptr<websocketpp_client_base>(new websocketpp_client());
            return connect_impl<websocketpp::config::asio_client>(async);
        }
    }

    HRESULT send(AsyncBlock* async, const char* payloadPtr)
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

        auto httpSingleton = get_http_singleton(false);
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
            std::lock_guard<std::mutex> lock(m_outgoingMessageQueueLock);
            m_outgoingMessageQueue.push(message);
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
        return close(HCWebSocketCloseStatus::HCWebSocketCloseStatus_Normal);
    }

    HRESULT close(HCWebSocketCloseStatus status)
    {
        websocketpp::lib::error_code ec;
        {
            std::lock_guard<std::mutex> lock(m_wsppClientLock);
            if (m_state == CONNECTED)
            {
                m_state = CLOSING;
                if (m_client->is_tls_client())
                {
                    auto &client = m_client->client<websocketpp::config::asio_tls_client>();
                    client.close(m_con, static_cast<websocketpp::close::status::value>(status), std::string(), ec);
                }
                else
                {
                    auto &client = m_client->client<websocketpp::config::asio_client>();
                    client.close(m_con, static_cast<websocketpp::close::status::value>(status), std::string(), ec);
                }
            }
        }
        return S_OK;
    }

private:
    template<typename WebsocketConfigType>
    HRESULT connect_impl(AsyncBlock* async)
    {
        auto &client = m_client->client<WebsocketConfigType>();

        client.clear_access_channels(websocketpp::log::alevel::all);
        client.clear_error_channels(websocketpp::log::alevel::all);
        client.init_asio();
        client.start_perpetual();

        ASSERT(m_state == CREATED);
        client.set_open_handler([this, async](websocketpp::connection_hdl)
        {
            ASSERT(m_state == CONNECTING);
            m_state = CONNECTED;
            set_connection_error<WebsocketConfigType>();
            CompleteAsync(async, S_OK, sizeof(WebSocketCompletionResult));
        });

        client.set_fail_handler([this, async](websocketpp::connection_hdl)
        {
            ASSERT(m_state == CONNECTING);
            shutdown_wspp_impl<WebsocketConfigType>();
            set_connection_error<WebsocketConfigType>();
            CompleteAsync(async, S_OK, sizeof(WebSocketCompletionResult));
        });

        client.set_message_handler([this](websocketpp::connection_hdl, const websocketpp::config::asio_client::message_type::ptr &msg)
        {
            HCWebSocketMessageFunction messageFunc = nullptr;
            HCWebSocketGetFunctions(&messageFunc, nullptr);

            if (messageFunc != nullptr)
            {
                ASSERT(m_state >= CONNECTED && m_state < CLOSED);
                auto& payload = msg->get_raw_payload();
                messageFunc(m_hcWebsocketHandle, payload.data());
            }
        });

        client.set_close_handler([this](websocketpp::connection_hdl)
        {
            ASSERT(m_state != CLOSED);
            shutdown_wspp_impl<WebsocketConfigType>();
        });

        // Set User Agent specified by the user. This needs to happen before any connection is created
        const auto& headers = m_hcWebsocketHandle->connectHeaders;

        auto user_agent_it = headers.find(websocketpp::user_agent);
        if (user_agent_it != headers.end())
        {
            client.set_user_agent(user_agent_it->second.data());
        }

        // Get the connection handle to save for later, have to create temporary
        // because type erasure occurs with connection_hdl.
        websocketpp::lib::error_code ec;
        auto con = client.get_connection(m_hcWebsocketHandle->uri.data(), ec);
        m_con = con;
        if (ec.value() != 0)
        {
            HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp get_connection failed", m_hcWebsocketHandle->id);
            return E_FAIL;
        }

        // Add any request headers specified by the user.
        for (const auto & header : headers)
        {
            // Subprotocols are handled seperately below
            if (str_icmp(header.first, SUB_PROTOCOL_HEADER) != 0)
            {
                con->append_header(header.first.data(), header.second.data());
            }
        }

        // Add any specified subprotocols.
        if (!m_hcWebsocketHandle->subProtocol.empty())
        {
            con->add_subprotocol(m_hcWebsocketHandle->subProtocol.data(), ec);
            if (ec.value())
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: add_subprotocol failed", m_hcWebsocketHandle->id);
                return E_FAIL;
            }
        }

        // Setup proxy options.
        if (!m_hcWebsocketHandle->proxyUri.empty())
        {
            con->set_proxy(m_hcWebsocketHandle->proxyUri.data(), ec);
            if (ec)
            {
                HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy failed", m_hcWebsocketHandle->id);
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
                    HC_TRACE_ERROR(WEBSOCKET, "Websocket [ID %llu]: wspp set_proxy failed", m_hcWebsocketHandle->id);
                    return E_FAIL;
                }
            }
        }
#endif

        struct connect_context
        {
            connect_context(websocketpp::client<WebsocketConfigType>& _client) : client(_client) {}
            websocketpp::client<WebsocketConfigType>& client;
        };
        auto context = http_allocate_shared<connect_context>(client);

        m_backgroundAsync = new (xbox::httpclient::http_memory::mem_alloc(sizeof(AsyncBlock))) AsyncBlock{};
        m_backgroundQueue = DuplicateAsyncQueueHandle(async->queue);
        m_backgroundAsync->queue = m_backgroundQueue;
        m_backgroundAsync->context = shared_ptr_cache::store(context);
        m_backgroundAsync->callback = [](AsyncBlock* async)
        {
            xbox::httpclient::http_memory::mem_free(async);
        };

        // Initialize the 'connect' AsyncBlock here, but the actually work will happen on the ASIO background thread below
        auto hr = BeginAsync(async, this, (void*)HCWebSocketConnectAsync, __FUNCTION__,
            [](AsyncOp op, const AsyncProviderData* data)
        {
            if (op == AsyncOp_GetResult)
            {
                auto context = static_cast<wspp_websocket_impl*>(data->context);
                auto result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                result->platformErrorCode = context->m_connectError.value();
                result->errorCode = context->m_connectError ? E_FAIL : S_OK;
            }
            return S_OK;
        });

        if (SUCCEEDED(hr))
        {
            m_state = CONNECTING;
            client.connect(con);

            hr = RunAsync(m_backgroundAsync, [](AsyncBlock* async)
            {
                auto context = shared_ptr_cache::fetch<connect_context>(async->context);

                context->client.run();

                // OpenSSL stores some per thread state that never will be cleaned up until
                // the dll is unloaded. If static linking, like we do, the state isn't cleaned up
                // at all and will be reported as leaks.
                // See http://www.openssl.org/support/faq.html#PROG13
#if HC_PLATFORM == HC_PLATFORM_ANDROID
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                ERR_remove_thread_state(nullptr);
#pragma clang diagnostic pop
#else 
                ERR_remove_thread_state(nullptr);
#endif // HC_ANDROID_API

                return S_OK;
            });
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
            std::lock_guard<std::mutex> lock(m_wsppClientLock);
            if (m_client->is_tls_client())
            {
                auto& client = m_client->client<websocketpp::config::asio_tls_client>();
                client.send(m_con, message.payload.data(), message.payload.length(), websocketpp::frame::opcode::text, message.error);
            }
            else
            {
                auto& client = m_client->client<websocketpp::config::asio_client>();
                client.send(m_con, message.payload.data(), message.payload.length(), websocketpp::frame::opcode::text, message.error);
            }

            if (message.error.value() != 0)
            {
                hr = E_FAIL;
            }
            CompleteAsync(message.async, hr, sizeof(WebSocketCompletionResult));

            if (--m_numSends > 0)
            {
                hr = send_msg();
            }
        }
        catch (...)
        {
            hr = E_FAIL;
            CompleteAsync(message.async, hr, sizeof(WebSocketCompletionResult));
        }
        return hr;
    }

    // Pull the next message from the queue and send it
    HRESULT send_msg()
    {
        auto sendContext = http_allocate_shared<send_msg_context>();
        sendContext->pThis = shared_from_this();
        {
            std::lock_guard<std::mutex> lock(m_outgoingMessageQueueLock);
            ASSERT(!m_outgoingMessageQueue.empty());
            sendContext->message = std::move(m_outgoingMessageQueue.front());
            m_outgoingMessageQueue.pop();
        }

        auto hr = BeginAsync(sendContext->message.async, shared_ptr_cache::store(sendContext), (void*)HCWebSocketSendMessageAsync, __FUNCTION__,
            [](AsyncOp op, const AsyncProviderData* data)
        {
            WebSocketCompletionResult* result;
            auto context = shared_ptr_cache::fetch<send_msg_context>(data->context, op == AsyncOp_Cleanup);

            switch (op)
            {
            case AsyncOp_DoWork:
                return context->pThis->send_msg_do_work(context->message);
            
            case AsyncOp_GetResult:
                result = reinterpret_cast<WebSocketCompletionResult*>(data->buffer);
                result->platformErrorCode = context->message.error.value();
                result->errorCode = GetAsyncStatus(data->async, false);
                return S_OK;

            default: return S_OK;
            }
        });

        if (SUCCEEDED(hr))
        {
            hr = ScheduleAsync(sendContext->message.async, 0);
        }
        return hr;
    }

    template <typename WebsocketConfigType>
    void shutdown_wspp_impl()
    {
        auto &client = m_client->client<WebsocketConfigType>();
        const auto &connection = client.get_con_from_hdl(m_con);
        m_closeCode = connection->get_local_close_code();
        client.stop_perpetual();

        // Can't join thread directly since it is the current thread.
        AsyncBlock* async = new (xbox::httpclient::http_memory::mem_alloc(sizeof(AsyncBlock))) AsyncBlock {};
        async->queue = m_backgroundQueue;
        async->context = this;
        async->callback = [](AsyncBlock* async)
        {
            xbox::httpclient::http_memory::mem_free(async);
        };

        RunAsync(async, [](AsyncBlock* async)
        {
            auto pThis = reinterpret_cast<wspp_websocket_impl*>(async->context);

            // Wait for background thread to finish
            (void)GetAsyncStatus(pThis->m_backgroundAsync, true);

            // Delete client to make sure Websocketpp cleans up all Boost.Asio portions.
            pThis->m_client.reset();

            HCWebSocketCloseEventFunction closeFunc = nullptr;
            HCWebSocketGetFunctions(nullptr, &closeFunc);
            if (closeFunc != nullptr)
            {
                closeFunc(pThis->m_hcWebsocketHandle, static_cast<HCWebSocketCloseStatus>(pThis->m_closeCode));
            }

            // Wait to change state to closed until the very end because 'this' may be cleaned up
            // after the state reaches closed
            pThis->m_state = CLOSED;

            return S_OK;
        });
    }

    template <typename WebsocketConfigType>
    inline void set_connection_error()
    {
        auto &client = m_client->client<WebsocketConfigType>();
        const auto &connection = client.get_con_from_hdl(m_con);
        m_connectError = connection->get_ec();
    }

    // Wrappers for the different types of websocketpp clients.
    // Perform type erasure to set the websocketpp client in use at runtime
    // after construction based on the URI.
    struct websocketpp_client_base
    {
        virtual ~websocketpp_client_base() HC_NOEXCEPT {}
        template <typename WebsocketConfig>
        websocketpp::client<WebsocketConfig> & client()
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
        virtual websocketpp::client<websocketpp::config::asio_client> & non_tls_client()
        {
            throw std::bad_cast();
        }
        virtual websocketpp::client<websocketpp::config::asio_tls_client> & tls_client()
        {
            throw std::bad_cast();
        }
        virtual bool is_tls_client() const = 0;
    };

    struct websocketpp_client : websocketpp_client_base
    {
        websocketpp::client<websocketpp::config::asio_client> & non_tls_client() override
        {
            return m_client;
        }
        bool is_tls_client() const override { return false; }
        websocketpp::client<websocketpp::config::asio_client> m_client;
    };

    struct websocketpp_tls_client : websocketpp_client_base
    {
        websocketpp::client<websocketpp::config::asio_tls_client> & tls_client() override
        {
            return m_client;
        }
        bool is_tls_client() const override { return true; }
        websocketpp::client<websocketpp::config::asio_tls_client> m_client;
    };

    // Asio client has a long running "run" task that we need to provide a thread for
    AsyncBlock* m_backgroundAsync;
    async_queue_handle_t m_backgroundQueue;

    websocketpp::connection_hdl m_con;

    websocketpp::lib::error_code m_connectError;
    websocketpp::close::status::value m_closeCode;

    // Used to safe guard the wspp client.
    std::mutex m_wsppClientLock;
    std::atomic<State> m_state;
    std::unique_ptr<websocketpp_client_base> m_client;

    // Guards access to m_outgoing_msg_queue
    std::mutex m_outgoingMessageQueueLock;

    // Queue to order the sends
    http_internal_queue<websocket_outgoing_message> m_outgoingMessageQueue;

    // Number of sends in progress and queued up.
    std::atomic<int> m_numSends;

    // Used to track if any of the OpenSSL server certificate verifications
    // failed. This can safely be tracked at the client level since connections
    // only happen once for each client.
    bool m_opensslFailed;

    hc_websocket_handle_t m_hcWebsocketHandle;

    Uri m_uri;
};

HRESULT Internal_HCWebSocketConnectAsync(
    _In_z_ const char* uri,
    _In_z_ const char* subProtocol,
    _In_ hc_websocket_handle_t websocket,
    _Inout_ AsyncBlock* async
    )
{
    websocket->uri = uri;
    websocket->subProtocol = subProtocol;
    auto wsppSocket = http_allocate_shared<wspp_websocket_impl>(websocket);
    websocket->impl = wsppSocket;

    return wsppSocket->connect(async);
}

HRESULT Internal_HCWebSocketSendMessageAsync(
    _In_ hc_websocket_handle_t websocket,
    _In_z_ const char* message,
    _Inout_ AsyncBlock* async
    )
{
    std::shared_ptr<wspp_websocket_impl> wsppSocket = std::dynamic_pointer_cast<wspp_websocket_impl>(websocket->impl);
    if (wsppSocket == nullptr)
    {
        return E_UNEXPECTED;
    }
    return wsppSocket->send(async, message);
}

HRESULT Internal_HCWebSocketDisconnect(
    _In_ hc_websocket_handle_t websocket,
    _In_ HCWebSocketCloseStatus closeStatus
    )
{
    if (websocket == nullptr)
    {
        return E_INVALIDARG;
    }

    std::shared_ptr<wspp_websocket_impl> wsppSocket = std::dynamic_pointer_cast<wspp_websocket_impl>(websocket->impl);
    if (wsppSocket == nullptr)
    {
        return E_UNEXPECTED;
    }

    HC_TRACE_INFORMATION(WEBSOCKET, "Websocket [ID %llu]: disconnecting", websocket->id);
    return wsppSocket->close(closeStatus);
}
