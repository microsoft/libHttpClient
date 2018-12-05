#include "pch.h"

#include "../trace_internal.h"

uint64_t Internal_ThisThreadId() noexcept
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

void Internal_HCTraceMessage(char const* area, HCTraceLevel level, char const* message) noexcept
{
    auto wtdi = GetWriteToDebuggerInfo();
    if (wtdi.callback)
    {
        wtdi.callback(area, level, message, wtdi.context);
    }
}

