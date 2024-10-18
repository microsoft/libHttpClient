#pragma once

#include <httpClient/trace.h>

#define MAX_TRACE_CLIENTS 10

class TraceState
{
public:
    TraceState() noexcept;
    void Init() noexcept;
    void Cleanup() noexcept;
    bool IsSetup() const noexcept;
    bool GetTraceToDebugger() noexcept;
    void SetTraceToDebugger(_In_ bool traceToDebugger) noexcept;
    void SetClientCallback(HCTraceCallback* callback) noexcept;
    void RemoveClientCallback(HCTraceCallback* callback) noexcept;
    uint64_t GetTimestamp() const noexcept;
    bool GetEtwEnabled() const noexcept;
#if HC_PLATFORM_IS_MICROSOFT
    void SetEtwEnabled(_In_ bool enabled) noexcept;
#endif

    HCTraceCallback* clientCallbacks[MAX_TRACE_CLIENTS]{};

private:
    std::atomic<uint32_t> m_tracingClients{ 0 };
    std::atomic<std::chrono::high_resolution_clock::time_point> m_initTime
    {
        std::chrono::high_resolution_clock::time_point{}
    };
    bool m_traceToDebugger = false;
    bool m_etwEnabled = false;
};

TraceState& GetTraceState() noexcept;

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

ThreadIdInfo& GetThreadIdInfo() noexcept;
WriteToDebuggerInfo& GetWriteToDebuggerInfo() noexcept;
