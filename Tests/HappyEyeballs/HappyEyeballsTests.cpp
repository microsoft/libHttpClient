// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#include "happy_eyeballs_connect.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

using asio::ip::tcp;
using xbox::httpclient::HappyEyeballsConnect;
using namespace std::chrono;

size_t outstandingAllocationBytes = 0;

template<typename T> struct TrackingAllocator
{
    using value_type = T;

    TrackingAllocator() = default;
    template<typename U> TrackingAllocator(TrackingAllocator<U> const&) {}

    T* allocate(size_t count)
    {
        auto allocation = std::allocator<T>{}.allocate(count);
        outstandingAllocationBytes += count * sizeof(T);
        return allocation;
    }

    void deallocate(T* allocation, size_t count)
    {
        outstandingAllocationBytes -= count * sizeof(T);
        std::allocator<T>{}.deallocate(allocation, count);
    }
};

template<typename T, typename U> bool operator==(TrackingAllocator<T> const&, TrackingAllocator<U> const&)
{
    return true;
}

template<typename T, typename U> bool operator!=(TrackingAllocator<T> const&, TrackingAllocator<U> const&)
{
    return false;
}

#define CHECK(condition)                                                                                                                   \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (!(condition))                                                                                                                  \
            throw std::runtime_error(#condition);                                                                                          \
    } while (false)

// A pending connect is a timer. Closing the socket cancels it and delivers an
// aborted completion just like Asio, including completions queued after a winner.
struct FakeSocket
{
    struct Plan
    {
        int delay;
        asio::error_code error;
    };
    struct State
    {
        asio::steady_timer timer;
        tcp::endpoint endpoint;
        bool closed{ false };
        explicit State(asio::any_io_executor executor)
            : timer(executor)
        {
        }
    };
    static std::map<unsigned short, Plan> plans;
    static std::vector<unsigned short> started;
    static std::vector<std::weak_ptr<State>> states;
    std::shared_ptr<State> state;

    explicit FakeSocket(asio::any_io_executor executor)
        : state(std::make_shared<State>(executor))
    {
        states.push_back(state);
    }
    auto get_executor()
    {
        return state->timer.get_executor();
    }
    void close(asio::error_code& ec)
    {
        state->closed = true;
        state->timer.cancel(ec);
    }
    template<class Handler> void async_connect(tcp::endpoint endpoint, Handler handler)
    {
        state->endpoint = endpoint;
        started.push_back(endpoint.port());
        auto const plan = plans.at(endpoint.port());
        state->timer.expires_after(milliseconds(plan.delay));
        state->timer.async_wait([handler, plan](asio::error_code ec) mutable { handler(ec ? ec : plan.error); });
    }
};
std::map<unsigned short, FakeSocket::Plan> FakeSocket::plans;
std::vector<unsigned short> FakeSocket::started;
std::vector<std::weak_ptr<FakeSocket::State>> FakeSocket::states;

tcp::endpoint V6(unsigned short port)
{
    return { asio::ip::address_v6::loopback(), port };
}
tcp::endpoint V4(unsigned short port)
{
    return { asio::ip::address_v4::loopback(), port };
}

void FakeTest(std::vector<tcp::endpoint> endpoints, std::vector<unsigned short> expected, unsigned short winner, int cancelAfter = -1,
    bool threaded = false)
{
    asio::io_context io;
    auto strand = std::make_shared<asio::io_service::strand>(io);
    FakeSocket::started.clear();
    FakeSocket::states.clear();
    FakeSocket target(io.get_executor());
    int completions = 0;
    asio::error_code result;
    std::shared_ptr<HappyEyeballsConnect<FakeSocket>> operation;
    asio::steady_timer cancel(io);
    auto const start = steady_clock::now();
    strand->post(
        [&]
        {
            operation = HappyEyeballsConnect<FakeSocket>::Start(target, strand, endpoints,
                [&](asio::error_code ec)
                {
                    ++completions;
                    result = ec;
                });
            if (cancelAfter >= 0)
            {
                cancel.expires_after(milliseconds(cancelAfter));
                cancel.async_wait(strand->wrap(
                    [&](asio::error_code ec)
                    {
                        if (!ec)
                        {
                            operation->Cancel();
                        }
                    }));
            }
        });
    std::thread worker;
    if (threaded)
    {
        worker = std::thread([&] { io.run(); });
    }
    io.run();
    if (worker.joinable())
    {
        worker.join();
    }
    CHECK(completions == 1);
    CHECK(FakeSocket::started == expected);
    if (winner)
    {
        CHECK(!result);
        CHECK(target.state->endpoint.port() == winner);
        CHECK(!target.state->closed);
    }
    else
    {
        CHECK(result);
        if (cancelAfter >= 0)
        {
            CHECK(result == asio::error::operation_aborted);
        }
        CHECK(target.state->closed);
    }
    for (auto const& weak : FakeSocket::states)
    {
        auto state = weak.lock();
        if (state && state != target.state)
        {
            CHECK(state->closed);
        }
    }
    // A simulated black hole has a 10s timeout; successful fallback or cancel
    // must clean it up instead of keeping the I/O loop alive until it expires.
    CHECK(steady_clock::now() - start < seconds(3));
    operation.reset();
}

void CustomAllocatorTest()
{
    using Race = HappyEyeballsConnect<FakeSocket, TrackingAllocator>;
    asio::io_context io;
    auto strand = std::make_shared<asio::io_service::strand>(io);
    FakeSocket target(io.get_executor());
    bool completed = false;
    strand->post(
        [&]
        {
            Race::Start(target, strand, { V6(1), V4(2) },
                [&](asio::error_code ec)
                {
                    CHECK(!ec);
                    CHECK(outstandingAllocationBytes > 0);
                    completed = true;
                });
        });
    io.run();
    CHECK(completed);
    CHECK(outstandingAllocationBytes == 0);
}

void RealLoopbackTest()
{
    asio::io_context io;
    tcp::acceptor listener(io, { tcp::v4(), 0 });
    auto const port = listener.local_endpoint().port();
    tcp::socket target(io);
    auto strand = std::make_shared<asio::io_service::strand>(io);
    int completions = 0;
    asio::error_code result;
    strand->post(
        [&]
        {
            HappyEyeballsConnect<>::Start(target, strand, { V6(port), V4(port) },
                [&](asio::error_code ec)
                {
                    ++completions;
                    result = ec;
                });
        });
    io.run();
    CHECK(completions == 1);
    CHECK(!result);
    CHECK(target.remote_endpoint().address().is_v4());
}

int main()
{
    try
    {
        FakeSocket::plans = { { 1, { 10000, {} } }, // IPv6 black hole
            { 2, { 0, {} } },                       // IPv4 success
            { 3, { 0, asio::error::connection_refused } }, { 4, { 0, asio::error::network_unreachable } }, { 5, { 0, {} } },
            { 6, { 260, {} } }, // primary wins after a second attempt has started
            { 7, { 10000, {} } } };
        FakeTest({ V6(1), V4(2) }, { 1, 2 }, 2);           // stalled IPv6, successful IPv4
        FakeTest({ V6(3), V4(2) }, { 3, 2 }, 2);           // refused IPv6, immediate IPv4
        FakeTest({ V6(1), V6(3), V4(2) }, { 1, 2 }, 2);    // interleave families
        FakeTest({ V4(1), V6(2) }, { 1, 2 }, 2);           // symmetric fallback
        FakeTest({ V6(2), V4(1) }, { 2 }, 2);              // IPv6 succeeds; no unnecessary IPv4
        FakeTest({ V4(3), V4(5) }, { 3, 5 }, 5);           // IPv4-only, multiple addresses
        FakeTest({ V6(4), V6(5) }, { 4, 5 }, 5);           // IPv6-only, multiple addresses
        FakeTest({ V6(3), V4(4) }, { 3, 4 }, 0);           // all attempts fail
        FakeTest({}, {}, 0);                               // empty result
        FakeTest({ V6(1), V4(7) }, { 1 }, 0, 25);          // cancel before stagger
        FakeTest({ V6(1), V4(7) }, { 1, 7 }, 0, 300);      // cancel both pending sockets
        FakeTest({ V6(6), V4(7) }, { 6, 7 }, 6);           // primary wins, close alternative
        FakeTest({ V6(1), V4(2) }, { 1, 2 }, 2, -1, true); // serialized on two I/O threads
        RealLoopbackTest();
        CustomAllocatorTest();
        std::cout << "15 Happy Eyeballs cases passed\n";
        return 0;
    }
    catch (std::exception const& ex)
    {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
