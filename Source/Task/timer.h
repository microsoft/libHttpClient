#pragma once

#if defined(_WIN32)
#elif defined(__APPLE__)
#else
#include <mutex>
#include <thread>
#endif

using PlatformTimerCallback = void(_In_opt_ void*);

// this class is implemented in platform specific files
class PlatformTimer
{
public:
    PlatformTimer(void* context, PlatformTimerCallback* callback);
    ~PlatformTimer();

    bool Valid() const noexcept;

    void Start(uint32_t delayInMs);
    void Cancel();

    void* const m_context;
    PlatformTimerCallback* const m_callback;

private:
#if defined(_WIN32)
    static void CALLBACK TimerCallback(
        _In_ PTP_CALLBACK_INSTANCE,
        _In_ void* context,
        _In_ PTP_TIMER timer
    ) noexcept;
    PTP_TIMER m_timer = nullptr;
#elif defined(__APPLE__)

#else
    std::mutex m_mutex;
#endif
};
