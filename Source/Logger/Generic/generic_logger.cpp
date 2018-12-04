#include "pch.h"

#include "../trace_internal.h"

namespace
{

struct ThreadIdInfo
{
    HCTracePlatformThisThreadIdCallback* callback;
    void* context;
};

struct WriteToDebuggerInfo
{
    HCTracePlatformWriteMessageToDebuggerCallback* callback;
    void* context;
};

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

}

HRESULT HCTraceSetPlatformCallbacks(
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

