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
        HCWebsocketHandle m_websocket;

        void StartBackgroundThread();
        void StopBackgroundThread();

        XTaskQueueHandle m_queue;
        XTaskQueueRegistrationToken m_callbackToken;
    };
}
