// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

std::string to_utf8string(std::string value) 
{ 
    return value; 
}

std::string to_utf8string(const std::wstring &value) 
{ 
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return conversion.to_bytes(value);
}

std::wstring to_utf16string(const std::string &value)
{ 
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return conversion.from_bytes(value);
} 

std::wstring to_utf16string(std::wstring value) 
{ 
    return value; 
}

NAMESPACE_XBOX_HTTP_CLIENT_END
