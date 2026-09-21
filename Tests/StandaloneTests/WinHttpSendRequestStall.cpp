// Copyright (c) Microsoft Corporation
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// Deterministic stall injection for WinHttpProviderLockStallRepro.
//
// The repro needs WinHttpSendRequest to be slow, which is the state the reported dump captured
// (winhttp!WinHttpSendRequest -> DNS / TCP connection / socket processing). Reproducing that
// through a real network is environment-dependent: libHttpClient opens its HTTPS sessions with
// WINHTTP_FLAG_SECURE_DEFAULTS, which implies WINHTTP_FLAG_ASYNC, so WinHttpSendRequest normally
// returns immediately and only stalls when WinHTTP does synchronous work on the calling thread
// (most commonly WPAD proxy auto-discovery under WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY).
//
// libHttpClient is linked statically here, and it reaches WinHTTP through the import thunk
// __imp_WinHttpSendRequest. Defining that symbol in this object file satisfies the reference before
// the linker pulls the member out of winhttp.lib, so every libHttpClient call lands here instead.
// Nothing in the product is modified, and every other WinHTTP function still resolves normally.
//
// The interposer sleeps and then forwards to the real export, so WinHTTP still owns the request and
// the normal completion path runs. Only the duration of the call is synthetic; the lock discipline
// under test is entirely the product's own.
//
// x64 only. On x86 a WINAPI (__stdcall) import is decorated, so the symbol the call site binds to is
// __imp__WinHttpSendRequest@28 and the undecorated definition below would not intercept anything --
// winhttp.lib would satisfy the reference and this file would silently become dead code. The bug
// under test is a GDK PLM suspend stall and GDK is x64-only, so rather than maintain a decorated
// alias for architectures the scenario cannot occur on, the solution builds this project for x64
// alone and this guard makes any other configuration fail loudly instead of quietly passing.

#if !defined(_M_X64)
#error "WinHttpProviderLockStallRepro is x64-only: the __imp_WinHttpSendRequest interposer below relies on undecorated import naming. See the comment above."
#endif

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace WinHttpStall
{
    namespace
    {
        using SendRequestFn = BOOL(WINAPI*)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);

        std::atomic<unsigned long> g_stallMs{ 0 };
        std::atomic<bool> g_entered{ false };
        std::atomic<bool> g_armed{ true };

        SendRequestFn RealSendRequest() noexcept
        {
            static SendRequestFn real = []() noexcept
            {
                HMODULE winhttp = ::GetModuleHandleW(L"winhttp.dll");
                if (!winhttp)
                {
                    winhttp = ::LoadLibraryW(L"winhttp.dll");
                }
                return reinterpret_cast<SendRequestFn>(
                    winhttp ? ::GetProcAddress(winhttp, "WinHttpSendRequest") : nullptr);
            }();
            return real;
        }
    }

    void SetStall(unsigned long ms) noexcept
    {
        g_stallMs.store(ms, std::memory_order_release);
    }

    bool SendRequestWasEntered() noexcept
    {
        return g_entered.load(std::memory_order_acquire);
    }
}

extern "C"
{
    static BOOL WINAPI Stall_WinHttpSendRequest(
        HINTERNET hRequest,
        LPCWSTR pwszHeaders,
        DWORD dwHeadersLength,
        LPVOID lpOptional,
        DWORD dwOptionalLength,
        DWORD dwTotalLength,
        DWORD_PTR dwContext)
    {
        // Only the first send is stalled. Later sends (including anything teardown triggers) run at
        // full speed so the repro exits promptly.
        if (WinHttpStall::g_armed.exchange(false, std::memory_order_acq_rel))
        {
            WinHttpStall::g_entered.store(true, std::memory_order_release);

            DWORD stallMs = static_cast<DWORD>(WinHttpStall::g_stallMs.load(std::memory_order_acquire));
            if (stallMs != 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{ stallMs });
            }
        }

        auto real = WinHttpStall::RealSendRequest();
        if (!real)
        {
            ::SetLastError(ERROR_PROC_NOT_FOUND);
            return FALSE;
        }

        // Forward to the genuine export so WinHTTP takes ownership of the request and the normal
        // completion callback path runs. The stall has already happened on this thread, with
        // WinHttpProvider::m_lock held by the caller, which is the whole point of the repro.
        return real(hRequest, pwszHeaders, dwHeadersLength, lpOptional, dwOptionalLength, dwTotalLength, dwContext);
    }

    // The import thunk libHttpClient's call site binds to. Defining it here pre-empts winhttp.lib.
    decltype(&WinHttpSendRequest) __imp_WinHttpSendRequest = &Stall_WinHttpSendRequest;
}
