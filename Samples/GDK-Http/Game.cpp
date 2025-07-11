//
// Game.cpp
//

#include "pch.h"
#include "Game.h"
#include <httpClient/httpClient.h>
#include <json_cpp\json.h>

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

win32_handle g_stopRequestedHandle;
win32_handle g_workReadyHandle;
win32_handle g_completionReadyHandle;
win32_handle g_exampleTaskDone;

DWORD g_targetNumThreads = 2;
HANDLE g_hActiveThreads[10] = { 0 };
DWORD g_defaultIdealProcessor = 0;
DWORD g_numActiveThreads = 0;

XTaskQueueHandle g_queue;
XTaskQueueRegistrationToken g_callbackToken;


DWORD WINAPI background_thread_proc(LPVOID /*lpParam*/)
{
    HANDLE hEvents[3] =
    {
        g_workReadyHandle.get(),
        g_completionReadyHandle.get(),
        g_stopRequestedHandle.get()
    };

    XTaskQueueHandle queue;
    XTaskQueueDuplicateHandle(g_queue, &queue);

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
                SetEvent(g_workReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completed 
            // Typically completions should be dispatched on the game thread, but
            // for this simple XAML app we're doing it here
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
            {
                // If we executed a completion set our event again to check next time
                SetEvent(g_completionReadyHandle.get());
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

void CALLBACK HandleAsyncQueueCallback(
    _In_ void* context,
    _In_ XTaskQueueHandle queue,
    _In_ XTaskQueuePort type
)
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(queue);

    switch (type)
    {
    case XTaskQueuePort::Work:
        SetEvent(g_workReadyHandle.get());
        break;

    case XTaskQueuePort::Completion:
        SetEvent(g_completionReadyHandle.get());
        break;
    }
}

void StartBackgroundThread()
{
    g_stopRequestedHandle.set(CreateEvent(nullptr, true, false, nullptr));
    g_workReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    g_completionReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    g_exampleTaskDone.set(CreateEvent(nullptr, false, false, nullptr));

    for (uint32_t i = 0; i < g_targetNumThreads; i++)
    {
        g_hActiveThreads[i] = CreateThread(nullptr, 0, background_thread_proc, nullptr, 0, nullptr);
        if (g_defaultIdealProcessor != MAXIMUM_PROCESSORS)
        {
            if (g_hActiveThreads[i] != nullptr)
            {
                SetThreadIdealProcessor(g_hActiveThreads[i], g_defaultIdealProcessor);
            }
        }
    }

    g_numActiveThreads = g_targetNumThreads;
}

void ShutdownActiveThreads()
{
    SetEvent(g_stopRequestedHandle.get());
    DWORD dwResult = WaitForMultipleObjectsEx(g_numActiveThreads, g_hActiveThreads, true, INFINITE, false);
    if (dwResult >= WAIT_OBJECT_0 && dwResult <= WAIT_OBJECT_0 + g_numActiveThreads - 1)
    {
        for (DWORD i = 0; i < g_numActiveThreads; i++)
        {
            CloseHandle(g_hActiveThreads[i]);
            g_hActiveThreads[i] = nullptr;
        }
        g_numActiveThreads = 0;
        ResetEvent(g_stopRequestedHandle.get());
    }
}

struct SampleHttpCallAsyncContext
{
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

void DoHttpCall(std::string url, std::string requestBody, bool isJson, std::string filePath, bool enableGzipCompression, bool enableGzipResponseCompression, bool customWrite)
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
    SampleHttpCallAsyncContext* hcContext = new SampleHttpCallAsyncContext{ call, isJson, filePath, buffer, customWrite };
    XAsyncBlock* asyncBlock = new XAsyncBlock;
    ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
    asyncBlock->context = hcContext;
    asyncBlock->queue = g_queue;
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
                web::json::value json = web::json::value::parse(utility::conversions::to_string_t(responseString));;
            }

            printf_s("Response string:\r\n%s\r\n", responseString.c_str());
        }
        else
        {
            readBuffer.push_back('\0');
            const char* responseStr = reinterpret_cast<const char*>(readBuffer.data());
            printf_s("Response string: %s\n", responseStr);
        }

        SetEvent(g_exampleTaskDone.get());
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

    XTaskQueueCreate(XTaskQueueDispatchMode::Manual, XTaskQueueDispatchMode::Manual, &g_queue);
    XTaskQueueRegisterMonitor(g_queue, nullptr, HandleAsyncQueueCallback, &g_callbackToken);
    HCTraceSetTraceToDebugger(true);
    StartBackgroundThread();
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
void Game::Update(DX::StepTimer const& timer)
{
    PIXBeginEvent(PIX_COLOR_DEFAULT, L"Update");

    static bool callStarted = false;
    if (!callStarted)
    {
        std::string url1 = "https://raw.githubusercontent.com/Microsoft/libHttpClient/master/Samples/Win32-Http/TestContent.json";
        DoHttpCall(url1, "{\"test\":\"value\"},{\"test2\":\"value\"},{\"test3\":\"value\"},{\"test4\":\"value\"},{\"test5\":\"value\"},{\"test6\":\"value\"},{\"test7\":\"value\"}", true, "", false, false, false);

        callStarted = true;
    }

    // See if call completed
    auto waitResult = WaitForSingleObject(g_exampleTaskDone.get(), 10);
    if (waitResult == WAIT_OBJECT_0)
    {
        ExitGame();
    }

    float elapsedTime = float(timer.GetElapsedSeconds());

    // TODO: Add your game logic here.
    elapsedTime;

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
