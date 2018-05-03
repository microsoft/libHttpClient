// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#pragma once

using String = http_internal_string;

template<class K, class V, class LESS = std::less<K>>
using Map = std::map<K, V, LESS, http_stl_allocator<std::pair<K const, V>>>;


NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

void trim_whitespace(_In_ http_internal_wstring& str);
void BasicAsciiLowercase(String& s);

bool StringToUint(String const& s, uint64_t& v, int32_t base = 0);
bool StringToUint4(char const* begin, char const* end, uint64_t& v, int32_t base);

template<class TBuffer>
void FormatHelper(TBuffer& buffer, _In_z_ _Printf_format_string_ char const* format, va_list args)
{
    va_list args1{};
    va_copy(args1, args);
    int required = std::vsnprintf(nullptr, 0, format, args1);
    va_end(args1);

    ASSERT(required > 0);

    size_t originalSize = buffer.size();

    va_list args2{};
    va_copy(args2, args);
    buffer.resize(originalSize + static_cast<size_t>(required) + 1); // add space for null terminator
    int written = std::vsnprintf(reinterpret_cast<char*>(&buffer[originalSize]), buffer.size(), format, args2);
    va_end(args2);

    ASSERT(written == required);
    //UNREFERENCED_LOCAL(written);

    buffer.resize(buffer.size() - 1); // drop null terminator
}

template<class TBuffer>
void AppendFormat(TBuffer& buffer, _In_z_ _Printf_format_string_ char const* format, ...)
{
    va_list args{};
    va_start(args, format);
    FormatHelper(buffer, format, args);
    va_end(args);
}

inline
bool IsAlpha(char ch)
{
    return (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z');
}

inline
bool IsNum(char ch)
{
    return (ch >= '0' && ch <= '9');
}

inline
bool IsAlnum(char ch)
{
    return IsAlpha(ch) ||
        IsNum(ch);
}

inline
bool IsHexChar(char ch)
{
    return (ch >= 'A' && ch <= 'F') ||
        (ch >= 'a' && ch <= 'f') ||
        IsNum(ch);
}

inline
char HexEncode(uint8_t v)
{
    switch (v)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        return '0' + v;
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
        return 'A' + (v - 10);
    default:
        ASSERT(false);
        return '0';
    }
}

inline
void HexEncodeByte(uint8_t v, char& highC, char& lowC)
{
    highC = HexEncode(v >> 4);
    lowC = HexEncode(v & 0xF);
}

inline
bool HexDecode(char c, uint8_t& v)
{
    switch (c)
    {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        v = static_cast<uint8_t>(c - '0');
        return true;
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
        v = static_cast<uint8_t>(c - 'A' + 10);
        return true;
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
        v = static_cast<uint8_t>(c - 'a' + 10);
        return true;
    default:
        v = 0;
        return false;
    }
}
inline
bool HexDecodePair(char highC, char lowC, uint8_t& v)
{
    uint8_t high = 0;
    uint8_t low = 0;
    if (!HexDecode(highC, high) || !HexDecode(lowC, low))
    {
        v = 0;
        return false;
    }

    v = (high << 4) + low;
    return true;
}

class hc_task : public std::enable_shared_from_this<hc_task>
{
public:
    hc_task() {}

    virtual ~hc_task() {}
};

inline
uint64_t ThisThreadId()
{
    // use platform specific implementations so that it's easy to look up values
    // in the debugger
#if HC_PLATFORM_IS_MICROSOFT
    return GetCurrentThreadId();
#elif HC_PLATFORM == HC_PLATFORM_IOS
    uint64_t threadId = 0;
    if (pthread_threadid_np(0, &threadId) != 0)
    { // MAVA_TODO: test
        threadId = pthread_mach_thread_np(pthread_self());
    }
    
    return threadId;
#else
    return pthread_self();
#endif
}

NAMESPACE_XBOX_HTTP_CLIENT_END
