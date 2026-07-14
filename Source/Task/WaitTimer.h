#pragma once

#include <atomic>

namespace OS
{
    using WaitTimerCallback = void(_In_opt_ void*);

    class WaitTimerImpl;

    // Source-internal test seam for deterministic STL WaitTimer lifetime
    // regressions. It is not exported as part of the public libHttpClient API.
    struct WaitTimerTestHooks
    {
        virtual void WaitTimerImplDestructionStarted() noexcept {}
        virtual bool BeforeTimerInvoke() noexcept { return true; }
        virtual void BeforeTimerQueueRetirement() noexcept {}
        virtual bool WaitTimerOperationLoaded(bool) noexcept { return true; }
    };

    void WaitTimerSetTestHooks(_In_opt_ WaitTimerTestHooks* hooks) noexcept;

    // A wait timer holds a single timeout expressed as a monotonic due time.
    // Calling Start will reset any pending timeout.
    class WaitTimer
    {
    public:
        WaitTimer() noexcept;
        ~WaitTimer() noexcept;

        HRESULT Initialize(_In_opt_ void* context, _In_ WaitTimerCallback* callback) noexcept;
        void Terminate() noexcept;

        // Arms the one-shot timer for the provided monotonic due time.
        void Start(_In_ uint64_t dueTime) noexcept;
        void Cancel() noexcept;

        // Returns the current monotonic time used for delayed-callback
        // ordering and stale-timer validation.
        uint64_t GetCurrentTime() noexcept;

        // Returns a monotonic due time msFromNow milliseconds in the future.
        uint64_t GetDueTime(_In_ uint32_t msFromNow) noexcept;

    private:
        DefaultUnnamedMutex m_mutex;
        std::atomic<WaitTimerImpl*> m_impl;
    };
}
