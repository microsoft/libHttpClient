#include "pch.h"
#include "MainPage.xaml.h"
#include "httpClient\httpClient.h"

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


MainPage::MainPage()
{
    InitializeComponent();
}

void HC_CALLING_CONV PerformCall(_In_ HC_CALL_HANDLE call)
{
    const WCHAR* url = nullptr;
    const WCHAR* method = nullptr;
    const WCHAR* requestBody = nullptr;
    const WCHAR* userAgent = nullptr;
    HCHttpCallRequestGetUrl(call, &method, &url, &requestBody);
    HCHttpCallRequestGetHeader(call, L"User-Agent", &userAgent);

    // TODO: make call

    HCHttpCallResponseSetStatusCode(call, 200);
    HCHttpCallResponseSetHeader(call, L"ContractVersoin", L"Ver1");
    HCHttpCallResponseSetErrorCode(call, 10);
    HCHttpCallResponseSetResponseString(call, L"bing.com");
}


void HttpTestApp::MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    HCGlobalInitialize();
    //HCGlobalSetHttpCallPerformCallback(PerformCall);
    HCSettingsSetTimeoutWindow(120);
    uint32_t timeoutWindow = 0;
    HCSettingsGetTimeoutWindow(&timeoutWindow);

    HC_CALL_HANDLE call = nullptr;
    HCHttpCallCreate(&call);
    HCHttpCallRequestSetUrl(call, L"GET", L"http://www.bing.com", nullptr);
    HCHttpCallRequestSetHeader(call, L"User-Agent", L"xsapi");
    HCHttpCallRequestSetRetryAllowed(call, true);

    HCHttpCallPerform(call);

    uint32_t errCode = 0;
    HCHttpCallResponseGetErrorCode(call, &errCode);
    uint32_t statusCode = 0;
    HCHttpCallResponseGetStatusCode(call, &statusCode);
    const WCHAR* str;
    std::wstring responseString;
    HCHttpCallResponseGetResponseString(call, &str);
    if( str != nullptr ) responseString = str;
    LogTextBox->Text = ref new Platform::String(responseString.c_str());
    uint32_t numHeaders = 0;
    HCHttpCallResponseGetNumHeaders(call, &numHeaders);
    HCHttpCallCleanup(call);

    HCGlobalCleanup();
}

