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

namespace
{

template<size_t SIZE>
int stprintf_s(char(&buffer)[SIZE], _Printf_format_string_ char const* format ...) noexcept
{
    va_list varArgs{};
    va_start(varArgs, format);
    auto result = vsnprintf(buffer, SIZE, format, varArgs);
    va_end(varArgs);
    return result;
}

int stprintf_s(char* buffer, size_t size, _Printf_format_string_ char const* format ...) noexcept
{
    va_list varArgs{};
    va_start(varArgs, format);
    auto result = vsnprintf(buffer, size, format, varArgs);
    va_end(varArgs);
    return result;
}

template<size_t SIZE>
int vstprintf_s(char(&buffer)[SIZE], _Printf_format_string_ char const* format, va_list varArgs) noexcept
{
    return vsnprintf(buffer, SIZE, format, varArgs);
}

//------------------------------------------------------------------------------
// Trace implementation
//------------------------------------------------------------------------------

void TraceMessageToDebugger(
    char const* areaName,
    HCTraceLevel level,
    uint64_t threadId,
    uint64_t timestamp,
    char const* message
) noexcept
{
    if (!GetTraceState().GetTraceToDebugger())
        return;

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

    static size_t const BUFFER_SIZE = 4096;

    std::time_t  timeTInSec = static_cast<std::time_t>(timestamp / 1000);
    uint32_t     fractionMSec = static_cast<uint32_t>(timestamp % 1000);
    std::tm      fmtTime = {};

#if HC_PLATFORM_IS_MICROSOFT
    localtime_s(&fmtTime, &timeTInSec);
#else
    localtime_r(&timeTInSec, &fmtTime);
#endif

    char outputBuffer[BUFFER_SIZE] = {};
    // [threadId][level][time][area] message
    auto written = stprintf_s(outputBuffer, "[%04llX][%s][%02d:%02d:%02d.%03u][%s] %s",
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
    written = std::min(written, static_cast<int>(BUFFER_SIZE - 3));
    auto remaining = BUFFER_SIZE - written;

    // Print new line
    auto written2 = stprintf_s(outputBuffer + written, remaining, "\r\n");
    if (written2 <= 0)
    {
        return;
    }

    Internal_HCTraceMessage(areaName, level, outputBuffer);
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

}

STDAPI_(void) HCTraceSetTraceToDebugger(_In_ bool traceToDebugger) noexcept
{
    GetTraceState().SetTraceToDebugger(traceToDebugger);
}

STDAPI_(void) HCTraceSetClientCallback(_In_opt_ HCTraceCallback* callback) noexcept
{
    GetTraceState().SetClientCallback(callback);
}

STDAPI_(void) HCTraceImplMessage(
    struct HCTraceImplArea const* area,
    HCTraceLevel level,
    _Printf_format_string_ char const* format,
    ...
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
    if (GetTraceState().GetClientCallback() == nullptr && !GetTraceState().GetTraceToDebugger())
    {
        return;
    }

    auto timestamp = GetTraceState().GetTimestamp();
    auto threadId = Internal_ThisThreadId();

    char message[4096] = {};

    va_list varArgs{};
    va_start(varArgs, format);
    auto result = vstprintf_s(message, format, varArgs);
    va_end(varArgs);

    if (result < 0)
    {
        return;
    }

    TraceMessageToDebugger(area->Name, level, threadId, timestamp, message);
    TraceMessageToClient(area->Name, level, threadId, timestamp, message);
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

void HCTraceInit() noexcept
{
    GetTraceState().Init();
}

void HCTraceCleanup() noexcept
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
