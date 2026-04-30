/*
 * Copyright (c) Microsoft Corporation
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */

#pragma once

#ifndef WINTLS_USE_STANDALONE_ASIO
#define WINTLS_USE_STANDALONE_ASIO
#endif

#include <wintls.hpp>

#include <websocketpp/transport/asio/base.hpp>
#include <websocketpp/transport/asio/security/base.hpp>
#include <websocketpp/uri.hpp>

#include <websocketpp/common/asio.hpp>
#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/common/functional.hpp>
#include <websocketpp/common/memory.hpp>

#include <sstream>
#include <string>

namespace websocketpp {
namespace transport {
namespace asio {
namespace wintls_socket {

typedef lib::function<void(connection_hdl, wintls::stream<lib::asio::ip::tcp::socket>&)>
    socket_init_handler;

typedef lib::function<lib::shared_ptr<wintls::context>(connection_hdl)>
    tls_init_handler;

class connection : public lib::enable_shared_from_this<connection> {
public:
    typedef connection type;
    typedef lib::shared_ptr<type> ptr;
    typedef wintls::stream<lib::asio::ip::tcp::socket> socket_type;
    typedef lib::shared_ptr<socket_type> socket_ptr;
    typedef lib::asio::io_service* io_service_ptr;
    typedef lib::shared_ptr<lib::asio::io_service::strand> strand_ptr;
    typedef lib::shared_ptr<wintls::context> context_ptr;

    explicit connection() = default;

    ptr get_shared()
    {
        return shared_from_this();
    }

    bool is_secure() const
    {
        return true;
    }

    socket_type::next_layer_type& get_raw_socket()
    {
        return m_socket->next_layer();
    }

    socket_type::next_layer_type& get_next_layer()
    {
        return m_socket->next_layer();
    }

    socket_type& get_socket()
    {
        return *m_socket;
    }

    void set_socket_init_handler(socket_init_handler h)
    {
        m_socket_init_handler = h;
    }

    void set_tls_init_handler(tls_init_handler h)
    {
        m_tls_init_handler = h;
    }

    void set_certificate_revocation_check(bool check)
    {
        m_certificateRevocationCheck = check;
    }

    std::string get_remote_endpoint(lib::error_code& ec) const
    {
        std::stringstream s;

        lib::asio::error_code aec;
        lib::asio::ip::tcp::endpoint ep = m_socket->next_layer().remote_endpoint(aec);

        if (aec)
        {
            ec = socket::make_error_code(socket::error::pass_through);
            s << "Error getting remote endpoint: " << aec << " (" << aec.message() << ")";
            return s.str();
        }

        ec = lib::error_code();
        s << ep;
        return s.str();
    }

protected:
    lib::error_code init_asio(io_service_ptr service, strand_ptr strand, bool is_server)
    {
        if (!m_tls_init_handler)
        {
            return socket::make_error_code(socket::error::missing_tls_init_handler);
        }

        m_context = m_tls_init_handler(m_hdl);
        if (!m_context)
        {
            return socket::make_error_code(socket::error::invalid_tls_context);
        }

        // Uses raw new because websocketpp's transport layer manages this via
        // lib::shared_ptr (std::shared_ptr), which cannot use the project's
        // custom allocator.
        m_socket.reset(new socket_type(lib::asio::ip::tcp::socket(*service), *m_context));

        if (m_socket_init_handler)
        {
            m_socket_init_handler(m_hdl, get_socket());
        }

        m_strand = strand;
        m_is_server = is_server;

        return lib::error_code();
    }

    void set_uri(uri_ptr u)
    {
        m_uri = u;
    }

    void pre_init(socket::init_handler callback)
    {
        if (!m_is_server && m_uri)
        {
            m_socket->set_server_hostname(m_uri->get_host());
            m_socket->set_certificate_revocation_check(m_certificateRevocationCheck);
        }

        callback(lib::error_code());
    }

    void post_init(socket::init_handler callback)
    {
        m_ec = socket::make_error_code(socket::error::tls_handshake_timeout);

        if (m_strand)
        {
            m_socket->async_handshake(
                get_handshake_type(),
                m_strand->wrap(lib::bind(&type::handle_init, get_shared(), callback, lib::placeholders::_1))
            );
        }
        else
        {
            m_socket->async_handshake(
                get_handshake_type(),
                lib::bind(&type::handle_init, get_shared(), callback, lib::placeholders::_1)
            );
        }
    }

    void set_handle(connection_hdl hdl)
    {
        m_hdl = hdl;
    }

    void handle_init(socket::init_handler callback, wintls::error_code const& ec)
    {
        if (ec)
        {
            m_ec = translate_ec(ec);
        }
        else
        {
            m_ec = lib::error_code();
        }

        callback(m_ec);
    }

    lib::error_code get_ec() const
    {
        return m_ec;
    }

    lib::asio::error_code cancel_socket()
    {
        lib::asio::error_code ec;
        get_raw_socket().cancel(ec);
        return ec;
    }

    void async_shutdown(socket::shutdown_handler callback)
    {
        if (m_strand)
        {
            m_socket->async_shutdown(m_strand->wrap(callback));
        }
        else
        {
            m_socket->async_shutdown(callback);
        }
    }

public:
    // Overload resolution note: wintls::error_code is a typedef for std::error_code
    // (via WINTLS_USE_STANDALONE_ASIO), which is the same type as lib::error_code.
    // TLS handshake failures therefore resolve to the identity overload below, preserving
    // the original SECURITY_STATUS value in system_category through to HResultFromConnectError.
    // The generic template only fires for non-std::error_code types (e.g. boost error codes
    // in non-standalone configurations).
    template <typename ErrorCodeType>
    static lib::error_code translate_ec(ErrorCodeType)
    {
        return websocketpp::transport::asio::error::make_error_code(
            websocketpp::transport::asio::error::pass_through);
    }

    static lib::error_code translate_ec(lib::error_code ec)
    {
        return ec;
    }

private:
    wintls::handshake_type get_handshake_type() const
    {
        return m_is_server ? wintls::handshake_type::server : wintls::handshake_type::client;
    }

    strand_ptr m_strand;
    context_ptr m_context;
    socket_ptr m_socket;
    uri_ptr m_uri;
    bool m_is_server{ false };
    lib::error_code m_ec;
    connection_hdl m_hdl;
    socket_init_handler m_socket_init_handler;
    tls_init_handler m_tls_init_handler;
    bool m_certificateRevocationCheck{ true };
};

class endpoint {
public:
    typedef endpoint type;
    typedef connection socket_con_type;
    typedef socket_con_type::ptr socket_con_ptr;

    explicit endpoint() = default;

    bool is_secure() const
    {
        return true;
    }

    void set_socket_init_handler(socket_init_handler h)
    {
        m_socket_init_handler = h;
    }

    void set_tls_init_handler(tls_init_handler h)
    {
        m_tls_init_handler = h;
    }

    void set_certificate_revocation_check(bool check)
    {
        m_certificateRevocationCheck = check;
    }

protected:
    lib::error_code init(socket_con_ptr scon)
    {
        scon->set_socket_init_handler(m_socket_init_handler);
        scon->set_tls_init_handler(m_tls_init_handler);
        scon->set_certificate_revocation_check(m_certificateRevocationCheck);
        return lib::error_code();
    }

private:
    socket_init_handler m_socket_init_handler;
    tls_init_handler m_tls_init_handler;
    bool m_certificateRevocationCheck{ true };
};

} // namespace wintls_socket
} // namespace asio
} // namespace transport
} // namespace websocketpp
