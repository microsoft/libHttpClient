// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#define _WEBSOCKETPP_CPP11_SYSTEM_ERROR_
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include "happy_eyeballs_endpoint.hpp"

// Compile the OpenSSL transport even on Windows, where the shipping provider
// uses WinTLS. In particular, OpenSSL's raw socket is a basic_socket, whereas
// get_next_layer() returns the TCP stream socket needed for winner transfer.
struct HappyEyeballsTlsConfig : websocketpp::config::asio_tls_client
{
    using transport_type = xbox::httpclient::HappyEyeballsEndpoint<transport_config>;
};

void CompileHappyEyeballsTlsEndpoint()
{
    websocketpp::client<HappyEyeballsTlsConfig> client;
    client.init_asio();
    websocketpp::lib::error_code ec;
    client.connect(client.get_connection("wss://localhost/", ec));
}
