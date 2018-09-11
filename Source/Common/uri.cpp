// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "uri.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

void AppendPortToString(String& string, uint16_t port);

/// <summary>
/// Legal characters in the scheme portion include:
/// - Any alphanumeric character
/// - '+' (plus)
/// - '-' (hyphen)
/// - '.' (period)
///
/// Note that the scheme must BEGIN with an alpha character.
/// </summary>
bool IsSchemeCharacter(char c);

/// <summary>
/// Unreserved characters are those that are allowed in a URI but do not have a reserved purpose. They include:
/// - A-Z
/// - a-z
/// - 0-9
/// - '-' (hyphen)
/// - '.' (period)
/// - '_' (underscore)
/// - '~' (tilde)
/// </summary>
bool IsUnreserved(char c);

/// <summary>
/// Subdelimiters are those characters that may have a defined meaning within component
/// of a uri for a particular scheme. They do not serve as delimiters in any case between
/// uri segments. sub_delimiters include:
/// - All of these !$&amp;'()*+,;=
/// </summary>
bool IsSubDelim(char c);

/// <summary>
/// Legal characters in the registered name host portion include:
/// - Any unreserved character
/// - The percent character ('%'), and thus any percent-endcoded octet
/// - The sub-delimiters
/// </summary>
bool IsRegNameCharacter(char c);

/// <summary>
/// Legal characters in the user information portion include:
/// - Any unreserved character
/// - The percent character ('%'), and thus any percent-endcoded octet
/// - The sub-delimiters
/// - ':' (colon)
/// </summary>
bool IsUserInfoCharacter(char c);

/// <summary>
/// Legal characters in the path portion include:
/// - Any unreserved character
/// - The percent character ('%'), and thus any percent-endcoded octet
/// - The sub-delimiters
/// - ':' (colon)
/// - '@' (ampersand)
/// </summary>
bool IsPathCharacter(char c);

/// <summary>
/// Legal characters in the query portion include:
/// - Any path character
/// - '?' (question mark)
/// </summary>
bool IsQueryCharacter(char c);

/// <summary>
/// Legal characters in a key or value of an encoded query string form include:
/// - Any path character (Excluding '&' and '=')
/// - '?' (question mark)
/// </summary>
bool IsQueryKeyOrValueCharacter(char c);

/// <summary>
/// Legal characters in the fragment portion include:
/// - Any path character
/// - '?' (question mark)
/// </summary>
bool IsFragmentCharacter(char c);

Uri::Uri() : m_valid(false)
{
}

Uri::Uri(String const& uri)
{
    m_uri = uri;
    String::const_iterator it = uri.begin();

    if (!ParseScheme(uri, it))
    {
        return;
    }

    if (!ParseAuthority(uri, it))
    {
        return;
    }

    if (it != uri.end() && *it == '/')
    {
        if (!ParsePath(uri, it))
        {
            return;
        }
    }
    else
    {
        // Canonicalize path by making an empty path a '/'
        m_path = "/";
    }

    if (it != uri.end() && *it == '?')
    {
        if (!ParseQuery(uri, it, true))
        {
            return;
        }
    }

    if (it != uri.end() && *it == '#')
    {
        if (!ParseFragment(uri, it, true))
        {
            return;
        }
    }

    if (it != uri.end())
    {
        HC_TRACE_WARNING(HTTPCLIENT, "Unexpected delimiter in URI."); // no tracing uris they could contain PII
        return;
    }

    m_valid = true;
}

bool Uri::IsValid() const
{
    return m_valid;
}

String const& Uri::FullPath() const
{
    return m_uri;
}

String const& Uri::Scheme() const
{
    return m_scheme;
}

String const& Uri::UserInfo() const
{
    return m_userInfo;
}

String const& Uri::Host() const
{
    return m_host;
}

uint16_t Uri::Port() const
{
    return m_port;
}

String const& Uri::Path() const
{
    return m_path;
}

String const& Uri::Query() const
{
    return m_query;
}

void Uri::SetQuery(String&& query)
{
    auto it = query.cbegin();
    if (!ParseQuery(query, it, false) || it != query.end())
    {
        //THROW(E_FAIL, "Attempting to set invalid query on URI.");
    }
}

String const& Uri::Fragment() const
{
    return m_fragment;
}

void Uri::SetFragment(String&& fragment)
{
    auto it = fragment.cbegin();
    if (!ParseFragment(fragment, it, false) || it != fragment.end())
    {
        //THROW(E_FAIL, "Attempting to set invalid fragment on URI.");
    }
}

String Uri::Authority() const
{
    String s{ m_userInfo };
    
    if (!s.empty())
    {
        s += '@';
    }

    s += m_host;

    AppendPortToString(s, m_port);

    return s;
}

String Uri::Resource() const
{
    String s{ m_path };

    if (!m_query.empty())
    {
        s += "?";
        s += m_query;
    }

    if (!m_fragment.empty())
    {
        s += "#";
        s += m_fragment;
    }

    return s;
}

String Uri::ToString() const
{
    String s{ m_scheme };
    s += "://";
    s += Authority();
    s += Resource();

    return s;
}

/* static */ String Uri::Decode(String const& urlPart)
{
    String decoded;

    size_t chunkStart = 0;
    while (true)
    {
        ASSERT(chunkStart <= urlPart.size());

        // find the next sequence to handle
        size_t chunkEnd = urlPart.find_first_of("+%", chunkStart);
        if (chunkEnd == String::npos)
        {
            chunkEnd = urlPart.size();
        }

        // copy up to the end of the chunk
        ASSERT(chunkEnd <= urlPart.size());
        decoded.append(urlPart.data() + chunkStart, urlPart.data() + chunkEnd);

        // if there's nothing left to handle
        if (chunkEnd == urlPart.size())
        {
            break;
        }

        // handle the sequence
        switch (urlPart[chunkEnd])
        {
        case '+':
        {
            decoded.push_back(' ');
            chunkStart = chunkEnd + 1;
        }
        break;
        case '%':
        {
            if (chunkEnd > urlPart.size() - 3) // a % encoding is 3 characters long
            {
                //THROW(E_INVALIDARG, "Invalid % encode in url encoded string");
            }

            uint8_t value = 0;
            if (!HexDecodePair(urlPart[chunkEnd + 1], urlPart[chunkEnd + 2], value))
            {
                //THROW(E_INVALIDARG, "Invalid value for % encode in url encoded string");
            }

            decoded.push_back(value);
            chunkStart = chunkEnd + 3;
        }
        break;
        default:
            ASSERT(false);
            break;
        }
    }

    return decoded;
}

/* static */ String Uri::EncodeQueryStringPart(String const& part)
{
    return EncodeString(part, IsQueryKeyOrValueCharacter);
}

/* static */ Map<String, String> Uri::ParseQuery(String const& urlPart)
{
    Map<String, String> args;

    size_t chunkStart = 0;
    while (true)
    {
        ASSERT(chunkStart <= urlPart.size());

        // find the next sequence to handle
        size_t chunkEnd = urlPart.find('&', chunkStart);
        if (chunkEnd == String::npos)
        {
            chunkEnd = urlPart.size();
        }

        // split the chunk in key and value
        size_t keyBegin = chunkStart;
        size_t keyEnd = urlPart.find('=', chunkStart);
        if (keyEnd > chunkEnd)
        {
            if (chunkStart != chunkEnd) // Ignore "?&"
            {
                // We've found a keyless value
                args[""] = Decode(urlPart.substr(chunkStart, chunkEnd - chunkStart));
            }
        }
        else
        {
            size_t valueBegin = keyEnd + 1;
            size_t valueEnd = chunkEnd;

            args[Decode(urlPart.substr(keyBegin, keyEnd - keyBegin))] =
                Decode(urlPart.substr(valueBegin, valueEnd - valueBegin));
        }

        // if there's nothing left to handle
        if (chunkEnd == urlPart.size())
        {
            break;
        }

        chunkStart = chunkEnd + 1;
        if (chunkStart == urlPart.size())
        {
            // Ignore trailing '&'
            break;
        }
    }

    return args;
}

/* static */ String Uri::FormQuery(Map<String, String> const& queryMap)
{
    String result;
    for (auto const& it : queryMap)
    {
        if (!result.empty())
        {
            result += '&';
        }

        if (!it.first.empty())
        {
            result += EncodeQueryStringPart(it.first);
            result += '=';
        }

        result += EncodeQueryStringPart(it.second);
    }

    return result;
}

/* static */ String Uri::EncodeString(String const& originalString, bool(*characterAllowed)(char))
{
    String encodedString;
    encodedString.reserve(originalString.length());

    auto chunkStart = originalString.begin();
    while (chunkStart != originalString.end())
    {
        auto chunkEnd = chunkStart;
        while (chunkEnd != originalString.end() && characterAllowed(*chunkEnd) && *chunkEnd != '+' && *chunkEnd != '%')
        {
            ++chunkEnd;
        }

        encodedString.append(chunkStart, chunkEnd);
        if (chunkEnd != originalString.end())
        {
            if (*chunkEnd == ' ')
            {
                encodedString += '+';
            }
            else
            {
                encodedString += '%';
                char high = '0';
                char low = '0';
                HexEncodeByte(*chunkEnd, high, low);
                encodedString += high;
                encodedString += low;
            }

            ++chunkEnd;
        }

        chunkStart = chunkEnd;
    }

    return encodedString;
}

bool Uri::ParseScheme(String const& uri, String::const_iterator& it)
{
    if (it == uri.end())
    {
        HC_TRACE_WARNING(HTTPCLIENT, "Missing scheme in URI."); // no tracing uris they could contain PII
        return false;
    }

    auto schemeEnd = it;

    if (!IsAlpha(*schemeEnd)) // We've already checked we have at least 1 character
    {
        HC_TRACE_WARNING(HTTPCLIENT, "Scheme must start with a letter."); // no tracing uris they could contain PII
        return false;
    }

    ++schemeEnd;
    for (; schemeEnd != uri.end() && *schemeEnd != ':'; ++schemeEnd)
    {
        if (!IsSchemeCharacter(*schemeEnd))
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Invalid character found in scheme."); // no tracing uris they could contain PII
            return false;
        }
    }

    if (schemeEnd == uri.end())
    {
        HC_TRACE_WARNING(HTTPCLIENT, "Cannot detect scheme in URI."); // no tracing uris they could contain PII
        return false;
    }

    m_scheme.assign(it, schemeEnd);
    it = schemeEnd + 1; // consume the ':'

    // Canonicalize the scheme by lowercasing it
    BasicAsciiLowercase(m_scheme);

    return true;
}

bool Uri::ParseAuthority(String const& uri, String::const_iterator& it)
{
    // Authority must begin with "//"
    for (size_t i = 0; i < 2; ++i, ++it)
    {
        if (it == uri.end() || *it != '/')
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Authority is required in URI."); // no tracing uris they could contain PII
            return false;
        }
    }

    if (!ParseUserInfo(uri, it) ||
        !ParseHost(uri, it))
    {
        return false;
    }

    if (it != uri.end() && *it == ':')
    {
        return ParsePort(uri, it);
    }

    return true;
}

bool Uri::ParseUserInfo(String const& uri, String::const_iterator& it)
{
    auto userEnd = it;
    while (userEnd != uri.end() && IsUserInfoCharacter(*userEnd))
    {
        ++userEnd;
    }

    if (userEnd != uri.end() && *userEnd == '@')
    {
        // This means we have a user info
        m_userInfo.assign(it, userEnd);
        it = userEnd + 1; // consume the '@'
    }

    return true; // User info is optional. We never fail here.
}

bool Uri::ParseHost(String const& uri, String::const_iterator& it)
{
    if (it == uri.end())
    {
        HC_TRACE_WARNING(HTTPCLIENT, "Missing host in URI."); // no tracing uris they could contain PII
        return false;
    }

    // extract host.
    // either a host string
    // an IPv4 address
    // or an IPv6 address
    // or IPvFuture address (not supported)
    if (*it == '[')
    {
        ++it; // consume the '['
              // IPv6 literal
              // extract IPv6 digits until ']'
        auto hostEnd = std::find(it, uri.end(), ']');

        if (hostEnd == uri.end())
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Cannot parse IPv6 literal."); // no tracing uris they could contain PII
            return false;
        }
        else if (*it == 'v' || *it == 'V')
        {
            HC_TRACE_WARNING(HTTPCLIENT, "IPvFuture literal not supported."); // no tracing uris they could contain PII
            return false;
        }
        else
        {
            // Validate the IPv6 address
            for (auto tempIt = it; tempIt != hostEnd; ++tempIt)
            {
                if (*tempIt != ':' && !IsHexChar(*tempIt))
                {
                    HC_TRACE_WARNING(HTTPCLIENT, "Invalid character found in IPv6 literal."); // no tracing uris they could contain PII
                    return false;
                }
            }

            m_host.assign(it, hostEnd);
        }

        it = hostEnd + 1; // consume the ']'
    }
    else
    {
        // IPv4 or registered name
        // extract until : or / or ? or #
        String::const_iterator hostEnd = it;
        for (; hostEnd != uri.end() && *hostEnd != ':' && *hostEnd != '/' && *hostEnd != '?' && *hostEnd != '#'; ++hostEnd)
        {
            if (!IsRegNameCharacter(*hostEnd))
            {
                HC_TRACE_WARNING(HTTPCLIENT, "Invalid character found in host."); // no tracing uris they could contain PII
                return false;
            }
        }

        m_host.assign(it, hostEnd);
        it = hostEnd;
        if (m_host.empty())
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Empty host name in URI."); // no tracing uris they could contain PII
            return false;
        }
    }

    // Canonicalize the host by lowercasing it
    BasicAsciiLowercase(m_host);

    return true;
}

bool Uri::ParsePort(String const& uri, String::const_iterator& it)
{
    ASSERT(*it == ':');
    ++it; // Skip the ':'

    auto portEnd = it;
    size_t portLen = 0;
    for (; portEnd != uri.end() && IsNum(*portEnd); ++portEnd)
    {
        ++portLen;
    }

    if (portLen == 0)
    {
        return true; // Port characters are optional
    }

    char const* port = &*it;
    uint64_t portV = 0;
    if (!StringToUint4(port, port + portLen, portV, 0))
    {
        HC_TRACE_WARNING(HTTPCLIENT, "Cannot parse port in URI."); // no tracing uris they could contain PII
        return false;
    }

    m_port = static_cast<uint16_t>(portV);
    it = portEnd;

    return true;
}

bool Uri::ParsePath(String const& uri, String::const_iterator& it)
{
    ASSERT(*it == '/');

    auto pathEnd = it;
    for (; pathEnd != uri.end() && *pathEnd != '?' && *pathEnd != '#'; ++pathEnd)
    {
        if (!IsPathCharacter(*pathEnd))
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Invalid character found in path."); // no tracing uris they could contain PII
            return false;
        }
    }

    m_path.assign(it, pathEnd);
    it = pathEnd;
    return true;
}

bool Uri::ParseQuery(String const& uri, String::const_iterator& it, bool expectQuestion)
{
    if (expectQuestion)
    {
        ASSERT(*it == '?');
        ++it; // Skip the '?'
    }

    auto queryEnd = it;
    for (; queryEnd != uri.end() && *queryEnd != '#'; ++queryEnd)
    {
        if (!IsQueryCharacter(*queryEnd))
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Invalid character found in query."); // no tracing uris they could contain PII
            return false;
        }
    }

    m_query.assign(it, queryEnd);
    it = queryEnd;
    return true;
}

bool Uri::ParseFragment(String const& uri, String::const_iterator& it, bool expectOctothorpe)
{
    if (expectOctothorpe)
    {
        ASSERT(*it == '#');
        ++it; // Skip the '#'
    }

    auto fragmentEnd = it;
    for (; fragmentEnd != uri.end(); ++fragmentEnd)
    {
        if (!IsFragmentCharacter(*fragmentEnd))
        {
            HC_TRACE_WARNING(HTTPCLIENT, "Invalid character found in fragment."); // no tracing uris they could contain PII
            return false;
        }
    }

    m_fragment.assign(it, fragmentEnd);
    it = fragmentEnd;
    return true;
}

void AppendPortToString(String& string, uint16_t port)
{
    if (port == 0)
    {
        return;
    }

    AppendFormat(string, ":%u", port);
}

bool IsSchemeCharacter(char c)
{
    return IsAlnum(c) || c == '+' || c == '-' || c == '.';
}

bool IsUnreserved(char c)
{
    return IsAlnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
}

bool IsSubDelim(char c)
{
    switch (c)
    {
    case '!':
    case '$':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '+':
    case ',':
    case ';':
    case '=':
        return true;
    default:
        return false;
    }
}

bool IsRegNameCharacter(char c)
{
    return IsUnreserved(c) || IsSubDelim(c) || c == '%';
}

bool IsUserInfoCharacter(char c)
{
    return IsUnreserved(c) || IsSubDelim(c) || c == '%' || c == ':';
}

bool IsPathCharacter(char c)
{
    return IsUnreserved(c) || IsSubDelim(c) || c == '%' || c == '/' || c == ':' || c == '@';
}

bool IsQueryCharacter(char c)
{
    return IsPathCharacter(c) || c == '?';
}

bool IsQueryKeyOrValueCharacter(char c)
{
    return IsQueryCharacter(c) && c != '=' && c != '&';
}

bool IsFragmentCharacter(char c)
{
    // this is intentional, they have the same set of legal characters
    return IsQueryCharacter(c);
}

NAMESPACE_XBOX_HTTP_CLIENT_END