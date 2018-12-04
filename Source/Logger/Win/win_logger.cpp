#include "pch.h"

#include <httpClient/trace.h>

uint64_t Internal_ThisThreadId() noexcept
{
    return GetCurrentThreadId();
}

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message)
{
    UNREFERENCED_PARAMETER(areaName);
    UNREFERENCED_PARAMETER(traceLevel);
    OutputDebugStringA(message);
}