//
// Game.cpp
//
// libHttpClient GDK console suspend-hang repro (bug 63050439).
//
// On a real Xbox console the GDK build of libHttpClient auto-selects the Curl provider (xCurl); see
// Source/Platform/GDK/PlatformComponents_GDK.cpp ("Detected Xbox console, using XCurl"). On Desktop/PC it
// uses the WinHttp provider instead, which already handles PLM suspend
// (winhttp_provider.cpp: RegisterAppStateChangeNotification -> Suspend() -> CloseAllConnections). The Curl
// provider has NO suspend handling. This sample keeps an HTTP request in-flight and, on suspend, parks the
// title-owned task queue that drives curl_multi_perform - reproducing the quiesce/watchdog hang.
//

#include "pch.h"
#include "Game.h"

extern void ExitGame() noexcept;

using namespace DirectX;

using Microsoft::WRL::ComPtr;

// REPRO: a long-running request that is kept in-flight so that a platform suspend overlaps an active xCurl
// request. Any endpoint that holds its response long enough works; swap this if httpbin is not reachable in
// your lab (e.g. point it at a large download or a local slow endpoint).
static const char* const c_reproUrl = "https://httpbin.org/delay/30";

#pragma region Async task-queue pump
DWORD WINAPI Game::BackgroundThreadEntry(LPVOID lpParam)
{
    Game& game{ *static_cast<Game*>(lpParam) };

    HANDLE hEvents[3] =
    {
        game.m_workReadyHandle.get(),
        game.m_completionReadyHandle.get(),
        game.m_stopRequestedHandle.get()
    };

    XTaskQueueHandle queue;
    XTaskQueueDuplicateHandle(game.m_queue, &queue);

    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(3, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // work ready
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0))
            {
                SetEvent(game.m_workReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completion ready
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
            {
                SetEvent(game.m_completionReadyHandle.get());
            }
            break;

        default:
            stop = true;
            break;
        }
    }

    XTaskQueueCloseHandle(queue);
    return 0;
}

void CALLBACK Game::HandleAsyncQueueCallback(
    _In_ void* context,
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort type)
{
    UNREFERENCED_PARAMETER(queue);

    Game& game = *static_cast<Game*>(context);

    switch (type)
    {
    case XTaskQueuePort::Work:
        SetEvent(game.m_workReadyHandle.get());
        break;

    case XTaskQueuePort::Completion:
        SetEvent(game.m_completionReadyHandle.get());
        break;
    }
}

void Game::StartBackgroundThreads()
{
    if (m_stopRequestedHandle.get() == nullptr)
    {
        m_stopRequestedHandle.set(CreateEvent(nullptr, true, false, nullptr));
        m_workReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
        m_completionReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
        m_exampleTaskDone.set(CreateEvent(nullptr, false, false, nullptr));
    }

    for (uint32_t i = 0; i < m_targetNumThreads; i++)
    {
        m_hActiveThreads[i] = CreateThread(nullptr, 0, BackgroundThreadEntry, this, 0, nullptr);
        if (m_defaultIdealProcessor != MAXIMUM_PROCESSORS && m_hActiveThreads[i] != nullptr)
        {
            SetThreadIdealProcessor(m_hActiveThreads[i], m_defaultIdealProcessor);
        }
    }

    m_numActiveThreads = m_targetNumThreads;
}

void Game::ShutdownBackgroundThreads()
{
    if (m_numActiveThreads == 0)
    {
        return;
    }

    SetEvent(m_stopRequestedHandle.get());
    DWORD dwResult = WaitForMultipleObjectsEx(m_numActiveThreads, m_hActiveThreads, true, INFINITE, false);
    if (dwResult >= WAIT_OBJECT_0 && dwResult <= WAIT_OBJECT_0 + m_numActiveThreads - 1)
    {
        for (DWORD i = 0; i < m_numActiveThreads; i++)
        {
            CloseHandle(m_hActiveThreads[i]);
            m_hActiveThreads[i] = nullptr;
        }
        m_numActiveThreads = 0;
        ResetEvent(m_stopRequestedHandle.get());
    }
}
#pragma endregion

#pragma region HTTP call
struct SampleHttpCallContext
{
    Game& game;
    HCCallHandle call;
};

void Game::PerformHttpCall(std::string url)
{
    HCCallHandle call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, "GET", url.c_str());
    HCHttpCallRequestSetRetryAllowed(call, true);
    HCHttpCallRequestSetHeader(call, "TestHeader", "1.0", true);

    printf_s("Calling GET %s\r\n", url.c_str());

    SampleHttpCallContext* hcContext = new SampleHttpCallContext{ *this, call };
    XAsyncBlock* asyncBlock = new XAsyncBlock;
    ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
    asyncBlock->context = hcContext;
    asyncBlock->queue = m_queue;
    asyncBlock->callback = [](XAsyncBlock* asyncBlock)
    {
        SampleHttpCallContext* hcContext = static_cast<SampleHttpCallContext*>(asyncBlock->context);
        HCCallHandle call = hcContext->call;

        HRESULT hr = XAsyncGetStatus(asyncBlock, false);
        if (SUCCEEDED(hr))
        {
            uint32_t statusCode = 0;
            HRESULT networkErrorCode = S_OK;
            uint32_t platErrCode = 0;
            HCHttpCallResponseGetNetworkErrorCode(call, &networkErrorCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            printf_s("HTTP call done. network error 0x%0.8x, status %u\r\n", networkErrorCode, statusCode);
        }
        else
        {
            printf_s("HTTP call failed 0x%0.8x\r\n", hr);
        }

        HANDLE done = hcContext->game.m_exampleTaskDone.get();
        HCHttpCallCloseHandle(call);
        delete hcContext;
        delete asyncBlock;
        if (done) SetEvent(done);
    };

    HCHttpCallPerformAsync(call, asyncBlock);
}
#pragma endregion

Game::Game() noexcept(false) :
    m_frame(0)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    m_deviceResources->SetClearColor(Colors::CornflowerBlue);
}

Game::~Game()
{
    ShutdownBackgroundThreads();

    if (m_queue)
    {
        XTaskQueueUnregisterMonitor(m_queue, m_callbackToken);
        XTaskQueueCloseHandle(m_queue);
        m_queue = nullptr;
    }

    HCCleanup();
}

// Initialize the Direct3D resources required to run, plus libHttpClient.
void Game::Initialize(HWND window)
{
    m_deviceResources->SetWindow(window);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    HRESULT hr = HCInitialize(nullptr);
    assert(SUCCEEDED(hr));
    UNREFERENCED_PARAMETER(hr);

    // Manual/Manual queue: the title owns the pump (see StartBackgroundThreads). On console the Curl provider
    // drives curl_multi_perform via callbacks dispatched on this queue's Work port.
    XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &m_queue);
    XTaskQueueRegisterMonitor(m_queue, this, HandleAsyncQueueCallback, &m_callbackToken);
    HCTraceSetTraceToDebugger(true);
    StartBackgroundThreads();
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Frame %llu", m_frame);

    m_deviceResources->WaitForOrigin();

    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    Render();

    PIXEndEvent();
    m_frame++;
}

// Updates the world.
void Game::Update(DX::StepTimer const& timer)
{
    PIXScopedEvent(PIX_COLOR_DEFAULT, L"Update");

    UNREFERENCED_PARAMETER(timer);

    // REPRO: keep a long-running HTTP call in-flight at all times. When the platform suspends the title,
    // OnSuspending() stops pumping the task queue (as a real title does when it parks its work queue on
    // suspend). libHttpClient's Curl provider drives curl_multi_perform via callbacks on that queue, so the
    // in-flight request stalls, and xCurl's suspend handler (Curl_multi::WaitForActiveHandles) then blocks
    // forever waiting for it to drain -> the title fails to suspend (quiesce hang / watchdog termination).
    if (m_httpCallPending)
    {
        if (WaitForSingleObject(m_exampleTaskDone.get(), 0) == WAIT_OBJECT_0)
        {
            m_httpCallsCompleted++;
            m_httpCallPending = false;
        }
    }
    else
    {
        m_httpCallPending = true;
        printf_s("REPRO: starting long-running call #%zu to %s (suspend the title while this is in-flight)\r\n",
            m_httpCallsCompleted, c_reproUrl);
        PerformHttpCall(c_reproUrl);
    }
}
#pragma endregion

#pragma region Frame Render
// Draws the scene.
void Game::Render()
{
    // Don't try to render anything before the first Update.
    if (m_timer.GetFrameCount() == 0)
    {
        return;
    }

    // Prepare the command list to render a new frame.
    m_deviceResources->Prepare();
    Clear();

    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Render");

    // TODO: Add your rendering code here.
    commandList;

    PIXEndEvent(commandList);

    // Show the new frame.
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Present");
    m_deviceResources->Present();

    PIXEndEvent();
}

// Helper method to clear the back buffers.
void Game::Clear()
{
    auto commandList = m_deviceResources->GetCommandList();
    PIXBeginEvent(commandList, PIX_COLOR_DEFAULT, L"Clear");

    // Clear the views.
    auto const rtvDescriptor = m_deviceResources->GetRenderTargetView();
    auto const dsvDescriptor = m_deviceResources->GetDepthStencilView();

    commandList->OMSetRenderTargets(1, &rtvDescriptor, FALSE, &dsvDescriptor);
    commandList->ClearRenderTargetView(rtvDescriptor, Colors::CornflowerBlue, 0, nullptr);
    commandList->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set the viewport and scissor rect.
    auto const viewport = m_deviceResources->GetScreenViewport();
    auto const scissorRect = m_deviceResources->GetScissorRect();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissorRect);

    PIXEndEvent(commandList);
}
#pragma endregion

#pragma region Message Handlers
// Occurs when the game is being suspended.
void Game::OnSuspending()
{
    // REPRO: a real title parks its work queue during suspend. Simulate that by stopping the threads that pump
    // our XTaskQueue. libHttpClient's Curl provider drives curl_multi_perform via callbacks on this queue, so
    // once it stops being pumped the in-flight request stalls - and xCurl's suspend handler
    // (Curl_multi::WaitForActiveHandles) blocks forever waiting for it to drain, so the title fails to suspend.
    //
    // NOTE: libHttpClient's WinHttp provider (Desktop/PC) handles suspend itself
    // (RegisterAppStateChangeNotification + WinHttpProvider::Suspend). The Curl provider (Xbox console / xCurl)
    // does not - that is the bug.
    printf_s("REPRO: OnSuspending - stopping the task queue pump (a title parks its queue during suspend)\r\n");
    ShutdownBackgroundThreads();

    m_deviceResources->Suspend();
}

// Occurs when the game is resuming.
void Game::OnResuming()
{
    m_deviceResources->Resume();
    m_timer.ResetElapsedTime();

    StartBackgroundThreads();
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    // TODO: Initialize device dependent objects here (independent of window size).
    device;
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
    // TODO: Initialize windows-size dependent objects here.
}
#pragma endregion
