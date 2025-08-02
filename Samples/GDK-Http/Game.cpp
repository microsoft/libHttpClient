//
// Game.cpp
//

#include "pch.h"
#include "Game.h"
#include <httpClient/httpClient.h>
// #include <json.h>  // Removed - using GDK's libHttpClient instead of custom build

extern void ExitGame() noexcept;

using namespace DirectX;

using Microsoft::WRL::ComPtr;


std::vector<std::vector<std::string>> ExtractAllHeaders(_In_ HCCallHandle call)
{
    uint32_t numHeaders = 0;
    HCHttpCallResponseGetNumHeaders(call, &numHeaders);

    std::vector< std::vector<std::string> > headers;
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const char* str;
        const char* str2;
        std::string headerName;
        std::string headerValue;
        HCHttpCallResponseGetHeaderAtIndex(call, i, &str, &str2);
        if (str != nullptr) headerName = str;
        if (str2 != nullptr) headerValue = str2;
        std::vector<std::string> header;
        header.push_back(headerName);
        header.push_back(headerValue);

        headers.push_back(header);
    }

    return headers;
}

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
                // If we executed work, set our event again to check next time.
                SetEvent(game.m_workReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completed 
            // Typically completions should be dispatched on the game thread, but
            // for this simple XAML app we're doing it here
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
            {
                // If we executed a completion set our event again to check next time
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
    _In_ XTaskQueuePort type
)
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
    m_stopRequestedHandle.set(CreateEvent(nullptr, true, false, nullptr));
    m_workReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    m_completionReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    m_exampleTaskDone.set(CreateEvent(nullptr, false, false, nullptr));

    for (uint32_t i = 0; i < m_targetNumThreads; i++)
    {
        m_hActiveThreads[i] = CreateThread(nullptr, 0, BackgroundThreadEntry, this, 0, nullptr);
        if (m_defaultIdealProcessor != MAXIMUM_PROCESSORS)
        {
            if (m_hActiveThreads[i] != nullptr)
            {
                SetThreadIdealProcessor(m_hActiveThreads[i], m_defaultIdealProcessor);
            }
        }
    }

    m_numActiveThreads = m_targetNumThreads;
}

void Game::ShutdownBackgroundThreads()
{
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

struct SampleHttpCallAsyncContext
{
    Game& game;
    HCCallHandle call;
    bool isJson;
    std::string filePath;
    std::vector<uint8_t> response;
    bool isCustom;
};

HRESULT CustomResponseBodyWrite(HCCallHandle /*call*/, const uint8_t* source, size_t bytesAvailable, void* context)
{
    SampleHttpCallAsyncContext* customContext = static_cast<SampleHttpCallAsyncContext*> (context);
    customContext->response.insert(customContext->response.end(), source, source + bytesAvailable);
    return S_OK;
}

void Game::PerformHttpCall(std::string url, std::string requestBody, bool isJson, std::string filePath, bool enableGzipCompression, bool enableGzipResponseCompression, bool customWrite)
{
    std::string method = "GET";
    bool retryAllowed = true;
    std::vector<std::vector<std::string>> headers;
    std::vector<std::string> header;

    if (enableGzipResponseCompression)
    {
        method = "POST";
        header.push_back("X-SecretKey");
        header.push_back("");
        headers.push_back(header);

        header.clear();
        header.push_back("Accept-Encoding");
        header.push_back("application/gzip");
        headers.push_back(header);

        header.clear();
        header.push_back("Content-Type");
        header.push_back("application/json");
        headers.push_back(header);
    }

    header.clear();
    header.push_back("TestHeader");
    header.push_back("1.0");
    headers.push_back(header);

    HCCallHandle call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, method.c_str(), url.c_str());
    HCHttpCallRequestSetRequestBodyString(call, requestBody.c_str());
    HCHttpCallRequestSetRetryAllowed(call, retryAllowed);

    if (enableGzipResponseCompression)
    {
        HCHttpCallResponseSetGzipCompressed(call, true);
    }

    if (enableGzipCompression)
    {
        HCHttpCallRequestEnableGzipCompression(call, HCCompressionLevel::Medium);
    }

    for (auto& h : headers)
    {
        std::string headerName = h[0];
        std::string headerValue = h[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str(), true);
    }

    printf_s("Calling %s %s\r\n", method.c_str(), url.c_str());

    std::vector<uint8_t> buffer;
    SampleHttpCallAsyncContext* hcContext = new SampleHttpCallAsyncContext{ *this, call, isJson, filePath, buffer, customWrite };
    XAsyncBlock* asyncBlock = new XAsyncBlock;
    ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
    asyncBlock->context = hcContext;
    asyncBlock->queue = m_queue;
    if (customWrite)
    {
        HCHttpCallResponseBodyWriteFunction customWriteWrapper = [](HCCallHandle call, const uint8_t* source, size_t bytesAvailable, void* context) -> HRESULT
        {
            return CustomResponseBodyWrite(call, source, bytesAvailable, context);
        };

        HCHttpCallResponseSetResponseBodyWriteFunction(call, customWriteWrapper, asyncBlock->context);
    }
    asyncBlock->callback = [](XAsyncBlock* asyncBlock)
    {
        const char* str;
        HRESULT networkErrorCode = S_OK;
        uint32_t platErrCode = 0;
        uint32_t statusCode = 0;
        std::string responseString;
        std::string errMessage;

        SampleHttpCallAsyncContext* hcContext = static_cast<SampleHttpCallAsyncContext*>(asyncBlock->context);
        HCCallHandle call = hcContext->call;
        bool isJson = hcContext->isJson;
        std::string filePath = hcContext->filePath;
        std::vector<uint8_t> readBuffer = hcContext->response;
        readBuffer.push_back('\0');
        bool customWriteUsed = hcContext->isCustom;
        HRESULT hr = XAsyncGetStatus(asyncBlock, false);
        if (FAILED(hr))
        {
            // This should be a rare error case when the async task fails
            printf_s("Couldn't get HTTP call object 0x%0.8x\r\n", hr);
            HCHttpCallCloseHandle(call);
            return;
        }

        HCHttpCallResponseGetNetworkErrorCode(call, &networkErrorCode, &platErrCode);
        HCHttpCallResponseGetStatusCode(call, &statusCode);
        if (!customWriteUsed)
        {
            HCHttpCallResponseGetResponseString(call, &str);
            if (str != nullptr) responseString = str;

            if (!isJson)
            {
                size_t bufferSize = 0;
                HCHttpCallResponseGetResponseBodyBytesSize(call, &bufferSize);
                uint8_t* buffer = new uint8_t[bufferSize];
                size_t bufferUsed = 0;
                HCHttpCallResponseGetResponseBodyBytes(call, bufferSize, buffer, &bufferUsed);
                HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
                DWORD bufferWritten = 0;
                WriteFile(hFile, buffer, (DWORD)bufferUsed, &bufferWritten, NULL);
                CloseHandle(hFile);
                delete[] buffer;
            }
        }

        std::vector<std::vector<std::string>> headers = ExtractAllHeaders(call);
        HCHttpCallCloseHandle(call);

        printf_s("HTTP call done\r\n");
        printf_s("Network error code: 0x%0.8x\r\n", networkErrorCode);
        printf_s("HTTP status code: %d\r\n", statusCode);

        int i = 0;
        for (auto& header : headers)
        {
            printf_s("Header[%d] '%s'='%s'\r\n", i, header[0].c_str(), header[1].c_str());
            i++;
        }

        if (!customWriteUsed)
        {
            if (isJson && responseString.length() > 0)
            {
                // Returned string starts with a BOM strip it out.
                uint8_t BOM[] = { 0xef, 0xbb, 0xbf, 0x0 };
                if (responseString.find(reinterpret_cast<char*>(BOM)) == 0)
                {
                    responseString = responseString.substr(3);
                }
                
                // Simple JSON validation - just check if it looks like JSON
                if (responseString.front() == '{' && responseString.back() == '}')
                {
                    printf_s("Response appears to be valid JSON object\r\n");
                }
                else if (responseString.front() == '[' && responseString.back() == ']')
                {
                    printf_s("Response appears to be valid JSON array\r\n");
                }
                else
                {
                    printf_s("Response doesn't appear to be well-formed JSON\r\n");
                }
            }

            printf_s("Response string:\r\n%s\r\n", responseString.c_str());
        }
        else
        {
            readBuffer.push_back('\0');
            const char* responseStr = reinterpret_cast<const char*>(readBuffer.data());
            printf_s("Response string: %s\n", responseStr);
        }

        SetEvent(hcContext->game.m_exampleTaskDone.get());
        delete asyncBlock;
    };


    HCHttpCallPerformAsync(call, asyncBlock);
}

Game::Game() noexcept(false)
{
    m_deviceResources = std::make_unique<DX::DeviceResources>();
    // TODO: Provide parameters for swapchain format, depth/stencil format, and backbuffer count.
    //   Add DX::DeviceResources::c_AllowTearing to opt-in to variable rate displays.
    //   Add DX::DeviceResources::c_EnableHDR for HDR10 display.
    //   Add DX::DeviceResources::c_ReverseDepth to optimize depth buffer clears for 0 instead of 1.
    m_deviceResources->RegisterDeviceNotify(this);
}

Game::~Game()
{
    if (m_deviceResources)
    {
        m_deviceResources->WaitForGpu();
    }
}

// Initialize the Direct3D resources required to run.
void Game::Initialize(HWND window, int width, int height)
{
    m_deviceResources->SetWindow(window, width, height);

    m_deviceResources->CreateDeviceResources();
    CreateDeviceDependentResources();

    m_deviceResources->CreateWindowSizeDependentResources();
    CreateWindowSizeDependentResources();

    // TODO: Change the timer settings if you want something other than the default variable timestep mode.
    // e.g. for 60 FPS fixed timestep update logic, call:
    /*
    m_timer.SetFixedTimeStep(true);
    m_timer.SetTargetElapsedSeconds(1.0 / 60);
    */

    HRESULT hr = HCInitialize(nullptr);
    assert(SUCCEEDED(hr));
    UNREFERENCED_PARAMETER(hr);

    XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &m_queue);
    XTaskQueueRegisterMonitor(m_queue, this, HandleAsyncQueueCallback, &m_callbackToken);
    HCTraceSetTraceToDebugger(true);
    StartBackgroundThreads();
}

#pragma region Frame Update
// Executes the basic game loop.
void Game::Tick()
{
    m_timer.Tick([&]()
    {
        Update(m_timer);
    });

    Render();
}

// Updates the world.
void Game::Update(DX::StepTimer const& /*timer*/)
{
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Update");

    if (m_httpCallPending)
    {
        // See if call completed
        auto waitResult = WaitForSingleObject(m_exampleTaskDone.get(), 10);
        if (waitResult == WAIT_OBJECT_0)
        {
            m_httpCallsCompleted++;
            m_httpCallPending = false;
        }
    }
    else
    {
        m_httpCallPending = true;
        std::string url;

        switch (m_httpCallsCompleted)
        {
        case 0:
        {
            url = "https://raw.githubusercontent.com/Microsoft/libHttpClient/master/Samples/Win32-Http/TestContent.json";
            PerformHttpCall(
                url, 
                "{\"test\":\"value\"},{\"test2\":\"value\"},{\"test3\":\"value\"},{\"test4\":\"value\"},{\"test5\":\"value\"},{\"test6\":\"value\"},{\"test7\":\"value\"}",
                true, 
                "",
                false,
                false,
                false
            );
        }
        break;

        case 1:
        {
            url = "https://github.com/Microsoft/libHttpClient/raw/master/Samples/XDK-Http/Assets/SplashScreen.png";
            PerformHttpCall(url, "", false, "SplashScreen.png", false, false, false);
        }
        break;

        case 2:
        {
            url = "https://80996.playfabapi.com/authentication/GetEntityToken";
            PerformHttpCall(url, "", false, "", false, true, false);
        }
        break;

        case 3:
        {
            url = "https://80996.playfabapi.com/authentication/GetEntityToken";
            PerformHttpCall(url, "", false, "", false, true, true);
        }
        break;

        default:
        {
            // All HttpCalls complete
            ExitGame();
        }
        }
    }

    PIXEndEvent();
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

    PIXEndEvent(commandList);

    // Show the new frame.
    PIXBeginEvent(m_deviceResources->GetCommandQueue(), PIX_COLOR_DEFAULT, L"Present");
    m_deviceResources->Present();
    PIXEndEvent(m_deviceResources->GetCommandQueue());
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
// Message handlers
void Game::OnActivated()
{
    // TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
    // TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
    // TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
    m_timer.ResetElapsedTime();

    // TODO: Game is being power-resumed (or returning from minimize).
}

void Game::OnWindowMoved()
{
    auto const r = m_deviceResources->GetOutputSize();
    m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnDisplayChange()
{
    m_deviceResources->UpdateColorSpace();
}

void Game::OnWindowSizeChanged(int width, int height)
{
    if (!m_deviceResources->WindowSizeChanged(width, height))
        return;

    CreateWindowSizeDependentResources();

    // TODO: Game window is being resized.
}

// Properties
void Game::GetDefaultSize(int& width, int& height) const noexcept
{
    // TODO: Change to desired default window size (note minimum size is 320x200).
    width = 800;
    height = 600;
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
    auto device = m_deviceResources->GetD3DDevice();

    // Check Shader Model 6 support
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = { D3D_SHADER_MODEL_6_0 };
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel)))
        || (shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_0))
    {
#ifdef _DEBUG
        OutputDebugStringA("ERROR: Shader Model 6.0 is not supported!\n");
#endif
        throw std::runtime_error("Shader Model 6.0 is not supported!");
    }

    // TODO: Initialize device dependent objects here (independent of window size).
    device;
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
    // TODO: Initialize windows-size dependent objects here.
}

void Game::OnDeviceLost()
{
    // TODO: Add Direct3D resource cleanup here.
}

void Game::OnDeviceRestored()
{
    CreateDeviceDependentResources();

    CreateWindowSizeDependentResources();
}
#pragma endregion
