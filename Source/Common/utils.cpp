// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

static void ltrim_whitespace(_In_ http_internal_wstring& str)
{
    size_t index;
    for (index = 0; index < str.size() && isspace(str[index]); ++index);
    str.erase(0, index);
}

static void rtrim_whitespace(_In_ http_internal_wstring& str)
{
    size_t index;
    for (index = str.size(); index > 0 && isspace(str[index - 1]); --index);
    str.erase(index);
}

void trim_whitespace(_In_ http_internal_wstring& str)
{
    ltrim_whitespace(str);
    rtrim_whitespace(str);
}

bool StringToUint4(char const* begin, char const* end, uint64_t& v, int32_t base)
{
    v = 0;

    // we have to manually clear errno
    errno = 0;

    char* readTo = nullptr;
    uint64_t n = std::strtoull(begin, &readTo, base);

    if (n == 0 && readTo == begin) // could not convert
    {
        return false;
    }
    else if (errno == ERANGE) // out of range
    {
        return false;
    }
    else if (readTo != end) // could not convert whole string
    {
        return false;
    }
    else
    {
        v = n;
        return true;
    }
}

void BasicAsciiLowercase(String& s)
{
    static std::locale const classicLocale = std::locale::classic();
    std::transform(s.begin(), s.end(), s.begin(), [](char c)
    {
        if ((c & 0x7F) == c)
        {
            return std::tolower(c, classicLocale);
        }
        else
        {
            assert(false);
            return c;
        }
    });
}

bool StringToUint(String const& s, uint64_t& v, int32_t base)
{
    return StringToUint4(s.data(), s.data() + s.size(), v, base);
}

NAMESPACE_XBOX_HTTP_CLIENT_END

NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_BEGIN

HRESULT StdBadAllocToResult(std::bad_alloc const& e, _In_z_ char const* file, uint32_t line)
{
    HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::bad_alloc reached api boundary: %s\n    %s:%u",
        E_OUTOFMEMORY, e.what(), file, line);
    return E_OUTOFMEMORY;
}

HRESULT StdExceptionToResult(std::exception const& e, _In_z_ char const* file, uint32_t line)
{
    HC_TRACE_ERROR(HTTPCLIENT, "[%d] std::exception reached api boundary: %s\n    %s:%u",
        E_FAIL, e.what(), file, line);

    ASSERT(false);
    return E_FAIL;
}

HRESULT UnknownExceptionToResult(_In_z_ char const* file, uint32_t line)
{
    HC_TRACE_ERROR(HTTPCLIENT, "[%d] unknown exception reached api boundary\n    %s:%u",
        E_FAIL, file, line);

    ASSERT(false);
    return E_FAIL;
}

// Validate the datamodel detected by httpClient/config.h
#if HC_DATAMODEL == HC_DATAMODEL_ILP32
static_assert(sizeof(int) == 4, "int is not 32 bits");
static_assert(sizeof(long) == 4, "long is not 32 bits");
static_assert(sizeof(void*) == 4, "pointer is not 32 bits");
#elif HC_DATAMODEL == HC_DATAMODEL_LLP64
static_assert(sizeof(int) == 4, "int is not 32 bits");
static_assert(sizeof(long) == 4, "long is not 32 bits");
static_assert(sizeof(long long) == 8, "long long is not 64 bits");
static_assert(sizeof(void*) == 8, "pointer is not 64 bits");
#elif HC_DATAMODEL == HC_DATAMODEL_LP64
static_assert(sizeof(int) == 4, "int is not 32 bits");
static_assert(sizeof(long) == 8, "long is not 64 bits");
static_assert(sizeof(void*) == 8, "pointer is not 64 bits");
#else
#error Invalid datamodel selected by httpClient/config.h
#endif

NAMESPACE_XBOX_HTTP_CLIENT_DETAIL_END
