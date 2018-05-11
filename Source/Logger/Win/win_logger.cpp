#include "pch.h"

#include <httpClient/trace.h>

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message)
{
    UNREFERENCED_PARAMETER(areaName);
    UNREFERENCED_PARAMETER(traceLevel);
    OutputDebugStringA(message);
}