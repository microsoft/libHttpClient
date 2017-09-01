// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

namespace details
{
    template <size_t charsize>
    struct widestring
    {
    };

    template <>
    struct widestring<2>
    {
        static std::wstring to_wstring(const std::string& input)
        {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> utfConverter;
            return utfConverter.from_bytes(input);
        }

        static std::string to_utf8string(const std::wstring& input)
        {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> utfConverter;
            return utfConverter.to_bytes(input);
        }
    };

    template <>
    struct widestring<4>
    {
        static std::wstring to_wstring(const std::string& input)
        {
            std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> utfConverter;
            return utfConverter.from_bytes(input);
        }

        static std::string to_utf8string(const std::wstring& input)
        {
            std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> utfConverter;
            return utfConverter.to_bytes(input);
        }
    };
}

std::string to_utf8string(std::string value) 
{
    return value;
}

std::string to_utf8string(const std::wstring &value) 
{
    return details::widestring<sizeof(wchar_t)>::to_utf8string(value);
}

std::wstring to_wstring(std::wstring value)
{
    return value;
}

std::wstring to_wstring(const std::string &value)
{
    return details::widestring<sizeof(wchar_t)>::to_wstring(value);
}

NAMESPACE_XBOX_HTTP_CLIENT_END
