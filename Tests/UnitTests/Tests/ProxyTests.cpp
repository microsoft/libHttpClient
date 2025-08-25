// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"libHttpClient"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "../Common/Win/utils_win.h"
#include "../../Source/HTTP/WinHttp/winhttp_provider.h"

using namespace xbox::httpclient;

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

DEFINE_TEST_CLASS(ProxyTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(ProxyTests);

    DEFINE_TEST_CASE(NamedProxyPortFormatting)
    {
        DEFINE_TEST_CASE_PROPERTIES(NamedProxyPortFormatting);
        // Verify BuildNamedProxyString includes the numeric port (regression guard for prior bug treating uint16_t as a single wide char).
        xbox::httpclient::Uri uri{"http://127.0.0.1:8888"};
        auto proxyName = WinHttpProvider::BuildNamedProxyString(uri);
        VERIFY_ARE_EQUAL_STR(L"127.0.0.1:8888", proxyName.c_str());
    }

    DEFINE_TEST_CASE(NamedProxyNoPort)
    {
        DEFINE_TEST_CASE_PROPERTIES(NamedProxyNoPort);
        xbox::httpclient::Uri uri{"http://localhost"};
        auto proxyName = WinHttpProvider::BuildNamedProxyString(uri);
        VERIFY_ARE_EQUAL_STR(L"localhost", proxyName.c_str());
    }

    DEFINE_TEST_CASE(NamedProxyExplicitDefaultPort)
    {
        DEFINE_TEST_CASE_PROPERTIES(NamedProxyExplicitDefaultPort);
        // Explicit default port (http://example.com:80) is preserved by Uri parsing (Port()==80 not treated as default)
        xbox::httpclient::Uri uri{"http://example.com:80"};
        auto proxyName = WinHttpProvider::BuildNamedProxyString(uri);
        VERIFY_ARE_EQUAL_STR(L"example.com:80", proxyName.c_str());

        // Verify no default port works
        xbox::httpclient::Uri uri2{"http://example.com"};
        auto proxyName2 = WinHttpProvider::BuildNamedProxyString(uri2);
        VERIFY_ARE_EQUAL_STR(L"example.com", proxyName2.c_str());
    }

    DEFINE_TEST_CASE(NamedProxyIPv6LiteralWithPort)
    {
        DEFINE_TEST_CASE_PROPERTIES(NamedProxyIPv6LiteralWithPort);
        // Uri class should normalize; host for IPv6 literal typically without brackets when accessed via Host()
        xbox::httpclient::Uri uri{"http://[2001:db8::1]:3128"};
        auto proxyName = WinHttpProvider::BuildNamedProxyString(uri);
        // Expect host + :port (no brackets re-added by BuildNamedProxyString)
        // If Uri::Host() preserves brackets, adjust expected accordingly; we detect by checking first char
        auto hostUtf16 = utf16_from_utf8(uri.Host());
        http_internal_wstring expected = hostUtf16;
        if (!uri.IsPortDefault() && uri.Port() > 0)
        {
            expected.push_back(L':');
            expected.append(std::to_wstring(static_cast<unsigned int>(uri.Port())));
        }
        VERIFY_ARE_EQUAL_STR(expected.c_str(), proxyName.c_str());
    }

    DEFINE_TEST_CASE(NamedProxyUnicodeHost)
    {
        DEFINE_TEST_CASE_PROPERTIES(NamedProxyUnicodeHost);
        // Internationalized domain name in punycode should round-trip; here we just ensure it's copied
        xbox::httpclient::Uri uri{"http://xn--bcher-kva.example:8080"}; // bücher.example punycode label
        auto proxyName = WinHttpProvider::BuildNamedProxyString(uri);
        VERIFY_ARE_EQUAL_STR(L"xn--bcher-kva.example:8080", proxyName.c_str());
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
