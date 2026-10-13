// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"
#include "utils_win.h"
#include "uri.h"

#include <httpClient/trace.h>
#if !HC_XDK_API && !HC_UWP_API
#include <winhttp.h>
#endif

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

http_internal_wstring utf16_from_utf8(_In_z_ const char* utf8)
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

    if (size > INT_MAX)
    {
        throw std::exception("utf8_from_utf16 failed: input too large");
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
        HC_TRACE_ERROR(HTTPCLIENT, "utf8_from_utf16 failed during buffer size query with error: %u", err);
#endif
        throw std::exception("utf8_from_utf16 failed");
    }

    // allocate the output buffer, queryResult is the required size
    http_internal_string utf8(static_cast<size_t>(queryResult), '\0');
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
        HC_TRACE_ERROR(HTTPCLIENT, "utf8_from_utf16 failed during conversion: %u", err);
#endif
        throw std::exception("utf8_from_utf16 failed");
    }

    return utf8;
}

http_internal_wstring utf16_from_utf8(_In_reads_(size) const char* utf8, size_t size)
{
    // early out on empty strings since they are trivially convertible
    if (size == 0)
    {
        return L"";
    }

    if (size > INT_MAX)
    {
        throw std::exception("utf16_from_utf8 failed: input too large");
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
        HC_TRACE_ERROR(HTTPCLIENT, "utf16_from_utf8 failed during buffer size query with error: %u", err);
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
        HC_TRACE_ERROR(HTTPCLIENT, "utf16_from_utf8 failed during conversion: %u", err);
#endif
        throw std::exception("utf16_from_utf8 failed");
    }

    return utf16;
}

#if !HC_XDK_API && !HC_UWP_API

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

proxy_type get_ie_proxy_info(_In_ proxy_protocol protocol, _Inout_ xbox::httpclient::Uri& proxyUri)
{
    proxy_type proxyType = proxy_type::automatic_proxy;

#if HC_PLATFORM != HC_PLATFORM_GDK && !defined(HC_WINHTTP_WIN32_NOXASYNC)
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config = { 0 };
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config))
    {
        return proxyType;
    }

    if (config.fAutoDetect)
    {
        proxyType = proxy_type::autodiscover_proxy;
    }
    else if (config.lpszProxy != nullptr)
    {
        http_internal_wstring proxyAddress;

        // something like "http=127.0.0.1:8888;https=127.0.0.1:8888", or "localhost:80"
        http_internal_wstring proxyConfig = config.lpszProxy;
        if (proxyConfig.find(L"=") == http_internal_wstring::npos)
        {
            proxyAddress = proxyConfig;
        }
        else
        {
            WString protocolString;
            switch (protocol)
            {
            case proxy_protocol::http:
            {
                protocolString = L"http";
                break;
            }
            case proxy_protocol::https:
            {
                protocolString = L"https";
                break;
            }
            case proxy_protocol::websocket:
            {
                protocolString = L"socks";
                break;
            }
            case proxy_protocol::ftp:
            {
                protocolString = L"ftp";
                break;
            }
            default:
            {
                assert(false);
            }
            }

            auto pos = proxyConfig.find(protocolString);
            if (pos != http_internal_wstring::npos)
            {
                proxyAddress = proxyConfig.substr(pos + protocolString.length() + 1);
                proxyAddress = proxyAddress.substr(0, proxyConfig.find(';', pos));
            }
        }

        if (!proxyAddress.empty())
        {
            if (proxyAddress.find(L"://") == http_internal_wstring::npos)
            {
                proxyAddress = L"http://" + proxyAddress;
            }
        }

        proxyType = (proxyAddress.empty()) ? proxy_type::no_proxy : proxy_type::named_proxy;
        proxyUri = Uri(utf8_from_utf16(proxyAddress));
    }
    else
    {
        proxyType = proxy_type::no_proxy;
    }
#else
    UNREFERENCED_PARAMETER(protocol);
    UNREFERENCED_PARAMETER(proxyUri);
#endif

    return proxyType;
}

NAMESPACE_XBOX_HTTP_CLIENT_END

#endif

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

http_internal_string format_platform_error_message(_In_ uint32_t platformErrorCode) noexcept
{
    if (platformErrorCode == 0)
    {
        return {};
    }

    // Win32 errors usually arrive wrapped as HRESULT_FROM_WIN32; the message tables are keyed by the raw code.
    DWORD messageId = platformErrorCode;
    if (HRESULT_FACILITY(platformErrorCode) == FACILITY_WIN32 && (platformErrorCode & 0x80000000) != 0)
    {
        messageId = HRESULT_CODE(platformErrorCode);
    }

    DWORD flags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    LPCVOID source = nullptr;

#if HC_PLATFORM == HC_PLATFORM_WIN32 || HC_PLATFORM == HC_PLATFORM_GDK
    // WinHTTP error text lives in winhttp.dll rather than the system message table.
    constexpr DWORD winHttpErrorBase = 12000;
    constexpr DWORD winHttpErrorLast = 12199;
    if (messageId >= winHttpErrorBase && messageId <= winHttpErrorLast)
    {
        HMODULE winHttpModule = GetModuleHandleW(L"winhttp.dll");
        if (winHttpModule != nullptr)
        {
            flags = FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;
            source = winHttpModule;
        }
    }
#endif

    wchar_t buffer[512]{};
    DWORD length = FormatMessageW(flags, source, messageId, 0, buffer, static_cast<DWORD>(ARRAYSIZE(buffer)), nullptr);
    while (length > 0 && iswspace(buffer[length - 1]))
    {
        --length;
    }

    if (length == 0)
    {
        return {};
    }

    try
    {
        return utf8_from_utf16(buffer, length);
    }
    catch (...)
    {
        return {};
    }
}

NAMESPACE_XBOX_HTTP_CLIENT_END
