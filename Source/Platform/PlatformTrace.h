#pragma once

#include <httpClient/trace.h>

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

//------------------------------------------------------------------------------
// Platform specific tracing functionality
//------------------------------------------------------------------------------

uint64_t GetThreadId() noexcept;
void TraceToDebugger(char const* area, HCTraceLevel level, char const* message) noexcept;

NAMESPACE_XBOX_HTTP_CLIENT_END