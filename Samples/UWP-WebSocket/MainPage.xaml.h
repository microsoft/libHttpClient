#pragma once
#include "MainPage.g.h"

namespace HttpTestApp
{
    public ref class MainPage sealed
    {
    public:
        MainPage();
        virtual ~MainPage();

    private:
        HANDLE m_hBackgroundThread;
        void Connect_Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void SendMessage_Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void Close_Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);

        void LogToUI(std::string str);
        void ClearLog();
        HC_WEBSOCKET_HANDLE m_websocket;

        //static void UpdateXamlUI(
        //    _In_ uint32_t errCode,
        //    _In_ std::string errMessage,
        //    _In_ uint32_t statusCode,
        //    _In_ std::string responseString,
        //    _In_ std::vector<std::vector<std::string>> headers
        //    );

        void StartBackgroundThread();
        void StopBackgroundThread();
    };
}
