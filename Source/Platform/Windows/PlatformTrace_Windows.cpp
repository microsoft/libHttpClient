#include "pch.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

uint64_t GetThreadId() noexcept
{
    return GetCurrentThreadId();
}

void TraceToDebugger(const char* areaName, HCTraceLevel traceLevel, const char* message) noexcept
{
    UNREFERENCED_PARAMETER(areaName);
    UNREFERENCED_PARAMETER(traceLevel);
    OutputDebugStringA(message);
}

NAMESPACE_XBOX_HTTP_CLIENT_END