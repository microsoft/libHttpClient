#include "pch.h"

#include <httpClient/trace.h>

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message)
{
    printf(message);
}