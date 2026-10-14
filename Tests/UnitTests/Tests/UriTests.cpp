// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "UnitTestIncludes.h"
#define TEST_CLASS_OWNER L"libHttpClient"
#include "DefineTestMacros.h"
#include "Utils.h"
#include "uri.h"
#include "HTTP/httpcall.h"
#include <httpClient/httpProvider.h>

#include <string>

#pragma warning(disable:4389)

NAMESPACE_XBOX_HTTP_CLIENT_TEST_BEGIN

namespace
{

struct UriCase
{
    char const* input;
    char const* scheme;
    char const* userInfo;
    char const* host;
    uint16_t port;
    char const* path;
    char const* query;
    char const* fragment;
    bool isSecure;
    bool isPortDefault;
    char const* authority;
    char const* resource;
    char const* toString;
};

// Valid inputs and how they parse. Scheme and host are lowercased; the path, query and fragment are kept
// verbatim (no percent-decoding); an empty path becomes "/"; a port is "default" only when none is given.
UriCase const c_validUris[] =
{
    // input                                         scheme   userinfo     host               port   path        query       fragment secure  dflt   authority                    resource              toString
    { "http://example.com",                          "http",  "",          "example.com",     0,     "/",        "",         "",      false,  true,  "example.com",               "/",                  "http://example.com/" },
    { "https://Example.COM:8443/a/b?x=1&y=2#frag",   "https", "",          "example.com",     8443,  "/a/b",     "x=1&y=2",  "frag",  true,   false, "example.com:8443",          "/a/b?x=1&y=2#frag",  "https://example.com:8443/a/b?x=1&y=2#frag" },
    { "HTTP://example.com/",                         "http",  "",          "example.com",     0,     "/",        "",         "",      false,  true,  "example.com",               "/",                  "http://example.com/" },
    { "wss://chat.example.com/socket",               "wss",   "",          "chat.example.com",0,     "/socket",  "",         "",      true,   true,  "chat.example.com",          "/socket",            "wss://chat.example.com/socket" },
    { "ws://chat.example.com:80",                    "ws",    "",          "chat.example.com",80,    "/",        "",         "",      false,  false, "chat.example.com:80",       "/",                  "ws://chat.example.com:80/" },
    { "http://example.com:80/",                      "http",  "",          "example.com",     80,    "/",        "",         "",      false,  false, "example.com:80",            "/",                  "http://example.com:80/" },
    { "https://example.com:443",                     "https", "",          "example.com",     443,   "/",        "",         "",      true,   false, "example.com:443",           "/",                  "https://example.com:443/" },
    { "http://user:p%40ss@host.example:8080/p",      "http",  "user:p%40ss","host.example",   8080,  "/p",       "",         "",      false,  false, "user:p%40ss@host.example:8080", "/p",             "http://user:p%40ss@host.example:8080/p" },
    { "ftp://anonymous@files.example",               "ftp",   "anonymous", "files.example",   0,     "/",        "",         "",      false,  true,  "anonymous@files.example",   "/",                  "ftp://anonymous@files.example/" },
    { "http://[::1]:8080/path",                      "http",  "",          "::1",             8080,  "/path",    "",         "",      false,  false, "[::1]:8080",                "/path",              "http://[::1]:8080/path" },
    { "https://[FE80::1:2]/",                        "https", "",          "fe80::1:2",       0,     "/",        "",         "",      true,   true,  "[fe80::1:2]",               "/",                  "https://[fe80::1:2]/" },
    { "http://127.0.0.1:65535",                      "http",  "",          "127.0.0.1",       65535, "/",        "",         "",      false,  false, "127.0.0.1:65535",           "/",                  "http://127.0.0.1:65535/" },
    { "http://example.com:0",                        "http",  "",          "example.com",     0,     "/",        "",         "",      false,  true,  "example.com",               "/",                  "http://example.com/" },
    { "http://example.com:/path",                    "http",  "",          "example.com",     0,     "/path",    "",         "",      false,  true,  "example.com",               "/path",              "http://example.com/path" },
    { "http://example.com:0080/",                    "http",  "",          "example.com",     80,    "/",        "",         "",      false,  false, "example.com:80",            "/",                  "http://example.com:80/" },
    { "http://example.com:010/",                     "http",  "",          "example.com",     10,    "/",        "",         "",      false,  false, "example.com:10",            "/",                  "http://example.com:10/" },
    { "http://example.com:08080/",                   "http",  "",          "example.com",     8080,  "/",        "",         "",      false,  false, "example.com:8080",          "/",                  "http://example.com:8080/" },
    { "http://example.com?q=1",                      "http",  "",          "example.com",     0,     "/",        "q=1",      "",      false,  true,  "example.com",               "/?q=1",              "http://example.com/?q=1" },
    { "http://example.com#top",                      "http",  "",          "example.com",     0,     "/",        "",         "top",   false,  true,  "example.com",               "/#top",              "http://example.com/#top" },
    { "http://example.com/?",                        "http",  "",          "example.com",     0,     "/",        "",         "",      false,  true,  "example.com",               "/",                  "http://example.com/" },
    { "http://example.com/a%20b/c%2Fd",              "http",  "",          "example.com",     0,     "/a%20b/c%2Fd", "",     "",      false,  true,  "example.com",               "/a%20b/c%2Fd",       "http://example.com/a%20b/c%2Fd" },
    { "http://example.com/p?a=%20&b=c?d#f/g?h",      "http",  "",          "example.com",     0,     "/p",       "a=%20&b=c?d", "f/g?h", false, true, "example.com",              "/p?a=%20&b=c?d#f/g?h", "http://example.com/p?a=%20&b=c?d#f/g?h" },
    { "custom+scheme.v1-x://host",                   "custom+scheme.v1-x", "", "host",        0,     "/",        "",         "",      false,  true,  "host",                      "/",                  "custom+scheme.v1-x://host/" },
    { "http://sub_domain.ex-ample.com/~user/a:b@c",  "http",  "",          "sub_domain.ex-ample.com", 0, "/~user/a:b@c", "", "",      false,  true,  "sub_domain.ex-ample.com",   "/~user/a:b@c",       "http://sub_domain.ex-ample.com/~user/a:b@c" },
};

char const* const c_invalidUris[] =
{
    "",                              // no scheme
    "example.com",                   // no ':' after scheme
    "1http://example.com",           // scheme must start with a letter
    "ht tp://example.com",           // invalid scheme character
    "http:/example.com",             // authority must start with "//"
    "http:example.com",              // no authority
    "http://",                       // no host
    "http:///path",                  // empty host
    "http://exa mple.com/",          // invalid host character
    "http://example.com:65536/",     // port out of range
    "http://example.com:99999999999999999999999/", // port overflow
    "http://example.com:12ab/",      // non-numeric port
    "http://example.com:-1/",        // negative port
    "http://[::1/",                  // unterminated IPv6 literal
    "http://[v1.fe80::1]/",          // IPvFuture not supported
    "http://[::g]/",                 // invalid IPv6 character
    "http://example.com/a b",        // space in path
    "http://example.com/a\"b",       // quote in path
    "http://example.com/?a b",       // space in query
    "http://example.com/#a b",       // space in fragment
    "http://example.com/#a#b",       // second '#'
    "http://example.com/\xC3\xA9",   // non-ASCII must be percent-encoded
};

xbox::httpclient::Uri ParseUri(char const* input)
{
    return xbox::httpclient::Uri{ xbox::httpclient::String{ input } };
}

void VerifyQuery(xbox::httpclient::Map<xbox::httpclient::String, xbox::httpclient::String> const& actual, char const* key, char const* expectedValue)
{
    auto it = actual.find(key);
    VERIFY_IS_TRUE(it != actual.end());
    if (it != actual.end())
    {
        VERIFY_ARE_EQUAL_STR(expectedValue, it->second.c_str());
    }
}

} // namespace

DEFINE_TEST_CLASS(UriTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(UriTests);

    DEFINE_TEST_CASE(TestValidUris)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestValidUris);

        for (auto const& c : c_validUris)
        {
            auto uri = ParseUri(c.input);
            VERIFY_ARE_EQUAL_STR(c.input, uri.IsValid() ? c.input : "<invalid>"); // names the failing input
            VERIFY_IS_FALSE(uri.IsEmpty());
            VERIFY_ARE_EQUAL_STR(c.input, uri.FullPath().c_str());
            VERIFY_ARE_EQUAL_STR(c.scheme, uri.Scheme().c_str());
            VERIFY_ARE_EQUAL_STR(c.userInfo, uri.UserInfo().c_str());
            VERIFY_ARE_EQUAL_STR(c.host, uri.Host().c_str());
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(c.port), static_cast<uint32_t>(uri.Port()));
            VERIFY_ARE_EQUAL_STR(c.path, uri.Path().c_str());
            VERIFY_ARE_EQUAL_STR(c.query, uri.Query().c_str());
            VERIFY_ARE_EQUAL_STR(c.fragment, uri.Fragment().c_str());
            VERIFY_ARE_EQUAL(c.isSecure, uri.IsSecure());
            VERIFY_ARE_EQUAL(c.isPortDefault, uri.IsPortDefault());
            VERIFY_ARE_EQUAL_STR(c.authority, uri.Authority().c_str());
            VERIFY_ARE_EQUAL_STR(c.resource, uri.Resource().c_str());
            VERIFY_ARE_EQUAL_STR(c.toString, uri.ToString().c_str());

            // The canonical form re-parses to the same components.
            auto reparsed = ParseUri(uri.ToString().c_str());
            VERIFY_IS_TRUE(reparsed.IsValid());
            VERIFY_ARE_EQUAL_STR(c.host, reparsed.Host().c_str());
            VERIFY_ARE_EQUAL(static_cast<uint32_t>(c.port), static_cast<uint32_t>(reparsed.Port()));
            VERIFY_ARE_EQUAL_STR(c.toString, reparsed.ToString().c_str());
        }
    }

    DEFINE_TEST_CASE(TestInvalidUris)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestInvalidUris);

        for (auto input : c_invalidUris)
        {
            auto uri = ParseUri(input);
            VERIFY_ARE_EQUAL_STR("<invalid>", uri.IsValid() ? input : "<invalid>"); // names the failing input
        }

        xbox::httpclient::Uri defaultConstructed;
        VERIFY_IS_FALSE(defaultConstructed.IsValid());
        VERIFY_IS_TRUE(defaultConstructed.IsEmpty());
        VERIFY_IS_FALSE(defaultConstructed.IsPortDefault());
    }

    DEFINE_TEST_CASE(TestSecureSchemes)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSecureSchemes);

        VERIFY_IS_TRUE(ParseUri("https://h").IsSecure());
        VERIFY_IS_TRUE(ParseUri("HTTPS://h").IsSecure());
        VERIFY_IS_TRUE(ParseUri("wss://h").IsSecure());
        VERIFY_IS_TRUE(ParseUri("WsS://h").IsSecure());
        VERIFY_IS_FALSE(ParseUri("http://h").IsSecure());
        VERIFY_IS_FALSE(ParseUri("ws://h").IsSecure());
        VERIFY_IS_FALSE(ParseUri("httpss://h").IsSecure());
        VERIFY_IS_FALSE(ParseUri("ftps://h").IsSecure());
    }

    DEFINE_TEST_CASE(TestParseQueryDecodes)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestParseQueryDecodes);
        using xbox::httpclient::Uri;

        auto query = Uri::ParseQuery("a=1&b=hello%20world&c=x+y&d=&%41%62=%2f%2F&e=caf%C3%A9");
        VERIFY_ARE_EQUAL(6u, static_cast<uint32_t>(query.size()));
        VerifyQuery(query, "a", "1");
        VerifyQuery(query, "b", "hello world");
        VerifyQuery(query, "c", "x y");
        VerifyQuery(query, "d", "");
        VerifyQuery(query, "Ab", "//");
        VerifyQuery(query, "e", "caf\xC3\xA9");

        // A keyless value is stored under the empty key; empty segments and a trailing '&' are ignored.
        auto keyless = Uri::ParseQuery("flag&&x=1&");
        VERIFY_ARE_EQUAL(2u, static_cast<uint32_t>(keyless.size()));
        VerifyQuery(keyless, "", "flag");
        VerifyQuery(keyless, "x", "1");

        // A value containing '=' keeps everything after the first '='; the last duplicate key wins.
        auto equals = Uri::ParseQuery("k=a=b&k2=1&k2=2");
        VerifyQuery(equals, "k", "a=b");
        VerifyQuery(equals, "k2", "2");

        // Malformed percent-escapes decode to an empty string.
        auto malformed = Uri::ParseQuery("bad=%zz&short=%2&ok=%21");
        VerifyQuery(malformed, "bad", "");
        VerifyQuery(malformed, "short", "");
        VerifyQuery(malformed, "ok", "!");

        VERIFY_ARE_EQUAL(0u, static_cast<uint32_t>(Uri::ParseQuery("").size()));
    }

    DEFINE_TEST_CASE(TestFormQueryEncodesAndRoundTrips)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestFormQueryEncodesAndRoundTrips);
        using xbox::httpclient::Uri;

        xbox::httpclient::Map<xbox::httpclient::String, xbox::httpclient::String> values;
        values["a"] = "1 2";
        values["b"] = "x&y=z";
        values["c"] = "50%+";
        values["d"] = "caf\xC3\xA9";
        values["e"] = "-._~!$'()*,;:@/?";
        values["k y"] = "";

        auto formed = Uri::FormQuery(values);
        VERIFY_ARE_EQUAL_STR("a=1+2&b=x%26y%3Dz&c=50%25%2B&d=caf%C3%A9&e=-._~!$'()*,;:@/?&k+y=", formed.c_str());

        auto parsed = Uri::ParseQuery(formed);
        VERIFY_IS_TRUE(parsed == values);

        // An empty key produces a bare value.
        xbox::httpclient::Map<xbox::httpclient::String, xbox::httpclient::String> keyless;
        keyless[""] = "flag";
        VERIFY_ARE_EQUAL_STR("flag", Uri::FormQuery(keyless).c_str());

        // A formed query is valid in a URI.
        auto uri = ParseUri(("https://example.com/?" + formed).c_str());
        VERIFY_IS_TRUE(uri.IsValid());
        VERIFY_ARE_EQUAL_STR(formed.c_str(), uri.Query().c_str());
    }

    DEFINE_TEST_CASE(TestSetQueryAndFragment)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestSetQueryAndFragment);

        auto uri = ParseUri("https://example.com/path?old=1#oldfrag");
        VERIFY_IS_TRUE(uri.IsValid());

        uri.SetQuery("new=2&x=%20");
        uri.SetFragment("section-2");
        VERIFY_ARE_EQUAL_STR("new=2&x=%20", uri.Query().c_str());
        VERIFY_ARE_EQUAL_STR("section-2", uri.Fragment().c_str());
        VERIFY_ARE_EQUAL_STR("/path?new=2&x=%20#section-2", uri.Resource().c_str());
        VERIFY_ARE_EQUAL_STR("https://example.com/path?new=2&x=%20#section-2", uri.ToString().c_str());

        // Invalid values are rejected and leave the previous value in place.
        uri.SetQuery("has space");
        uri.SetFragment("a#b");
        VERIFY_ARE_EQUAL_STR("new=2&x=%20", uri.Query().c_str());
        VERIFY_ARE_EQUAL_STR("section-2", uri.Fragment().c_str());

        uri.SetQuery("");
        uri.SetFragment("");
        VERIFY_ARE_EQUAL_STR("/path", uri.Resource().c_str());
    }
};

DEFINE_TEST_CLASS(HeaderTests)
{
public:
    DEFINE_TEST_CLASS_PROPS(HeaderTests);

    class CallScope
    {
    public:
        CallScope() noexcept
        {
            m_initResult = HCInitialize(nullptr);
            if (SUCCEEDED(m_initResult))
            {
                m_createResult = HCHttpCallCreate(&m_call);
            }
        }

        ~CallScope()
        {
            if (m_call != nullptr)
            {
                HCHttpCallCloseHandle(m_call);
            }
            if (SUCCEEDED(m_initResult))
            {
                HCCleanup();
            }
        }

        CallScope(CallScope const&) = delete;
        CallScope& operator=(CallScope const&) = delete;

        HCCallHandle Get() const
        {
            VERIFY_ARE_EQUAL(S_OK, m_initResult);
            VERIFY_ARE_EQUAL(S_OK, m_createResult);
            return m_call;
        }

    private:
        HRESULT m_initResult{ E_FAIL };
        HRESULT m_createResult{ E_FAIL };
        HCCallHandle m_call{ nullptr };
    };

    DEFINE_TEST_CASE(TestRequestHeaderLookupIsCaseInsensitive)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaderLookupIsCaseInsensitive);
        CallScope scope;
        HCCallHandle call = scope.Get();

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "Content-Type", "application/json", true));

        char const* value = nullptr;
        char const* const spellings[] = { "Content-Type", "content-type", "CONTENT-TYPE", "cOnTeNt-TyPe" };
        for (auto name : spellings)
        {
            value = nullptr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeader(call, name, &value));
            VERIFY_ARE_EQUAL_STR("application/json", value);
        }

        value = "sentinel";
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeader(call, "Content-Typ", &value));
        VERIFY_IS_NULL(value);
    }

    DEFINE_TEST_CASE(TestRequestHeaderSetTwiceReplaces)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaderSetTwiceReplaces);
        CallScope scope;
        HCCallHandle call = scope.Get();

        // allowTracing only controls logging; it has no effect on the stored header.
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "X-Test", "first", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "x-test", "second", false));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1u, numHeaders);

        char const* name = nullptr;
        char const* value = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeaderAtIndex(call, 0, &name, &value));
        VERIFY_ARE_EQUAL_STR("X-Test", name); // the first spelling of the name is kept
        VERIFY_ARE_EQUAL_STR("second", value);

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "X-TEST", "", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeader(call, "x-test", &value));
        VERIFY_ARE_EQUAL_STR("", value);
    }

    DEFINE_TEST_CASE(TestRequestHeaderIndexOrdering)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestRequestHeaderIndexOrdering);
        CallScope scope;
        HCCallHandle call = scope.Get();

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "b-header", "2", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "C", "3", true));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestSetHeader(call, "A-Header", "1", true));

        // Headers are enumerated in case-insensitive name order, not insertion order.
        char const* const expectedNames[] = { "A-Header", "b-header", "C" };
        char const* const expectedValues[] = { "1", "2", "3" };
        for (uint32_t i = 0; i < 3; ++i)
        {
            char const* name = nullptr;
            char const* value = nullptr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeaderAtIndex(call, i, &name, &value));
            VERIFY_ARE_EQUAL_STR(expectedNames[i], name);
            VERIFY_ARE_EQUAL_STR(expectedValues[i], value);
        }

        // Out of range: the request getter reports S_OK with null outputs.
        char const* name = "sentinel";
        char const* value = "sentinel";
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallRequestGetHeaderAtIndex(call, 3, &name, &value));
        VERIFY_IS_NULL(name);
        VERIFY_IS_NULL(value);
    }

    DEFINE_TEST_CASE(TestResponseHeaderDuplicatesAreCombined)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseHeaderDuplicatesAreCombined);
        CallScope scope;
        HCCallHandle call = scope.Get();

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(call, "Set-Cookie", "a=1"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(call, "set-cookie", "b=2"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(call, "SET-COOKIE", "c=3"));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(1u, numHeaders);

        char const* value = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "set-COOKIE", &value));
        VERIFY_ARE_EQUAL_STR("a=1, b=2, c=3", value);

        char const* name = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeaderAtIndex(call, 0, &name, &value));
        VERIFY_ARE_EQUAL_STR("Set-Cookie", name);
        VERIFY_ARE_EQUAL_STR("a=1, b=2, c=3", value);

        value = "sentinel";
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "Cookie", &value));
        VERIFY_IS_NULL(value);
    }

    DEFINE_TEST_CASE(TestResponseHeaderIndexOrdering)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseHeaderIndexOrdering);
        CallScope scope;
        HCCallHandle call = scope.Get();

        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(call, "zeta", "z"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(call, "Alpha", "a"));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeader(call, "beta", "b"));

        char const* const expectedNames[] = { "Alpha", "beta", "zeta" };
        char const* const expectedValues[] = { "a", "b", "z" };
        for (uint32_t i = 0; i < 3; ++i)
        {
            char const* name = nullptr;
            char const* value = nullptr;
            VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeaderAtIndex(call, i, &name, &value));
            VERIFY_ARE_EQUAL_STR(expectedNames[i], name);
            VERIFY_ARE_EQUAL_STR(expectedValues[i], value);
        }

        // Out of range: unlike the request getter, the response getter fails.
        char const* name = "sentinel";
        char const* value = "sentinel";
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseGetHeaderAtIndex(call, 3, &name, &value));
        VERIFY_IS_NULL(name);
        VERIFY_IS_NULL(value);
    }

    DEFINE_TEST_CASE(TestResponseSetHeaderWithLength)
    {
        DEFINE_TEST_CASE_PROPERTIES(TestResponseSetHeaderWithLength);
        CallScope scope;
        HCCallHandle call = scope.Get();

        // Inputs are not NUL-terminated: only nameSize/valueSize bytes are read.
        char const nameBuffer[] = { 'X', '-', 'L', 'e', 'n', 'J', 'U', 'N', 'K' };
        char const valueBuffer[] = { 'a', 'b', 'c', 'X', 'Y', 'Z' };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeaderWithLength(call, nameBuffer, 5, valueBuffer, 3));

        char const* value = nullptr;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "x-len", &value));
        VERIFY_ARE_EQUAL_STR("abc", value);
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "X-LenJUNK", &value));
        VERIFY_IS_NULL(value);

        // A duplicate set with length is appended with ", ".
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeaderWithLength(call, "X-LEN!!", 5, "defGHI", 3));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "X-Len", &value));
        VERIFY_ARE_EQUAL_STR("abc, def", value);

        // Zero-length value.
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeaderWithLength(call, "X-Empty", 7, "ignored", 0));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "x-empty", &value));
        VERIFY_ARE_EQUAL_STR("", value);

        // Embedded NULs are stored, but the C-string getter only exposes the part before the first NUL.
        char const nulValue[] = { 'a', '\0', 'b' };
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseSetHeaderWithLength(call, "X-Nul", 5, nulValue, sizeof(nulValue)));
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetHeader(call, "X-Nul", &value));
        VERIFY_ARE_EQUAL_STR("a", value);
        VERIFY_ARE_EQUAL(3u, static_cast<uint32_t>(call->responseHeaders.find("x-nul")->second.size()));

        uint32_t numHeaders = 0;
        VERIFY_ARE_EQUAL(S_OK, HCHttpCallResponseGetNumHeaders(call, &numHeaders));
        VERIFY_ARE_EQUAL(3u, numHeaders);

        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseSetHeaderWithLength(nullptr, "a", 1, "b", 1));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseSetHeaderWithLength(call, nullptr, 1, "b", 1));
        VERIFY_ARE_EQUAL(E_INVALIDARG, HCHttpCallResponseSetHeaderWithLength(call, "a", 1, nullptr, 1));
    }
};

NAMESPACE_XBOX_HTTP_CLIENT_TEST_END
