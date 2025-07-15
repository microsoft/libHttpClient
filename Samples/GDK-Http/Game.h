//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"


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
// provides a game loop.
class Game final : public DX::IDeviceNotify
{
public:

    Game() noexcept(false);
    ~Game();

    Game(Game&&) = default;
    Game& operator= (Game&&) = default;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(HWND window, int width, int height);

    // Basic game loop
    void Tick();

    // IDeviceNotify
    void OnDeviceLost() override;
    void OnDeviceRestored() override;

    // Messages
    void OnActivated();
    void OnDeactivated();
    void OnSuspending();
    void OnResuming();
    void OnWindowMoved();
    void OnDisplayChange();
    void OnWindowSizeChanged(int width, int height);

    // Properties
    void GetDefaultSize( int& width, int& height ) const noexcept;

private:

    void Update(DX::StepTimer const& timer);
    void Render();

    void Clear();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

    static DWORD WINAPI BackgroundThreadEntry(LPVOID lpParam);
    static void CALLBACK HandleAsyncQueueCallback(void* context, XTaskQueueHandle queue, XTaskQueuePort type);

    void StartBackgroundThreads();
    void ShutdownBackgroundThreads();
    void PerformHttpCall(std::string url, std::string requestBody, bool isJson, std::string filePath, bool enableGzipCompression, bool enableGzipResponseCompression, bool customWrite);

    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer                           m_timer;

    win32_handle                            m_stopRequestedHandle;
    win32_handle                            m_workReadyHandle;
    win32_handle                            m_completionReadyHandle;
    win32_handle                            m_exampleTaskDone;

    const DWORD                             m_targetNumThreads{ 2 };
    HANDLE                                  m_hActiveThreads[10] = { 0 };
    DWORD                                   m_defaultIdealProcessor = 0;
    DWORD                                   m_numActiveThreads = 0;

    XTaskQueueHandle                        m_queue{};
    XTaskQueueRegistrationToken             m_callbackToken{};

    size_t                                  m_httpCallsCompleted{ 0 };
    bool                                    m_httpCallPending{ false };
};
