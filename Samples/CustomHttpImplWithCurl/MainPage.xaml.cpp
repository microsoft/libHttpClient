#include "pch.h"
#include "MainPage.xaml.h"
#include "httpClient\httpClient.h"

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

static Windows::UI::Core::CoreDispatcher^ s_dispatcher;
static MainPage^ g_MainPage;

MainPage::MainPage()
{
	s_dispatcher = this->Dispatcher;
	g_MainPage = this;
	InitializeComponent();
    curl_global_init(CURL_GLOBAL_ALL);
}

struct MemoryStruct 
{
    char *memory;
    size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->memory = (char*)realloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

std::string to_utf8string(const std::wstring &value)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return conversion.to_bytes(value);
}

std::wstring to_utf16string(const std::string &value)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conversion;
    return conversion.from_bytes(value);
}

void HC_CALLING_CONV PerformCall(
	_In_ HC_CALL_HANDLE call,
	_In_ HC_ASYNC_TASK_HANDLE taskHandle
	)
{
    const WCHAR* url = nullptr;
    const WCHAR* method = nullptr;
    const WCHAR* requestBody = nullptr;
    const WCHAR* userAgent = nullptr;
    HCHttpCallRequestGetUrl(call, &method, &url);
    HCHttpCallRequestGetRequestBodyString(call, &requestBody);
    HCHttpCallRequestGetHeader(call, L"User-Agent", &userAgent);

    CURL *curl;
    curl = curl_easy_init();
    CURLcode res;

    // from https://curl.haxx.se/libcurl/c/postinmemory.html
    struct MemoryStruct chunk;
    chunk.memory = (char*)malloc(1);  /* will be grown as needed by realloc above */
    chunk.size = 0;    /* no data at this point */

    curl_easy_setopt(curl, CURLOPT_URL, to_utf8string(url).c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    res = curl_easy_perform(curl);
    if (res == CURLE_OK)
    {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        HCHttpCallResponseSetStatusCode(call, response_code);
        char *ct;
        res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
        //HCHttpCallResponseSetHeader(call, L"ContractVersoin", L"Ver1");
        std::string responseStr = chunk.memory;
        std::wstring wstr = to_utf16string(responseStr);
        HCHttpCallResponseSetResponseString(call, wstr.c_str());
    }
    HCHttpCallResponseSetErrorCode(call, res);
	HCThreadSetResultsReady(taskHandle);

    curl_easy_cleanup(curl);
}


void HttpTestApp::MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    double ver = HCGlobalGetLibVersion();

    HCGlobalInitialize();
    HCGlobalSetHttpCallPerformFunction(PerformCall);
    HCSettingsSetTimeoutWindow(120);
    uint32_t timeoutWindow = 0;
    HCSettingsGetTimeoutWindow(&timeoutWindow);

    HC_CALL_HANDLE call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, L"GET", L"http://www.bing.com");
    HCHttpCallRequestSetHeader(call, L"User-Agent", L"xsapi");
    HCHttpCallRequestSetRetryAllowed(call, true);

    HCHttpCallPerform(call, nullptr, 
		[](_In_ void* completionRoutineContext, _In_ HC_CALL_HANDLE call)
		{
			uint32_t errCode = 0;
			HCHttpCallResponseGetErrorCode(call, &errCode);
			uint32_t statusCode = 0;
			HCHttpCallResponseGetStatusCode(call, &statusCode);
			const WCHAR* str;
			std::wstring responseString;
			HCHttpCallResponseGetResponseString(call, &str);
			if (str != nullptr) responseString = str;
			uint32_t numHeaders = 0;
			HCHttpCallResponseGetNumHeaders(call, &numHeaders);
			HCHttpCallCleanup(call);

			// If !g_manualThreadingCheckbox, then this callback is called from background thread
			// and we must set the XAML text on the UI thread, so use CoreDispatcher to get it there
			s_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
				ref new Windows::UI::Core::DispatchedHandler([responseString]()
			{
				g_MainPage->LogTextBox->Text = ref new Platform::String(responseString.c_str());
			}));

	});


    HCGlobalCleanup();
}

