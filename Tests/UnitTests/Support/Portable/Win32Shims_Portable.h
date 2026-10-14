// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Minimal emulation of the handful of Win32 APIs the unit tests use (events, threads, waits,
// tick counts, ...), so Tests/UnitTests/Tests/*.cpp build unchanged on non-Microsoft platforms.
// Test-only: correctness over speed (all waitables share one mutex/condition variable).

#pragma once

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0x00000000L
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 0x00000102L
#endif
#ifndef WAIT_FAILED
#define WAIT_FAILED 0xFFFFFFFF
#endif
#ifndef ERROR_INVALID_STATE
#define ERROR_INVALID_STATE 5023L
#endif
#ifndef HRESULT_FROM_WIN32
#define HRESULT_FROM_WIN32(x) __HRESULT_FROM_WIN32(x)
#endif
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef int BOOL;
typedef char CHAR;
typedef uint32_t UINT;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef wchar_t WCHAR;
typedef const wchar_t* PCWSTR;
typedef const wchar_t* LPCWSTR;
typedef const char* PCSTR;
typedef const char* LPCSTR;

#ifndef CopyMemory
#define CopyMemory(destination, source, length) memcpy((destination), (source), (length))
#endif

namespace hcunittest { namespace win32shim {

struct Waitable
{
    bool manualReset{ true };
    bool signaled{ false };
};

struct SharedWaitState
{
    std::mutex lock;
    std::condition_variable cv;
};

inline SharedWaitState& WaitState()
{
    static SharedWaitState* s_state = new SharedWaitState{}; // leaked: detached threads may signal during exit
    return *s_state;
}

// What a HANDLE points to. Threads keep their own reference to the waitable so closing the
// handle (which detaches, like Win32) does not free state the thread still signals on exit.
struct HandleObject
{
    std::shared_ptr<Waitable> waitable;
    std::thread thread;
};

inline Waitable* GetWaitable(HANDLE h) { return h ? static_cast<HandleObject*>(h)->waitable.get() : nullptr; }

inline DWORD& ThreadIdSlot()
{
    thread_local DWORD t_id = 0;
    return t_id;
}

inline DWORD NextThreadId()
{
    static std::atomic<DWORD> s_next{ 1 };
    return s_next++;
}

}} // namespace hcunittest::win32shim

inline HANDLE CreateEvent(const void* /*attributes*/, BOOL manualReset, BOOL initialState, const void* /*name*/)
{
    auto handle = new hcunittest::win32shim::HandleObject{ std::make_shared<hcunittest::win32shim::Waitable>(), std::thread{} };
    handle->waitable->manualReset = manualReset != FALSE;
    handle->waitable->signaled = initialState != FALSE;
    return handle;
}

inline BOOL SetEvent(HANDLE h)
{
    auto& state = hcunittest::win32shim::WaitState();
    {
        std::lock_guard<std::mutex> guard{ state.lock };
        hcunittest::win32shim::GetWaitable(h)->signaled = true;
    }
    state.cv.notify_all();
    return TRUE;
}

inline BOOL ResetEvent(HANDLE h)
{
    std::lock_guard<std::mutex> guard{ hcunittest::win32shim::WaitState().lock };
    hcunittest::win32shim::GetWaitable(h)->signaled = false;
    return TRUE;
}

inline BOOL CloseHandle(HANDLE h)
{
    auto handle = static_cast<hcunittest::win32shim::HandleObject*>(h);
    if (handle == nullptr) { return FALSE; }
    if (handle->thread.joinable()) { handle->thread.detach(); }
    delete handle;
    return TRUE;
}

inline DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds)
{
    using namespace hcunittest::win32shim;
    for (DWORD i = 0; i < count; ++i) { if (GetWaitable(handles[i]) == nullptr) { return WAIT_FAILED; } }

    DWORD signaledIndex = 0;
    auto ready = [&]()
    {
        for (DWORD i = 0; i < count; ++i)
        {
            bool signaled = GetWaitable(handles[i])->signaled;
            if (!waitAll && signaled) { signaledIndex = i; return true; }
            if (waitAll && !signaled) { return false; }
        }
        return waitAll != FALSE;
    };

    auto& state = WaitState();
    std::unique_lock<std::mutex> lock{ state.lock };
    if (milliseconds == INFINITE) { state.cv.wait(lock, ready); }
    else if (!state.cv.wait_for(lock, std::chrono::milliseconds(milliseconds), ready)) { return WAIT_TIMEOUT; }

    for (DWORD i = 0; i < count; ++i)
    {
        Waitable* w = GetWaitable(handles[i]);
        if ((waitAll || i == signaledIndex) && !w->manualReset) { w->signaled = false; }
    }
    return WAIT_OBJECT_0 + (waitAll ? 0 : signaledIndex);
}

inline DWORD WaitForSingleObject(HANDLE h, DWORD milliseconds)
{
    return WaitForMultipleObjects(1, &h, TRUE, milliseconds);
}

inline DWORD GetCurrentThreadId()
{
    DWORD& id = hcunittest::win32shim::ThreadIdSlot();
    if (id == 0) { id = hcunittest::win32shim::NextThreadId(); }
    return id;
}

inline HANDLE CreateThread(const void* /*attributes*/, size_t /*stackSize*/, DWORD (*start)(void*), void* parameter, DWORD /*flags*/, DWORD* threadId)
{
    using namespace hcunittest::win32shim;
    DWORD id = NextThreadId();
    if (threadId) { *threadId = id; }
    auto waitable = std::make_shared<Waitable>(); // manual reset, signaled when the thread exits
    auto handle = new HandleObject{ waitable, std::thread{} };
    handle->thread = std::thread([start, parameter, waitable, id]()
    {
        ThreadIdSlot() = id;
        start(parameter);
        {
            std::lock_guard<std::mutex> guard{ WaitState().lock };
            waitable->signaled = true;
        }
        WaitState().cv.notify_all();
    });
    return handle;
}

inline void Sleep(DWORD milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

inline uint64_t GetTickCount64()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

template<typename T> inline T InterlockedIncrement(T volatile* value) { return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST); }
template<typename T> inline T InterlockedDecrement(T volatile* value) { return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST); }

inline int _wcsicmp(const wchar_t* a, const wchar_t* b)
{
    for (; *a && std::towlower(*a) == std::towlower(*b); ++a, ++b) {}
    return static_cast<int>(std::towlower(*a)) - static_cast<int>(std::towlower(*b));
}

inline DWORD GetEnvironmentVariableW(const wchar_t* name, wchar_t* buffer, DWORD size)
{
    std::string narrowName;
    for (; *name; ++name) { narrowName.push_back(static_cast<char>(*name)); }
    const char* value = std::getenv(narrowName.c_str());
    if (value == nullptr) { return 0; }
    DWORD length = static_cast<DWORD>(std::strlen(value));
    if (length + 1 > size) { return length + 1; }
    for (DWORD i = 0; i <= length; ++i) { buffer[i] = static_cast<wchar_t>(static_cast<unsigned char>(value[i])); }
    return length;
}
