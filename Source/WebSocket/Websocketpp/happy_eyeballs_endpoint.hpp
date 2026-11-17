// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

#include <websocketpp/transport/asio/endpoint.hpp>
#include "happy_eyeballs_connect.hpp"

namespace xbox
{
namespace httpclient
{

// Keep websocketpp's TLS, proxy tunnel, and HTTP upgrade handling; replace only
// its sequential DNS/TCP connection step. Applies equally to ws and wss.
template<class Config, template<class> class Allocator = std::allocator>
class HappyEyeballsEndpoint : public websocketpp::transport::asio::endpoint<Config>
{
    using Base = websocketpp::transport::asio::endpoint<Config>;

public:
    class transport_con_type : public Base::transport_con_type
    {
        using ConnectionBase = typename Base::transport_con_type;

    public:
        using Base::transport_con_type::proxy_init;
        using ConnectionBase::ConnectionBase;
    };
    using transport_con_ptr = std::shared_ptr<transport_con_type>;

private:
    using Connection = transport_con_ptr;
    using Callback = websocketpp::transport::connect_handler;
    using SocketConnection = typename Base::socket_con_type;
    using Race = HappyEyeballsConnect<asio::ip::tcp::socket, Allocator>;

    struct Connect : std::enable_shared_from_this<Connect>
    {
        Connection connection;
        Callback callback;
        asio::ip::tcp::resolver resolver;
        asio::steady_timer timer;
        std::shared_ptr<Race> race;
        bool done{ false };
        size_t timerGeneration{ 0 };

        Connect(Connection con, Callback cb)
            : connection(std::move(con)),
              callback(std::move(cb)),
              resolver(connection->get_next_layer().get_executor()),
              timer(connection->get_next_layer().get_executor())
        {
        }

        void Finish(websocketpp::lib::error_code ec)
        {
            if (done)
            {
                return;
            }
            done = true;
            asio::error_code ignored;
            timer.cancel(ignored);
            resolver.cancel();
            if (race)
            {
                race->Cancel();
            }
            race.reset();
            auto cb = std::move(callback);
            cb(ec);
        }

        void Deadline(long milliseconds)
        {
            timer.expires_after(std::chrono::milliseconds(milliseconds));
            auto self = this->shared_from_this();
            auto const generation = ++timerGeneration;
            timer.async_wait(connection->get_strand()->wrap(
                [self, generation](asio::error_code ec)
                {
                    if (!ec && generation == self->timerGeneration)
                    {
                        self->Finish(websocketpp::transport::error::make_error_code(websocketpp::transport::error::timeout));
                    }
                }));
        }

        void Start(std::string const& host, std::string const& port)
        {
            Deadline(Config::timeout_dns_resolve);
            auto self = this->shared_from_this();
            resolver.async_resolve(host, port,
                connection->get_strand()->wrap(
                    [self](asio::error_code ec, asio::ip::tcp::resolver::results_type results)
                    {
                        if (self->done)
                        {
                            return;
                        }
                        if (ec)
                        {
                            self->Finish(SocketConnection::translate_ec(ec));
                            return;
                        }
                        self->Deadline(Config::timeout_connect);
                        typename Race::Endpoints endpoints;
                        for (auto const& result : results)
                        {
                            endpoints.push_back(result.endpoint());
                        }
                        // OpenSSL's raw/lowest layer is basic_socket, while its next
                        // layer is the movable TCP stream socket (as on ws and WinTLS).
                        self->race = Race::Start(self->connection->get_next_layer(), self->connection->get_strand(), std::move(endpoints),
                            [self](asio::error_code error) { self->Finish(SocketConnection::translate_ec(error)); });
                    }));
        }
    };

protected:
    void async_connect(Connection connection, websocketpp::uri_ptr uri, Callback callback)
    {
        connection->set_uri(uri);
        auto host = uri->get_host();
        auto port = uri->get_port_str();
        auto const proxy = connection->get_proxy();
        if (!proxy.empty())
        {
            websocketpp::uri proxyUri(proxy);
            if (!proxyUri.get_valid())
            {
                callback(websocketpp::transport::asio::error::make_error_code(websocketpp::transport::asio::error::proxy_invalid));
                return;
            }
            auto ec = connection->proxy_init(uri->get_authority());
            if (ec)
            {
                callback(ec);
                return;
            }
            host = proxyUri.get_host();
            port = proxyUri.get_port_str();
        }
        auto operation = std::allocate_shared<Connect>(Allocator<Connect>{}, connection, std::move(callback));
        // Start and complete on the same strand as websocketpp's connection.
        connection->get_strand()->dispatch([operation, host, port] { operation->Start(host, port); });
    }
};

} // namespace httpclient
} // namespace xbox
