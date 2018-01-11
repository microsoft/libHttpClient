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
win32_handle g_pendingReadyHandle;
win32_handle g_completeReadyHandle;

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

HC_RESULT libhttpclient_event_handler(
    _In_opt_ void* context,
    _In_ HC_TASK_EVENT_TYPE eventType,
    _In_ HC_TASK_HANDLE taskHandle
    )
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(taskHandle);

    switch (eventType)
    {
    case HC_TASK_EVENT_TYPE::HC_TASK_EVENT_PENDING:
        // For size, you can do:
        // uint64_t sizeOfPendingQueue = HCTaskGetPendingTaskQueueSize(HC_SUBSYSTEM_ID_GAME);
        SetEvent(g_pendingReadyHandle.get());
        break;

    case HC_TASK_EVENT_TYPE::HC_TASK_EVENT_EXECUTE_STARTED:
        break;

    case HC_TASK_EVENT_TYPE::HC_TASK_EVENT_EXECUTE_COMPLETED:
        // For size, you can do:
        // uint64_t sizeOfPendingQueue = HCTaskGetCompletedTaskQueueSize(HC_SUBSYSTEM_ID_GAME, 0);
        SetEvent(g_completeReadyHandle.get());
        break;
    }

    return HC_OK;
}

MainPage::MainPage()
{
    s_dispatcher = this->Dispatcher;
    g_MainPage = this;
    g_stopRequestedHandle.set(CreateEvent(nullptr, true, false, nullptr));
    g_pendingReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    g_completeReadyHandle.set(CreateEvent(nullptr, false, false, nullptr));
    InitializeComponent();
    HCGlobalInitialize();
    HCSettingsSetLogLevel(HC_LOG_LEVEL::LOG_VERBOSE);

    HC_TASK_EVENT_HANDLE eventHandle;
    HCAddTaskEventHandler(
        HC_SUBSYSTEM_ID_GAME,
        nullptr,
        libhttpclient_event_handler,
        &eventHandle
        );

    StartBackgroundThread();

    TextboxURL->Text = L"http://www.bing.com";
    TextboxHeaders->Text = L"User-Agent: XboxServicesAPI; x-xbl-contract-version: 1";
    TextboxMethod->Text = L"GET";
    TextboxTimeout->Text = L"120";
    TextboxRequestString->Text = L"";
}

MainPage::~MainPage()
{
    HCGlobalCleanup();
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
        g_pendingReadyHandle.get(),
        g_completeReadyHandle.get(),
        g_stopRequestedHandle.get()
    };

    bool stop = false;
    uint64_t taskGroupId = 0;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(3, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // pending 
            HCTaskProcessNextPendingTask(HC_SUBSYSTEM_ID_GAME);

            // If there's more pending tasks, then set the event to process them
            if (HCTaskGetPendingTaskQueueSize(HC_SUBSYSTEM_ID_GAME) > 0)
            {
                SetEvent(g_pendingReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completed 
            HCTaskProcessNextCompletedTask(HC_SUBSYSTEM_ID_GAME, 0);

            // If there's more completed tasks, then set the event to process them
            if (HCTaskGetCompletedTaskQueueSize(HC_SUBSYSTEM_ID_GAME, taskGroupId) > 0)
            {
                SetEvent(g_completeReadyHandle.get());
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
        m_hBackgroundThread = CreateThread(nullptr, 0, background_thread_proc, nullptr, 0, nullptr);
    }
}

std::vector<std::vector<std::string>> ExtractAllHeaders(_In_ HC_CALL_HANDLE call)
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
    ss << L"Network Error: " << errMessage << L" [Code: " << errCode << L"]\r\n";
    ss << L"StatusCode: " << statusCode << L"\r\n";
    for (int i = 0; i < headers.size(); i++)
    {
        ss << L"Header[" << i << L"]: " << headers[i][0] << L": " << headers[i][1] << L"\r\n";
    }
    ss << L"Response: " << responseString << L"\r\n";

    std::string strText = ss.str();

    // This callback is called from background thread
    // and we must set the XAML text on the UI thread, so use CoreDispatcher to get it there
    s_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
        ref new Windows::UI::Core::DispatchedHandler([strText]()
    {
        g_MainPage->LogTextBox->Text = ref new Platform::String(to_utf16string(strText).c_str());
    }));
}

void HttpTestApp::MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    std::string requestBody = to_utf8string(TextboxRequestString->Text->Data());
    std::string requestHeaders = to_utf8string(TextboxHeaders->Text->Data());
    std::string requestMethod = to_utf8string(TextboxMethod->Text->Data());
    std::string requestUrl = to_utf8string(TextboxURL->Text->Data());

    HC_CALL_HANDLE call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, requestMethod.c_str(), requestUrl.c_str());

    HCHttpCallRequestSetRequestBodyString(call, requestBody.c_str());
    HCHttpCallRequestSetRetryAllowed(call, RetryAllowedCheckbox->IsChecked->Value);
    auto headers = ExtractHeadersFromHeadersString(requestHeaders);
    for (auto header : headers)
    {
        std::string headerName = header[0];
        std::string headerValue = header[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str());
    }

    HC_SUBSYSTEM_ID taskSubsystemId = HC_SUBSYSTEM_ID_GAME;
    uint64_t taskGroupId = 0;
    HCHttpCallPerform(call, nullptr, taskSubsystemId, taskGroupId, nullptr,
        [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            const CHAR* str;
            HC_RESULT errCode = HC_OK;
            uint32_t platErrCode = 0;
            uint32_t statusCode = 0;
            std::string responseString;
            std::string errMessage;

            HCHttpCallResponseGetNetworkErrorCode(call, &errCode, &platErrCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &str);
            if (str != nullptr) responseString = str;
            std::vector<std::vector<std::string>> headers = ExtractAllHeaders(call);

            HCHttpCallCloseHandle(call);

            UpdateXamlUI(errCode, errMessage, statusCode, responseString, headers);
        });
}

