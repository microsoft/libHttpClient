// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include "pch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>

#include "trace_internal.h"
#include "utils.h"
#include "Platform/PlatformTrace.h"

#if HC_PLATFORM_IS_MICROSOFT
#include <TraceLoggingProvider.h>
#endif

namespace
{

#if !HC_PLATFORM_IS_MICROSOFT
template<size_t SIZE>
int sprintf_s(char(&buffer)[SIZE], _Printf_format_string_ char const* format ...) noexcept
{
    va_list varArgs{};
    va_start(varArgs, format);
    auto result = vsnprintf(buffer, SIZE, format, varArgs);
    va_end(varArgs);
    return result;
}

int sprintf_s(char* buffer, size_t size, _Printf_format_string_ char const* format ...) noexcept
{
    va_list varArgs{};
    va_start(varArgs, format);
    auto result = vsnprintf(buffer, size, format, varArgs);
    va_end(varArgs);
    return result;
}

template<size_t SIZE>
int _snprintf_s(char(&buffer)[SIZE], size_t /*count*/, _Printf_format_string_ char const* format ...) noexcept
{
    va_list varArgs{};
    va_start(varArgs, format);
    auto result = vsnprintf(buffer, SIZE, format, varArgs);
    va_end(varArgs);
    return result;
}

template<size_t SIZE>
int vstprintf_s(char(&buffer)[SIZE], _Printf_format_string_ char const* format, va_list varArgs) noexcept
{
    return vsnprintf(buffer, SIZE, format, varArgs);
}
#else
template<size_t SIZE>
int vstprintf_s(char(&buffer)[SIZE], _Printf_format_string_ char const* format, va_list varArgs) noexcept
{
    _set_errno(0);
    _vsnprintf_s(buffer, _TRUNCATE, format, varArgs);

    if (errno != 0)
    {
        return -1;
    }

    return 0;
}
#endif

#if HC_PLATFORM_IS_MICROSOFT
TRACELOGGING_DEFINE_PROVIDER(
    g_hTraceLoggingProvider,
    "libHttpClient",
    (0x2b6274b2, 0x159f, 0x4e6f, 0xb8, 0x0, 0xd, 0x17, 0x0, 0x9e, 0x14, 0x7a) // {2B6274B2-159F-4E6F-B800-0D17009E147A}
);


// Helper method to ensure our ETW Provider never gets registered twice. Registering an ETW provider twice may result
// in a crash or undefined behavior.  See https://learn.microsoft.com/en-us/windows/win32/api/traceloggingprovider/nf-traceloggingprovider-traceloggingregister
// for more details.
void RegisterProvider()
{
    struct ProviderRegistrar
    {
        ProviderRegistrar()
        {
            TraceLoggingRegister(g_hTraceLoggingProvider);
        }
        ~ProviderRegistrar()
        {
            TraceLoggingUnregister(g_hTraceLoggingProvider);
        }
    };

    static ProviderRegistrar s_registrar{};
}
#endif

//------------------------------------------------------------------------------
// Trace implementation
//------------------------------------------------------------------------------

template<size_t SIZE>
void FormatTrace(
    char const* areaName,
    HCTraceLevel level,
    uint64_t threadId,
    uint64_t timestamp,
    char const* message,
    char (&outputBuffer)[SIZE]
)
{
    // Needs to match the HCTraceLevel enum
    static char const* traceLevelNames[] =
    {
        "Off",
        "E",
        "W",
        "P",
        "I",
        "V",
    };

    std::time_t  timeTInSec = static_cast<std::time_t>(timestamp / 1000);
    uint32_t     fractionMSec = static_cast<uint32_t>(timestamp % 1000);
    std::tm      fmtTime = {};

#if _WIN32
    localtime_s(&fmtTime, &timeTInSec);
#elif HC_PLATFORM == HC_PLATFORM_SONY_PLAYSTATION_4 || HC_PLATFORM == HC_PLATFORM_SONY_PLAYSTATION_5
    localtime_s(&timeTInSec, &fmtTime);
#else
    localtime_r(&timeTInSec, &fmtTime);
#endif

    // [threadId][level][time][area] message
    auto written = _snprintf_s(outputBuffer, SIZE - 3, "[%04llX][%s][%02d:%02d:%02d.%03u][%s] %s",
        threadId,
        traceLevelNames[static_cast<size_t>(level)],
        fmtTime.tm_hour,
        fmtTime.tm_min,
        fmtTime.tm_sec,
        fractionMSec,
        areaName,
        message
    );
    if (written <= 0)
    {
        return;
    }

    // Make sure there is room for the \r \n and \0
    written = std::min(written, static_cast<int>(SIZE - 3));
    auto remaining = SIZE - written;

    // Print new line
    auto written2 = sprintf_s(outputBuffer + written, remaining, "\r\n");
    if (written2 <= 0)
    {
        return;
    }
}

void TraceMessageToDebugger(
    char const* areaName,
    HCTraceLevel level,
    uint64_t threadId,
    uint64_t timestamp,
    char const* message
) noexcept
{
    if (!GetTraceState().GetTraceToDebugger())
    {
        return;
    }
    
    static size_t const BUFFER_SIZE = 4096;
    char outputBuffer[BUFFER_SIZE] = {};
    FormatTrace(areaName, level, threadId, timestamp, message, outputBuffer);

    xbox::httpclient::TraceToDebugger(areaName, level, outputBuffer);
}

void TraceMessageToClient(
    char const* areaName,
    HCTraceLevel level,
    uint64_t threadId,
    uint64_t timestamp,
    char const* message
) noexcept
{
    auto callback = GetTraceState().GetClientCallback();
    if (callback)
    {
        callback(areaName, level, threadId, timestamp, message);
    }
}

#if HC_PLATFORM_IS_MICROSOFT
void TraceMessageToETW(
    char const* areaName,
    HCTraceLevel level,
    uint64_t threadId,
    uint64_t timestamp,
    char const* message
) noexcept
{
    if (!GetTraceState().GetEtwEnabled())
    {
        return;
    }

    static size_t const BUFFER_SIZE = 4096;
    char outputBuffer[BUFFER_SIZE] = {};
    FormatTrace(areaName, level, threadId, timestamp, message, outputBuffer);

    TraceLoggingWrite(
        g_hTraceLoggingProvider,
        "libHttpClient_TraceMessage",
        TraceLoggingString(outputBuffer, "Message")
    );
}
#endif

}

STDAPI_(void) HCTraceSetTraceToDebugger(_In_ bool traceToDebugger) noexcept
{
    GetTraceState().SetTraceToDebugger(traceToDebugger);
}

STDAPI_(void) HCTraceSetClientCallback(_In_opt_ HCTraceCallback* callback) noexcept
{
    GetTraceState().SetClientCallback(callback);
}

#if HC_PLATFORM_IS_MICROSOFT
STDAPI_(void) HCTraceSetEtwEnabled(_In_ bool enabled) noexcept
{
    GetTraceState().SetEtwEnabled(enabled);

    if (enabled)
    {
        RegisterProvider();
    }
}
#endif

STDAPI_(void) HCTraceImplMessage(
    struct HCTraceImplArea const* area,
    HCTraceLevel level,
    _Printf_format_string_ char const* format,
    ...
    ) noexcept
{
    va_list varArgs{};
    va_start(varArgs, format);
    HCTraceImplMessage_v(area, level, format, varArgs);
    va_end(varArgs);
}

STDAPI_(void) HCTraceImplMessage_v(
    struct HCTraceImplArea const* area,
    HCTraceLevel level,
    _Printf_format_string_ char const* format,
    va_list varArgs
) noexcept
{
    if (!area)
    {
        return;
    }

    if (level > area->Verbosity)
    {
        return;
    }

    if (!GetTraceState().IsSetup())
    {
        return;
    }

    if (!format)
    {
        return;
    }

    // Only do work if there's reason to
    if (GetTraceState().GetClientCallback() == nullptr && !GetTraceState().GetTraceToDebugger() && !GetTraceState().GetEtwEnabled())
    {
        return;
    }

    auto timestamp = GetTraceState().GetTimestamp();
    auto threadId = xbox::httpclient::GetThreadId();

    char message[4096] = {};

    auto result = vstprintf_s(message, format, varArgs);

    if (result < 0)
    {
        return;
    }

    TraceMessageToDebugger(area->Name, level, threadId, timestamp, message);
    TraceMessageToClient(area->Name, level, threadId, timestamp, message);
#if HC_PLATFORM_IS_MICROSOFT
    TraceMessageToETW(area->Name, level, threadId, timestamp, message);
#endif
}

STDAPI_(uint64_t) HCTraceImplScopeId() noexcept
{
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
}

// trace_internal.h
TraceState::TraceState() noexcept : m_traceToDebugger(false)
{}

void TraceState::Init() noexcept
{
    auto previousCount = m_tracingClients++;
    if (previousCount == 0)
    {
        m_initTime = std::chrono::high_resolution_clock::now();
    }
}

void TraceState::Cleanup() noexcept
{
    --m_tracingClients;
}

bool TraceState::IsSetup() const noexcept
{
    return m_tracingClients > 0;
}

bool TraceState::GetTraceToDebugger() noexcept
{
    return m_traceToDebugger;
}

void TraceState::SetTraceToDebugger(_In_ bool traceToDebugger) noexcept
{
    m_traceToDebugger = traceToDebugger;
}

bool TraceState::GetEtwEnabled() const noexcept
{
    return m_etwEnabled;
}

#if HC_PLATFORM_IS_MICROSOFT
void TraceState::SetEtwEnabled(_In_ bool etwEnabled) noexcept
{
    m_etwEnabled = etwEnabled;
}
#endif

void TraceState::SetClientCallback(HCTraceCallback* callback) noexcept
{
    m_clientCallback = callback;
}

HCTraceCallback* TraceState::GetClientCallback() const noexcept
{
    return m_clientCallback;
}

uint64_t TraceState::GetTimestamp() const noexcept
{
    auto now = std::chrono::high_resolution_clock::now();
    auto nowMS = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_initTime.load());
    return nowMS.count();
}

TraceState& GetTraceState() noexcept
{
    static TraceState state;
    return state;
}

STDAPI_(void) HCTraceInit() noexcept
{
    GetTraceState().Init();
}

STDAPI_(void) HCTraceCleanup() noexcept
{
    GetTraceState().Cleanup();
}

ThreadIdInfo& GetThreadIdInfo() noexcept
{
    static ThreadIdInfo info{};
    return info;
}

WriteToDebuggerInfo& GetWriteToDebuggerInfo() noexcept
{
    static WriteToDebuggerInfo info{};
    return info;
}

STDAPI HCTraceSetPlatformCallbacks(
    _In_ HCTracePlatformThisThreadIdCallback* threadIdCallback,
    _In_opt_ void* threadIdContext,
    _In_ HCTracePlatformWriteMessageToDebuggerCallback* writeToDebuggerCallback,
    _In_opt_ void* writeToDebuggerContext
) noexcept
{
    if (GetTraceState().IsSetup())
    {
        return E_HC_ALREADY_INITIALISED;
    }

    auto& tidi = GetThreadIdInfo();
    tidi.callback = threadIdCallback;
    tidi.context = threadIdContext;

    auto& wtdi = GetWriteToDebuggerInfo();
    wtdi.callback = writeToDebuggerCallback;
    wtdi.context = writeToDebuggerContext;

    return S_OK;
}
