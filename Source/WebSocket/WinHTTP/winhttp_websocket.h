#pragma once

#include "WebSocket/hcwebsocket.h"
#include "HTTP/WinHttp/winhttp_connection.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

struct WinHttpWebSocket : public hc_websocket_impl, public std::enable_shared_from_this<WinHttpWebSocket>
{
    WinHttpWebSocket(std::shared_ptr<WinHttpConnection> connection) : winHttpConnection{ std::move(connection) } {}
    virtual ~WinHttpWebSocket() = default;

    std::shared_ptr<WinHttpConnection> const winHttpConnection;
};

NAMESPACE_XBOX_HTTP_CLIENT_END
