#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_

#pragma warning( push )
#pragma warning( disable : 4100 4127 4512 4996 4701 4267 )
#define _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CONSTEXPR_TOKEN_
#define _SCL_SECURE_NO_WARNINGS
#if (_MSC_VER >= 1900)
#define ASIO_ERROR_CATEGORY_NOEXCEPT noexcept(true)
#endif // (_MSC_VER >= 1900)

#include <cstdint>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/extensions/permessage_deflate/enabled.hpp>
#include <websocketpp/server.hpp>
#include <iostream>

struct echo_server_config : public websocketpp::config::asio
{
    typedef echo_server_config type;
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

typedef websocketpp::server<echo_server_config> server;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

// pull out the type of messages sent by our config
typedef server::message_ptr message_ptr;

// Define a callback to handle incoming messages
void on_message(server* s, websocketpp::connection_hdl hdl, message_ptr msg)
{
    std::cout << "on_message called with hdl: " << hdl.lock().get()
        << " and message: " << msg->get_payload()
        << std::endl;

    // check for a special command to instruct the server to stop listening so
    // it can be cleanly exited.
    if (msg->get_payload() == "stop-listening")
    {
        s->stop_listening();
        return;
    }

    try 
    {
        s->send(hdl, msg->get_payload(), msg->get_opcode());
    }
    catch (websocketpp::exception const& e)
    {
        std::cout << "Echo failed because: "
            << "(" << e.what() << ")" << std::endl;
    }
}

void on_open(server* s, websocketpp::connection_hdl hdl)
{
    try
    {
        auto connection = s->get_con_from_hdl(hdl);
        auto const& requestedExtensions = connection->get_request_header("Sec-WebSocket-Extensions");
        auto const& negotiatedExtensions = connection->get_response_header("Sec-WebSocket-Extensions");

        std::cout << "client connected; requested extensions: "
            << (requestedExtensions.empty() ? "<none>" : requestedExtensions)
            << "; negotiated extensions: "
            << (negotiatedExtensions.empty() ? "<none>" : negotiatedExtensions)
            << std::endl;
    }
    catch (websocketpp::exception const& e)
    {
        std::cout << "Failed to inspect negotiated extensions: (" << e.what() << ")" << std::endl;
    }
}

int main()
{
    // Create a server endpoint
    server echo_server;

    try 
    {
        // Set logging settings
        echo_server.set_access_channels(websocketpp::log::alevel::all);
        echo_server.clear_access_channels(websocketpp::log::alevel::frame_payload);

        // Initialize Asio
        echo_server.init_asio();

        // Register our message handler
        echo_server.set_message_handler(bind(&on_message, &echo_server, ::_1, ::_2));
        echo_server.set_open_handler(bind(&on_open, &echo_server, ::_1));

        // Listen on port 9002
        echo_server.listen(9002);

        // Start the server accept loop
        echo_server.start_accept();

        std::cout << "Listening on ws://127.0.0.1:9002 with permessage-deflate negotiation enabled." << std::endl;

        // Start the ASIO io_service run loop
        echo_server.run();
    }
    catch (websocketpp::exception const& e)
    {
        std::cout << e.what() << std::endl;
    }
    catch (...)
    {
        std::cout << "other exception" << std::endl;
    }
}
