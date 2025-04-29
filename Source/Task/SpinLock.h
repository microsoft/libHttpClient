#pragma once
#include <algorithm>
#include <atomic>
#include <thread>

#if defined(_WIN32) || defined(__WINDOWS__) 
#include <intrin.h>
#elif (defined(_M_IX86) || defined(_M_X64))
#include <x86intrin.h>
#endif

//
// SpinLock: A spinlock implementation based on std::atomic_flag that
// prevents CPU starvation. SpinLock can be used as a RAII wrapper around
// an external flag, or its static Lock API may be used to lock an
// external flag.
//
class SpinLock
{
public:
    SpinLock(_In_ std::atomic_flag& flag) : m_lock(flag)
    {
        Lock(m_lock);
    }

    ~SpinLock()
    {
        m_lock.clear(std::memory_order_release);
    }

    static void Lock(_In_ std::atomic_flag& flag)
    {
        unsigned int backoff = 1;
        constexpr unsigned int maxBackoff = 1024;

        while (flag.test_and_set(std::memory_order_acquire)) {
            for (unsigned int i = 0; i < backoff; ++i) {
                cpu_pause();
            }
            
            // Exponential backoff with cap. If we are over the cap yield
            // this thread.

            backoff = backoff << 1;
            if (backoff >= maxBackoff)
            {
                backoff = maxBackoff;
                std::this_thread::yield();
            }
        }
    }

private:
    std::atomic_flag& m_lock;

    static inline void cpu_pause()
    {
#if defined(_M_IX86) || defined(_M_X64)
        // x86/x64: Tells CPU we're spinning
        // Reduces energy consumption
        // Helps avoid memory order violations
        // Maps to PAUSE instruction
        _mm_pause();
#elif defined(_M_ARM) || defined(_M_ARM64)
        // ARM: Yields to other hardware threads
        __yield();
#else
        // Other platforms: No specific CPU hint
        // Still helps due to compiler barrier
#endif
    }
};