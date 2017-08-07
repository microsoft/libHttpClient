#include "pch.h"
#include "MainPage.xaml.h"
#include "httpClient\httpClient.h"

#include <regex>
#include <sstream>

#include <winsock2.h> 
#include "curl/curl.h"
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
static bool g_manualThreadingCheckbox = false;
HANDLE g_pendingHandle = nullptr;
HANDLE g_stopHandle = nullptr;

#define TICKS_PER_SECOND 10000000i64

void HC_CALLING_CONV PerformCallWithCurl(
    _In_ HC_CALL_HANDLE call,
    _In_ HC_TASK_HANDLE taskHandle
    );


MainPage::MainPage()
{
    s_dispatcher = this->Dispatcher;
    g_MainPage = this;
    g_stopHandle = CreateEvent(nullptr, false, false, nullptr);
    InitializeComponent();
    HCGlobalInitialize();
    StartBackgroundThread();
    curl_global_init(CURL_GLOBAL_ALL);
    HCGlobalSetHttpCallPerformFunction(PerformCallWithCurl);

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

std::vector<std::vector<std::wstring>> ExtractHeadersFromHeadersString(std::wstring headersList)
{
    std::vector<std::vector<std::wstring>> headers;
    std::wregex headersListToken(L"; ");
    std::wsregex_token_iterator iterHeadersList(headersList.begin(), headersList.end(), headersListToken, -1);
    std::wsregex_token_iterator endHeadersList;
    std::vector<std::wstring> headerList(iterHeadersList, endHeadersList);
    for (auto header : headerList)
    {
        std::wregex headerToken(L": ");
        std::wsregex_token_iterator iterHeader(header.begin(), header.end(), headerToken, -1);
        std::wsregex_token_iterator endHeader;
        std::vector<std::wstring> valueKeyPair(iterHeader, endHeader);
        if (valueKeyPair.size() == 2)
        {
            headers.push_back(valueKeyPair);
        }
    }

    return headers;
}

void HttpTestApp::MainPage::DispatcherTimer_Tick(Platform::Object^ sender, Platform::Object^ e)
{
    uint32_t taskGroupId = 0;
    HCTaskProcessNextCompletedTask(taskGroupId);
}

DWORD WINAPI background_thread_proc(LPVOID lpParam)
{
    HANDLE hEvents[2] =
    {
        g_pendingHandle,
        g_stopHandle
    };

    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(2, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // pending async op ready
            HCTaskProcessNextPendingTask();
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
        SetEvent(g_stopHandle);
        WaitForSingleObject(m_hBackgroundThread, INFINITE);
        CloseHandle(m_hBackgroundThread);
        m_hBackgroundThread = nullptr;
        m_timer = nullptr;
    }
}

void HttpTestApp::MainPage::StartBackgroundThread()
{
    if (m_hBackgroundThread == nullptr)
    {
        g_pendingHandle = HCTaskGetPendingHandle();
        m_hBackgroundThread = CreateThread(nullptr, 0, background_thread_proc, nullptr, 0, nullptr);

        m_timer = ref new DispatcherTimer;
        m_timer->Tick += ref new EventHandler<Object^>(this, &HttpTestApp::MainPage::DispatcherTimer_Tick);

        TimeSpan ts;
        ts.Duration = TICKS_PER_SECOND / 60;
        m_timer->Interval = ts;

        m_timer->Start();
    }
}

std::vector<std::vector<std::wstring>> ExtractAllHeaders(_In_ HC_CALL_HANDLE call)
{
    uint32_t numHeaders = 0;
    HCHttpCallResponseGetNumHeaders(call, &numHeaders);

    std::vector< std::vector<std::wstring> > headers;
    for (uint32_t i = 0; i < numHeaders; i++)
    {
        const WCHAR* str;
        const WCHAR* str2;
        std::wstring headerName;
        std::wstring headerValue;
        HCHttpCallResponseGetHeaderAtIndex(call, i, &str, &str2);
        if (str != nullptr) headerName = str;
        if (str2 != nullptr) headerValue = str2;
        std::vector<std::wstring> header;
        header.push_back(headerName);
        header.push_back(headerValue);

        headers.push_back(header);
    }

    return headers;
}

void HttpTestApp::MainPage::UpdateXamlUI(
    _In_ uint32_t errCode,
    _In_ std::wstring errMessage,
    _In_ uint32_t statusCode,
    _In_ std::wstring responseString,
    _In_ std::vector<std::vector<std::wstring>> headers
    )
{
    std::wstringstream ss;
    ss << L"Network Error: " << errMessage << L" [Code: " << errCode << L"]\r\n";
    ss << L"StatusCode: " << statusCode << L"\r\n";
    for (int i = 0; i < headers.size(); i++)
    {
        ss << L"Header[" << i << L"]: " << headers[i][0] << L": " << headers[i][1] << L"\r\n";
    }
    ss << L"Response: " << responseString << L"\r\n";

    std::wstring strText = ss.str();

    if (!g_manualThreadingCheckbox)
    {
        // If !g_manualThreadingCheckbox, then this callback is called from background thread
        // and we must set the XAML text on the UI thread, so use CoreDispatcher to get it there
        s_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
            ref new Windows::UI::Core::DispatchedHandler([strText]()
        {
            g_MainPage->LogTextBox->Text = ref new Platform::String(strText.c_str());
        }));
    }
    else
    {
        // If g_manualThreadingCheckbox, then this callback is called from thread 
        // that called HCThreadProcessCompletedAsyncOp() which in this sample 
        // is the UI thread

        // We must set the XAML text on the UI thread, so no need to use CoreDispatcher here
        g_MainPage->LogTextBox->Text = ref new Platform::String(strText.c_str());
    }
}

void HttpTestApp::MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    HC_CALL_HANDLE call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, TextboxMethod->Text->Data(), TextboxURL->Text->Data());
    HCHttpCallRequestSetRequestBodyString(call, TextboxRequestString->Text->Data());
    HCHttpCallRequestSetRetryAllowed(call, RetryAllowedCheckbox->IsChecked->Value);
    auto headers = ExtractHeadersFromHeadersString(TextboxHeaders->Text->Data());
    for (auto header : headers)
    {
        std::wstring headerName = header[0];
        std::wstring headerValue = header[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str());
    }

    uint32_t taskGroupId = 0;
    HCHttpCallPerform(taskGroupId, call, nullptr,
        [](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
        {
            const WCHAR* str;
            uint32_t errCode = 0;
            uint32_t statusCode = 0;
            std::wstring responseString;
            std::wstring errMessage;

            HCHttpCallResponseGetErrorCode(call, &errCode);
            HCHttpCallResponseGetStatusCode(call, &statusCode);
            HCHttpCallResponseGetResponseString(call, &str);
            if (str != nullptr) responseString = str;
            HCHttpCallResponseGetErrorMessage(call, &str);
            if (str != nullptr) errMessage = str;
            std::vector<std::vector<std::wstring>> headers = ExtractAllHeaders(call);

            HCHttpCallCleanup(call);

            UpdateXamlUI(errCode, errMessage, statusCode, responseString, headers);
        });
}

