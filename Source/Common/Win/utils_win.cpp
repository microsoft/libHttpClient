// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "utils_win.h"

#include <httpClient/trace.h>

http_internal_string utf8_from_utf16(const http_internal_wstring& utf16)
{
    return utf8_from_utf16(utf16.data(), utf16.size());
}

http_internal_wstring utf16_from_utf8(const http_internal_string& utf8)
{
    return utf16_from_utf8(utf8.data(), utf8.size());
}

http_internal_string utf8_from_utf16(_In_z_ PCWSTR utf16)
{
    return utf8_from_utf16(utf16, wcslen(utf16));
}

http_internal_wstring utf16_from_utf8(_In_z_ UTF8CSTR utf8)
{
    return utf16_from_utf8(utf8, strlen(utf8));
}

http_internal_string utf8_from_utf16(_In_reads_(size) PCWSTR utf16, size_t size)
{
    // early out on empty strings since they are trivially convertible
    if (size == 0)
    {
        return "";
    }

    // query for the buffer size
    auto queryResult = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        utf16, static_cast<int>(size),
        nullptr, 0,
        nullptr, nullptr
    );
    if (queryResult == 0)
    {
#if HC_TRACE_ERROR_ENABLE // to avoid unused variable warnings
        auto err = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "utf16_from_uft8 failed during buffer size query with error: %u", err);
#endif
        throw std::exception("utf16_from_utf8 failed");
    }

    // allocate the output buffer, queryResult is the required size
    http_internal_string utf8(static_cast<size_t>(queryResult), L'\0');
    auto conversionResult = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        utf16, static_cast<int>(size),
        &utf8[0], static_cast<int>(utf8.size()),
        nullptr, nullptr
    );
    if (conversionResult == 0)
    {
#if HC_TRACE_ERROR_ENABLE // to avoid unused variable warnings
        auto err = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "utf16_from_uft8 failed during conversion: %u", err);
#endif
        throw std::exception("utf16_from_utf8 failed");
    }

    return utf8;
}

http_internal_wstring utf16_from_utf8(_In_reads_(size) UTF8CSTR utf8, size_t size)
{
    // early out on empty strings since they are trivially convertible
    if (size == 0)
    {
        return L"";
    }

    // query for the buffer size
    auto queryResult = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8, static_cast<int>(size),
        nullptr, 0
    );
    if (queryResult == 0)
    {
#if HC_TRACE_ERROR_ENABLE // to avoid unused variable warnings
        auto err = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "utf16_from_uft8 failed during buffer size query with error: %u", err);
#endif
        throw std::exception("utf16_from_utf8 failed");
    }

    // allocate the output buffer, queryResult is the required size
    http_internal_wstring utf16(static_cast<size_t>(queryResult), L'\0');
    auto conversionResult = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8, static_cast<int>(size),
        &utf16[0], static_cast<int>(utf16.size())
    );
    if (conversionResult == 0)
    {
#if HC_TRACE_ERROR_ENABLE // to avoid unused variable warnings
        auto err = GetLastError();
        HC_TRACE_ERROR(HTTPCLIENT, "utf16_from_uft8 failed during conversion: %u", err);
#endif
        throw std::exception("utf16_from_utf8 failed");
    }

    return utf16;
}
