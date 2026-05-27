// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <httpClient/httpClient.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

#ifndef HC_NOWEBSOCKETS

constexpr uint32_t WebSocketLegacySemanticsMask() noexcept
{
    return static_cast<uint32_t>(HCWebSocketOptions::LegacySemantics);
}

constexpr uint32_t WebSocketRequestCompressionMask() noexcept
{
    return static_cast<uint32_t>(HCWebSocketOptions::RequestCompression);
}

constexpr uint32_t WebSocketCompressionNoContextTakeoverMask() noexcept
{
    return
        static_cast<uint32_t>(HCWebSocketOptions::CompressionServerNoContextTakeover) |
        static_cast<uint32_t>(HCWebSocketOptions::CompressionClientNoContextTakeover);
}

constexpr uint32_t WebSocketSupportedOptionsMask() noexcept
{
    return WebSocketLegacySemanticsMask() | WebSocketRequestCompressionMask() | WebSocketCompressionNoContextTakeoverMask();
}

constexpr uint32_t RawWebSocketOptions(HCWebSocketOptions options) noexcept
{
    return static_cast<uint32_t>(options);
}

constexpr bool HasUnsupportedWebSocketOptions(HCWebSocketOptions options) noexcept
{
    return (RawWebSocketOptions(options) & ~WebSocketSupportedOptionsMask()) != 0;
}

constexpr bool RequestsLegacyWebSocketSemantics(HCWebSocketOptions options) noexcept
{
    return (RawWebSocketOptions(options) & WebSocketLegacySemanticsMask()) != 0;
}

constexpr bool RequestsWebSocketCompression(HCWebSocketOptions options) noexcept
{
    return (RawWebSocketOptions(options) & WebSocketRequestCompressionMask()) != 0;
}

constexpr bool HasNoContextTakeoverWebSocketOptions(HCWebSocketOptions options) noexcept
{
    return (RawWebSocketOptions(options) & WebSocketCompressionNoContextTakeoverMask()) != 0;
}

#endif // !HC_NOWEBSOCKETS

NAMESPACE_XBOX_HTTP_CLIENT_END
