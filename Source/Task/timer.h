#pragma once

using PlatformTimerCallback = void(_In_opt_ void*);

#if defined (__APPLE__)
struct TimerWrapper;
struct TargetWrapper;
#endif

// this class is implemented in platform specific files
class PlatformTimer
{
public:
    PlatformTimer(void* context, PlatformTimerCallback* callback) noexcept;
    ~PlatformTimer() noexcept;

    bool Valid() const noexcept;

    void Start(uint32_t delayInMs) noexcept;
    void Cancel() noexcept;

    void* const m_context;
    PlatformTimerCallback* const m_callback;

private:
#if defined(_WIN32)
    static void CALLBACK TimerCallback(
        _In_ PTP_CALLBACK_INSTANCE,
        _In_ void* context,
        _In_ PTP_TIMER timer
    ) noexcept;
    PTP_TIMER m_timer;
//#elif defined(__APPLE__)
//    TimerWrapper* m_timerWrapper;
//    TargetWrapper* m_targetWrapper;
#else
    friend class TimerQueue;
    void OnDeadline() noexcept;

    bool const m_valid;
#endif
};
