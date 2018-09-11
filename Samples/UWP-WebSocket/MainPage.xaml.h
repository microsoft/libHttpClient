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
        hc_websocket_handle_t m_websocket;

        void StartBackgroundThread();
        void StopBackgroundThread();

        async_queue_handle_t m_queue;
        registration_token_t m_callbackToken;
    };
}
