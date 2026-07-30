//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"


// RAII wrapper for a Win32 event handle used by the async task-queue pump.
class win32_handle
{
public:
    win32_handle() : m_handle(nullptr)
    {
    }

    ~win32_handle()
    {
        if (m_handle != nullptr) CloseHandle(m_handle);
        m_handle = nullptr;
    }

    void set(HANDLE handle)
    {
        m_handle = handle;
    }

    HANDLE get() { return m_handle; }

private:
    HANDLE m_handle;
};

// A basic game implementation that creates a D3D12 device and
// provides a render loop, plus the libHttpClient suspend-hang repro.
class Game
{
public:

    Game() noexcept(false);
    ~Game();

    Game(Game&&) = default;
    Game& operator= (Game&&) = default;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(HWND window);

    // Basic render loop
    void Tick();

    // Messages
    void OnSuspending();
    void OnResuming();
    void OnConstrained() {}
    void OnUnConstrained() {}

    // Properties
    bool RequestHDRMode() const noexcept { return m_deviceResources ? (m_deviceResources->GetDeviceOptions() & DX::DeviceResources::c_EnableHDR) != 0 : false; }

private:

    void Update(DX::StepTimer const& timer);
    void Render();

    void Clear();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

    // libHttpClient repro helpers.
    static DWORD WINAPI BackgroundThreadEntry(LPVOID lpParam);
    static void CALLBACK HandleAsyncQueueCallback(void* context, XTaskQueueHandle queue, XTaskQueuePort type);

    void StartBackgroundThreads();
    void ShutdownBackgroundThreads();
    void PerformHttpCall(std::string url);

    // Device resources.
    std::unique_ptr<DX::DeviceResources>        m_deviceResources;

    // Rendering loop timer.
    uint64_t                                    m_frame;
    DX::StepTimer                               m_timer;

    // Async task-queue pump (a title-owned work queue that libHttpClient's
    // Curl provider drives via curl_multi_perform callbacks).
    win32_handle                                m_stopRequestedHandle;
    win32_handle                                m_workReadyHandle;
    win32_handle                                m_completionReadyHandle;
    win32_handle                                m_exampleTaskDone;

    const DWORD                                 m_targetNumThreads{ 2 };
    HANDLE                                      m_hActiveThreads[10] = { 0 };
    DWORD                                       m_defaultIdealProcessor = 0;
    DWORD                                       m_numActiveThreads = 0;

    XTaskQueueHandle                            m_queue{};
    XTaskQueueRegistrationToken                 m_callbackToken{};

    size_t                                      m_httpCallsCompleted{ 0 };
    bool                                        m_httpCallPending{ false };
};
