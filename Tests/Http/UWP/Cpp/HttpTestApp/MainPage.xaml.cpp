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


void HttpTestApp::MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
    double ver = HCGlobalGetLibVersion();
    LogTextBox->Text = "Version: " + ver.ToString();
}

