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
        // Reproduce logic: Port should be formatted as digits, not interpreted as a single wide char.
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

    DEFINE_TEST_CASE(NamedProxyFormattingCopiedLogic)
    {
        DEFINE_TEST_CASE_PROPERTIES(NamedProxyFormattingCopiedLogic);
        // This test intentionally copies the (fixed) logic for building the proxy string to guarantee coverage
        // without needing to invoke broader WinHTTP setup.
        xbox::httpclient::Uri uri{"http://127.0.0.1:8888"};
        auto formatted = WinHttpProvider::BuildNamedProxyString(uri);
        http_internal_wstring wProxyHost = utf16_from_utf8(uri.Host());
        VERIFY_ARE_EQUAL_STR(L"127.0.0.1:8888", formatted.c_str());

        // Demonstrate what the buggy pattern would have produced (length 12 vs 14 expected characters including colon?)
        uint16_t port = uri.Port();
        http_internal_basic_stringstream<wchar_t> buggy;
        buggy.imbue(std::locale::classic());
        // Simulate original insertion that treated uint16_t as wchar_t
        buggy << wProxyHost << L":" << static_cast<wchar_t>(port);
        auto buggyStr = buggy.str();
        // Ensure buggy result differs from correct formatting (regression guard)
        VERIFY_IS_TRUE(buggyStr != formatted);
        // The buggy output should have total length host + 1 (char for ':') + 1 (single wide char) == wProxyHost.length() + 2
        VERIFY_ARE_EQUAL(wProxyHost.length() + 2, buggyStr.length());
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
