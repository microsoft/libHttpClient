#pragma once

#include <httpClient/trace.h>
#include <mutex>

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
    bool SetClientCallback(HCTraceCallback* callback) noexcept;
    void RemoveClientCallback(HCTraceCallback* callback) noexcept;
    bool HasClientCallbacks() const noexcept;
    void InvokeClientCallbacks(
        char const* areaName,
        HCTraceLevel level,
        uint64_t threadId,
        uint64_t timestamp,
        char const* message
    ) noexcept;
    uint64_t GetTimestamp() const noexcept;
    bool GetEtwEnabled() const noexcept;
#if HC_PLATFORM_IS_MICROSOFT
    void SetEtwEnabled(_In_ bool enabled) noexcept;
#endif

private:
    std::mutex m_clientCallbacksMutex;
    std::atomic<HCTraceCallback*> m_clientCallbacks[MAX_TRACE_CLIENTS]{};
    std::atomic<uint32_t> m_tracingClients{ 0 };
    std::atomic<std::chrono::high_resolution_clock::time_point> m_initTime
    {
        std::chrono::high_resolution_clock::time_point{}
    };
    std::atomic<bool> m_traceToDebugger{ false };
    std::atomic<bool> m_etwEnabled{ false };
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
