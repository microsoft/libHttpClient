#include "pch.h"
#include "Logger/trace_internal.h"

NAMESPACE_XBOX_HTTP_CLIENT_BEGIN

uint64_t GetThreadId() noexcept
{
    auto tidi = GetThreadIdInfo();
    if (tidi.callback)
    {
        return tidi.callback(tidi.context);
    }
    else
    {
        return static_cast<uint64_t>(-1);
    }
}

void TraceToDebugger(char const* area, HCTraceLevel level, char const* message) noexcept
{
    auto wtdi = GetWriteToDebuggerInfo();
    if (wtdi.callback)
    {
        wtdi.callback(area, level, message, wtdi.context);
    }
}

NAMESPACE_XBOX_HTTP_CLIENT_END
