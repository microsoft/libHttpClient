/*
 * Copyright (c) 2014, Peter Thorson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#pragma once

// Work around a bug in the vendored websocketpp disabled permessage-deflate
// header so libHttpClient does not depend on a patched submodule checkout.
#ifndef WEBSOCKETPP_EXTENSION_PERMESSAGE_DEFLATE_DISABLED_HPP
#define WEBSOCKETPP_EXTENSION_PERMESSAGE_DEFLATE_DISABLED_HPP

#include <websocketpp/common/cpp11.hpp>
#include <websocketpp/common/stdint.hpp>
#include <websocketpp/common/system_error.hpp>

#include <websocketpp/http/constants.hpp>
#include <websocketpp/extensions/extension.hpp>

#include <map>
#include <string>
#include <utility>

namespace websocketpp {
namespace extensions {
namespace permessage_deflate {

template <typename config>
class disabled {
    typedef std::pair<lib::error_code, std::string> err_str_pair;

public:
    err_str_pair negotiate(http::attribute_list const &) {
        return make_pair(
            websocketpp::extensions::error::make_error_code(websocketpp::extensions::error::disabled),
            std::string()
        );
    }

    lib::error_code init(bool) {
        return lib::error_code();
    }

    bool is_implemented() const {
        return false;
    }

    bool is_enabled() const {
        return false;
    }

    void set_server_mode(bool) {}

    void enable_server_no_context_takeover() {}

    void enable_client_no_context_takeover() {}

    std::string generate_offer() const {
        return "";
    }

    lib::error_code compress(std::string const &, std::string &) {
        return websocketpp::extensions::error::make_error_code(websocketpp::extensions::error::disabled);
    }

    lib::error_code decompress(uint8_t const *, size_t, std::string &) {
        return websocketpp::extensions::error::make_error_code(websocketpp::extensions::error::disabled);
    }
};

} // namespace permessage_deflate
} // namespace extensions
} // namespace websocketpp

#endif // WEBSOCKETPP_EXTENSION_PERMESSAGE_DEFLATE_DISABLED_HPP
