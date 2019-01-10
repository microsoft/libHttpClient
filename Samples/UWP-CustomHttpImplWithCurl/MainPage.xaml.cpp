#include "pch.h"
#include "MainPage.xaml.h"
#include <httpClient\httpClient.h>
#include <httpClient\httpProvider.h>

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

void STDAPIVCALLTYPE PerformCallWithCurl(
    _In_ HCCallHandle call,
    _Inout_ AsyncBlock* asyncBlock
    );

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
    _In_ async_queue_handle_t queue,
    _In_ AsyncQueueCallbackType type
    )
{
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(queue);

    switch (type)
    {
    case AsyncQueueCallbackType::AsyncQueueCallbackType_Work:
        SetEvent(g_workReadyHandle.get());
        break;

    case AsyncQueueCallbackType::AsyncQueueCallbackType_Completion:
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

    uint32_t sharedAsyncQueueId = 0;
    CreateSharedAsyncQueue(
        sharedAsyncQueueId,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        &m_queue);
    RegisterAsyncQueueCallbackSubmitted(m_queue, nullptr, HandleAsyncQueueCallback, &m_callbackToken);

    StartBackgroundThread();

    curl_global_init(CURL_GLOBAL_ALL);
    HCSetHttpCallPerformFunction(PerformCallWithCurl);

    TextboxURL->Text = L"http://www.bing.com";
    TextboxHeaders->Text = L"User-Agent: XboxServicesAPI; x-xbl-contract-version: 1";
    TextboxMethod->Text = L"GET";
    TextboxTimeout->Text = L"120";
    TextboxRequestString->Text = L"";
}

MainPage::~MainPage()
{
    HCCleanup();
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

    async_queue_handle_t queue;
    uint32_t sharedAsyncQueueId = 0;
    CreateSharedAsyncQueue(
        sharedAsyncQueueId,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        AsyncQueueDispatchMode::AsyncQueueDispatchMode_Manual,
        &queue);

    bool stop = false;
    while (!stop)
    {
        DWORD dwResult = WaitForMultipleObjectsEx(3, hEvents, false, INFINITE, false);
        switch (dwResult)
        {
        case WAIT_OBJECT_0: // work ready
            DispatchAsyncQueue(queue, AsyncQueueCallbackType_Work, 0);

            if (!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Work))
            {
                // If there's more pending work, then set the event to process them
                SetEvent(g_workReadyHandle.get());
            }
            break;

        case WAIT_OBJECT_0 + 1: // completed 
            // Typically completions should be dispatched on the game thread, but
            // for this simple XAML app we're doing it here
            DispatchAsyncQueue(queue, AsyncQueueCallbackType_Completion, 0);

            if (!IsAsyncQueueEmpty(queue, AsyncQueueCallbackType_Completion))
            {
                // If there's more pending completions, then set the event to process them
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
        m_hBackgroundThread = CreateThread(nullptr, 0, background_thread_proc, nullptr, 0, nullptr);
    }
}

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
    for (size_t i = 0; i < headers.size(); i++)
    {
        ss << L"Header[" << i << L"]: " << headers[i][0] << L": " << headers[i][1] << L"\r\n";
    }
    ss << L"Response: " << responseString << L"\r\n";
   
    std::wstring strText = to_utf16string(ss.str());

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
    std::string requestBody = to_utf8string(TextboxRequestString->Text->Data());
    std::string requestHeaders = to_utf8string(TextboxHeaders->Text->Data());
    std::string requestMethod = to_utf8string(TextboxMethod->Text->Data());
    std::string requestUrl = to_utf8string(TextboxURL->Text->Data());

    HCCallHandle call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, requestMethod.c_str(), requestUrl.c_str());
    HCHttpCallRequestSetRequestBodyString(call, requestBody.c_str());
    HCHttpCallRequestSetRetryAllowed(call, RetryAllowedCheckbox->IsChecked->Value);
    auto headers = ExtractHeadersFromHeadersString(requestHeaders.c_str());
    for (auto header : headers)
    {
        std::string headerName = header[0];
        std::string headerValue = header[1];
        HCHttpCallRequestSetHeader(call, headerName.c_str(), headerValue.c_str(), true);
    }

    AsyncBlock* asyncBlock = new AsyncBlock;
    ZeroMemory(asyncBlock, sizeof(AsyncBlock));
    asyncBlock->context = call;
    asyncBlock->queue = m_queue;
    asyncBlock->callback = [](AsyncBlock* asyncBlock)
    {
        const char* str;
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

