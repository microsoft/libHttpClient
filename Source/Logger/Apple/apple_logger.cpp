#include "pch.h"

#include <httpClient/trace.h>

uint64_t Internal_ThisThreadId() noexcept
{
    return reinterpret_cast<uint64_t>(pthread_self());
}

void Internal_HCTraceMessage(const char* areaName, HCTraceLevel traceLevel, const char* message) noexcept
{
    printf("%s", message);
}
