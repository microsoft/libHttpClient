#include "pch.h"
#include "MainPage.xaml.h"
#include <httpClient\httpClient.h>

#include <regex>
#include <sstream>
#include <iomanip>
#include <codecvt>

using namespace HttpTestApp;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

static HttpTestApp::MainPage^ g_MainPage;
static Windows::UI::Core::CoreDispatcher^ s_dispatcher;
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

#define TICKS_PER_SECOND 10000000i64


static std::string to_utf8string(const std::wstring& input)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> utfConverter;
    return utfConverter.to_bytes(input);
}

static std::wstring to_utf16string(const std::string& input)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> utfConverter;
    return utfConverter.from_bytes(input);
}

void CALLBACK HandleAsyncQueueCallback(
    _In_opt_ void* context,
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

MainPage::MainPage()
{
    s_dispatcher = this->Dispatcher;
    g_MainPage = this;
    g_stopRequestedHandle.set(CreateEvent(nullptr, true, false, nullptr));
    g_workReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    g_completionReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    InitializeComponent();
    HCInitialize(nullptr);
    HCSettingsSetTraceLevel(HCTraceLevel::Information);

    XTaskQueueCreate(
        XTaskQueueDispatchMode::Manual,
        XTaskQueueDispatchMode::Manual,
        &m_queue);
    XTaskQueueRegisterMonitor(m_queue, nullptr, HandleAsyncQueueCallback, &m_callbackToken);

    StartBackgroundThread();

    TextboxURL->Text = L"http://www.bing.com";
    TextboxHeaders->Text = L"User-Agent: XboxServicesAPI; x-xbl-contract-version: 1";
    TextboxMethod->Text = L"GET";
    TextboxTimeout->Text = L"120";
    TextboxRequestString->Text = L"";
}

MainPage::~MainPage()
{
    HCCleanup();
    XTaskQueueCloseHandle(m_queue);
}

std::vector<std::vector<std::string>> ExtractHeadersFromHeadersString(std::string headersList)
{
    std::vector<std::vector<std::string>> headers;
    std::regex headersListToken("; ");
    std::sregex_token_iterator iterHeadersList(headersList.begin(), headersList.end(), headersListToken, -1);
    std::sregex_token_iterator endHeadersList;
    std::vector<std::string> headerList(iterHeadersList, endHeadersList);
    for (auto header : headerList)
    {
        std::regex headerToken(": ");
        std::sregex_token_iterator iterHeader(header.begin(), header.end(), headerToken, -1);
        std::sregex_token_iterator endHeader;
        std::vector<std::string> valueKeyPair(iterHeader, endHeader);
        if (valueKeyPair.size() == 2)
        {
            headers.push_back(valueKeyPair);
        }
    }

    return headers;
}

DWORD WINAPI background_thread_proc(LPVOID lpParam)
{
    HANDLE hEvents[3] =
    {
        g_workReadyHandle.get(),
        g_completionReadyHandle.get(),
        g_stopRequestedHandle.get()
    };

    XTaskQueueHandle queue = static_cast<XTaskQueueHandle>(lpParam);

    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(3, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // work ready
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Work, 0))
            {
                // If we processed work, check for more
                SetEvent(g_workReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completed 
            // Typically completions should be dispatched on the game thread, but
            // for this simple XAML app we're doing it here
            if (XTaskQueueDispatch(queue, XTaskQueuePort::Completion, 0))
            {
                // If we processed a completion, check for more
                SetEvent(g_completionReadyHandle.get());
            }
            break;

        default:
            stop = true;
            break;
        }
    }

    return 0;
}

void HttpTestApp::MainPage::StopBackgroundThread()
{
    if (m_hBackgroundThread != nullptr)
    {
        SetEvent(g_stopRequestedHandle.get());
        WaitForSingleObject(m_hBackgroundThread, INFINITE);
        CloseHandle(m_hBackgroundThread);
        m_hBackgroundThread = nullptr;
    }
}

void HttpTestApp::MainPage::StartBackgroundThread()
{
    if (m_hBackgroundThread == nullptr)
    {
        m_hBackgroundThread = CreateThread(nullptr, 0, background_thread_proc, m_queue, 0, nullptr);
    }
}

std::vector<std::vector<std::string>> ExtractAllHeaders(_In_ HCCallHandle call)
{
    uint32_t numHeaders = 0;
    HCHttpCallResponseGetNumHeaders(call, &numHeaders);

    std::vector< std::vector<std::string> > headers;
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const CHAR* str;
        const CHAR* str2;
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

void HttpTestApp::MainPage::UpdateXamlUI(
    _In_ uint32_t errCode,
    _In_ std::string errMessage,
    _In_ uint32_t statusCode,
    _In_ std::string responseString,
    _In_ std::vector<std::vector<std::string>> headers
    )
{
    std::stringstream ss;
    ss << "Network Error: " << errMessage << " [Code: " << errCode << "]\r\n";
    ss << "StatusCode: " << statusCode << "\r\n";
    for (size_t i = 0; i < headers.size(); i++)
    {
        ss << "Header[" << i << "]: " << headers[i][0] << ": " << headers[i][1] << "\r\n";
    }
    ss << "Response: " << responseString << "\r\n";

    std::string strText = ss.str();

    // This callback is called from background thread
    // and we must set the XAML text on the UI thread, so use CoreDispatcher to get it there
    s_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
        ref new Windows::UI::Core::DispatchedHandler([strText]()
    {
        g_MainPage->LogTextBox->Text = ref new Platform::String(to_utf16string(strText).c_str());
    }));
}

void CALLBACK TraceCallback(
    _In_z_ const char* areaName,
    _In_ HCTraceLevel level,
    _In_ uint64_t threadId,
    _In_ uint64_t timestamp,
    _In_z_ const char* message
)
{
    // Hook up to your own tracing.  For example:
    // OutputDebugStringA(message);
}


void HttpTestApp::MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    std::string requestBody = to_utf8string(TextboxRequestString->Text->Data());
    std::string requestHeaders = to_utf8string(TextboxHeaders->Text->Data());
    std::string requestMethod = to_utf8string(TextboxMethod->Text->Data());
    std::string requestUrl = to_utf8string(TextboxURL->Text->Data());
    std::string timeout = to_utf8string(TextboxTimeout->Text->Data());
    uint32_t timeoutInt = atoi(timeout.c_str());

    HCTraceSetClientCallback(TraceCallback);
    HCTraceSetTraceToDebugger(true);

    HCCallHandle call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, requestMethod.c_str(), requestUrl.c_str());
    HCHttpCallRequestSetTimeoutWindow(call, timeoutInt);

    HCHttpCallRequestSetRequestBodyString(call, requestBody.c_str());
    HCHttpCallRequestSetRetryAllowed(call, RetryAllowedCheckbox->IsChecked->Value);
    auto headers = ExtractHeadersFromHeadersString(requestHeaders);
    for (auto header : headers)
    {
        std::string headerName = header[0];
        std::string headerValue = header[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str(), true);
    }

    XAsyncBlock* asyncBlock = new XAsyncBlock;
    ZeroMemory(asyncBlock, sizeof(XAsyncBlock));
    asyncBlock->context = call;
    asyncBlock->queue = m_queue;
    asyncBlock->callback = [](XAsyncBlock* asyncBlock)
    {
        const CHAR* str;
        HRESULT errCode = S_OK;
        uint32_t platErrCode = 0;
        uint32_t statusCode = 0;
        std::string responseString;
        std::string errMessage;

        HCCallHandle call = static_cast<HCCallHandle>(asyncBlock->context);
        HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
        HCHttpCallResponseGetStatusCode(call, &statusCode);
        HCHttpCallResponseGetResponseString(call, &str);
        if (str != nullptr) responseString = str;
        std::vector<std::vector<std::string>> headers = ExtractAllHeaders(call);

        HCHttpCallCloseHandle(call);
        UpdateXamlUI(errCode, errMessage, statusCode, responseString, headers);
        delete asyncBlock;
    };

    HCHttpCallPerformAsync(call, asyncBlock);
}


