#pragma once

using WaitTimerCallback = void(_In_opt_ void*);

class WaitTimerImpl;

// A wait timer holds a single timeout in absolute
// time.  Calling Start will reset any pending timeout.
class WaitTimer
{
public:
    WaitTimer() noexcept;
    ~WaitTimer() noexcept;

    HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback) noexcept;

    void Start(_In_ uint64_t absoluteTime) noexcept;
    void Cancel() noexcept;

    uint64_t GetAbsoluteTime(_In_ uint32_t msFromNow) noexcept;

private:
    WaitTimerImpl* m_impl;
};
