#include "pch.h"
#include <httpClient/trace.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

uint64_t GetThreadId() noexcept
{
    return reinterpret_cast<uint64_t>(pthread_self());
}

void TraceToDebugger(const char* areaName, HCTraceLevel traceLevel, const char* message) noexcept
{
    printf("%s", message);
}

NAMESPACE_XBOX_HTTP_CLIENT_END
