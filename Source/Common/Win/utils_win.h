// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

#include "../Global/mem.h"

#if HC_USE_HANDLES

class win32_handle
{
public:
    win32_handle() : m_handle(nullptr)
    {
    }

    ~win32_handle()
    {
        if (m_handle != nullptr) CloseHandle(m_handle);
        m_handle = nullptr;
    }

    void set(HANDLE handle)
    {
        m_handle = handle;
    }

    HANDLE get() { return m_handle; }

private:
    HANDLE m_handle;
};

#endif

using http_internal_wstring = http_internal_basic_string<wchar_t>;

http_internal_string utf8_from_utf16(const http_internal_wstring& utf16);
http_internal_wstring utf16_from_utf8(const http_internal_string& utf8);

http_internal_string utf8_from_utf16(_In_z_ PCWSTR utf16);
http_internal_wstring utf16_from_utf8(_In_z_ PCSTR utf8);

http_internal_string utf8_from_utf16(_In_reads_(size) PCWSTR utf16, size_t size);
http_internal_wstring utf16_from_utf8(_In_reads_(size) PCSTR utf8, size_t size);
