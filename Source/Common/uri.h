// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.
#pragma once
#include "utils.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

class Uri
{
public:
    Uri();
    explicit Uri(String const& uri);

    bool IsValid() const;

    bool IsSecure() const
    {
        return (Scheme() == "https" || Scheme() == "wss");
    }

    bool IsPortDefault() const
    {
        return !IsEmpty() && Port() == 0;
    }

    bool IsEmpty() const
    {
        return Host().empty();
    }

    String const& FullPath() const;
    String const& Scheme() const;
    String const& UserInfo() const;
    String const& Host() const;
    uint16_t Port() const;
    String const& Path() const;
    String const& Query() const;
    void SetQuery(String&& query);
    String const& Fragment() const;
    void SetFragment(String&& query);

    String Authority() const;
    String Resource() const;

    String ToString() const;

    static Map<String, String> ParseQuery(String const& urlPart);
    static String FormQuery(Map<String, String> const& queryMap);

private:
    String m_uri;
    String m_scheme;
    String m_userInfo;
    String m_host;
    String m_path;
    String m_query;
    String m_fragment;
    uint16_t m_port = 0;
    bool m_valid = false;

    bool ParseScheme(String const& uri, String::const_iterator& it);
    bool ParseAuthority(String const& uri, String::const_iterator& it);
    bool ParseUserInfo(String const& uri, String::const_iterator& it);
    bool ParseHost(String const& uri, String::const_iterator& it);
    bool ParsePort(String const& uri, String::const_iterator& it);
    bool ParsePath(String const& uri, String::const_iterator& it);
    bool ParseQuery(String const& uri, String::const_iterator& it, bool expectQuestion);
    bool ParseFragment(String const& uri, String::const_iterator& it, bool expectOctothorpe);

    static String Decode(String const& urlPart);
    static String EncodeQueryStringPart(String const& part);
    static String EncodeString(String const& originalString, bool(*characterAllowed)(char));
};

NAMESPACE_XBOX_HTTP_CLIENT_END