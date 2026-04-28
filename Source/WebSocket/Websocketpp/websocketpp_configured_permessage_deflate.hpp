// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include <websocketpp/http/constants.hpp>
#if defined(__clang__)
// Keep this third-party warning suppression local to the wrapper instead of patching the submodule.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <websocketpp/extensions/permessage_deflate/enabled.hpp>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace xbox {
namespace httpclient {

// Wrap websocketpp's vendored permessage-deflate extension so libHttpClient can
// choose the outbound offer parameters without modifying the submodule copy.
template<typename config, bool RequestServerNoContextTakeover, bool RequestClientNoContextTakeover>
class configured_permessage_deflate :
    public websocketpp::extensions::permessage_deflate::enabled<config>
{
public:
    using base = websocketpp::extensions::permessage_deflate::enabled<config>;

    std::string generate_offer() const
    {
        if (RequestClientNoContextTakeover)
        {
            // Preserve websocketpp's tracking that the client offered
            // client_no_context_takeover so init(false) selects Z_FULL_FLUSH.
            (void)base::generate_offer();
        }

        std::string offer = "permessage-deflate";

        if (RequestServerNoContextTakeover)
        {
            offer += "; server_no_context_takeover";
        }

        if (RequestClientNoContextTakeover)
        {
            offer += "; client_no_context_takeover";
        }

        offer += "; client_max_window_bits";
        return offer;
    }
};

} // namespace httpclient
} // namespace xbox
