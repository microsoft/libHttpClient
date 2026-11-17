// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once

#include <asio.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

namespace xbox
{
namespace httpclient
{

// Internal TCP-only race. No TLS handshake or application bytes are sent until
// a winner has been selected. All entry points and handlers run on one strand.
// Socket is a template parameter so stalled connects can be tested without
// depending on the host's routing table or firewall.
template<class Socket = asio::ip::tcp::socket, template<class> class Allocator = std::allocator>
class HappyEyeballsConnect : public std::enable_shared_from_this<HappyEyeballsConnect<Socket, Allocator>>
{
public:
    using Endpoint = asio::ip::tcp::endpoint;
    using Endpoints = std::vector<Endpoint, Allocator<Endpoint>>;
    using Callback = std::function<void(asio::error_code)>;

    static std::shared_ptr<HappyEyeballsConnect> Start(
        Socket& target, std::shared_ptr<asio::io_service::strand> strand, Endpoints endpoints, Callback callback)
    {
        auto operation = std::allocate_shared<HappyEyeballsConnect>(
            Allocator<HappyEyeballsConnect>{}, target, std::move(strand), std::move(endpoints), std::move(callback));
        operation->StartNext();
        return operation;
    }

    void Cancel()
    {
        Finish(asio::error::operation_aborted, nullptr);
    }

    HappyEyeballsConnect(Socket& target, std::shared_ptr<asio::io_service::strand> strand, Endpoints endpoints, Callback callback)
        : m_target(target),
          m_strand(std::move(strand)),
          m_timer(target.get_executor()),
          m_callback(std::move(callback))
    {
        // Preserve the resolver's preferred family and order within each family,
        // but don't let a run of IPv6 addresses postpone the first IPv4 attempt.
        if (!endpoints.empty())
        {
            bool const firstV6 = endpoints.front().address().is_v6();
            Endpoints first, second;
            for (auto const& endpoint : endpoints)
            {
                (endpoint.address().is_v6() == firstV6 ? first : second).push_back(endpoint);
            }
            for (size_t i = 0; i < (std::max)(first.size(), second.size()); ++i)
            {
                if (i < first.size())
                {
                    m_endpoints.push_back(first[i]);
                }
                if (i < second.size())
                {
                    m_endpoints.push_back(second[i]);
                }
            }
        }
    }

private:
    void StartNext()
    {
        if (m_done)
        {
            return;
        }
        if (m_next == m_endpoints.size())
        {
            if (m_pending == 0)
            {
                Finish(m_lastError, nullptr);
            }
            return;
        }

        auto self = this->shared_from_this();
        auto const endpoint = m_endpoints[m_next++];
        Socket* socket = &m_target;
        if (m_next != 1)
        {
            m_sockets.push_back(std::allocate_shared<Socket>(Allocator<Socket>{}, m_target.get_executor()));
            socket = m_sockets.back().get();
        }
        ++m_pending;
        socket->async_connect(endpoint, m_strand->wrap(
                                            [self, socket](asio::error_code ec)
                                            {
                                                if (self->m_done)
                                                {
                                                    return;
                                                }
                                                --self->m_pending;
                                                if (!ec)
                                                {
                                                    self->Finish(ec, socket);
                                                }
                                                else if (ec == asio::error::operation_aborted)
                                                {
                                                    self->Finish(ec, nullptr);
                                                }
                                                else
                                                {
                                                    self->m_lastError = ec;
                                                    // A definitive failure needn't wait out the stagger interval.
                                                    self->StartNext();
                                                }
                                            }));

        asio::error_code ignored;
        m_timer.cancel(ignored);
        auto const generation = ++m_timerGeneration;
        if (m_next < m_endpoints.size())
        {
            m_timer.expires_after(std::chrono::milliseconds(250));
            m_timer.async_wait(m_strand->wrap(
                [self, generation](asio::error_code ec)
                {
                    if (!ec && generation == self->m_timerGeneration)
                    {
                        self->StartNext();
                    }
                }));
        }
    }

    void Finish(asio::error_code ec, Socket* winner)
    {
        if (m_done)
        {
            return;
        }
        m_done = true;
        asio::error_code ignored;
        m_timer.cancel(ignored);
        if (winner != &m_target)
        {
            m_target.close(ignored);
        }
        for (auto const& socket : m_sockets)
        {
            if (socket.get() != winner)
            {
                socket->close(ignored);
            }
        }
        if (winner && winner != &m_target)
        {
            m_target = std::move(*winner);
        }
        auto callback = std::move(m_callback);
        callback(ec);
    }

    Socket& m_target;
    std::shared_ptr<asio::io_service::strand> m_strand;
    asio::steady_timer m_timer;
    Endpoints m_endpoints;
    std::vector<std::shared_ptr<Socket>, Allocator<std::shared_ptr<Socket>>> m_sockets;
    Callback m_callback;
    size_t m_next{ 0 };
    size_t m_pending{ 0 };
    size_t m_timerGeneration{ 0 };
    asio::error_code m_lastError{ asio::error::host_not_found };
    bool m_done{ false };
};

} // namespace httpclient
} // namespace xbox
