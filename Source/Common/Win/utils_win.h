// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "../../Global/mem.h"
#include "uri.h"

http_internal_string utf8_from_utf16(const http_internal_wstring& utf16);
http_internal_wstring utf16_from_utf8(const http_internal_string& utf8);

http_internal_string utf8_from_utf16(_In_z_ wchar_t const* utf16);
http_internal_wstring utf16_from_utf8(_In_z_ const char* utf8);

http_internal_string utf8_from_utf16(_In_reads_(size) wchar_t const* utf16, size_t size);
http_internal_wstring utf16_from_utf8(_In_reads_(size) const char* utf8, size_t size);

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

enum class proxy_type
{
    no_proxy,
    default_proxy,
    autodiscover_proxy,
    named_proxy,
    automatic_proxy
};

enum class proxy_protocol
{
    http,
    https,
    ftp,
    websocket
};

proxy_type get_ie_proxy_info(_In_ proxy_protocol protocol, _Inout_ xbox::httpclient::Uri& proxyUri);

NAMESPACE_XBOX_HTTP_CLIENT_END
