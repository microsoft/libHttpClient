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

        void Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void DispatcherTimer_Tick(Platform::Object^ sender, Platform::Object^ e);

        static void UpdateXamlUI(
            _In_ uint32_t errCode,
            _In_ std::string errMessage,
            _In_ uint32_t statusCode,
            _In_ std::string responseString,
            _In_ std::vector<std::vector<std::string>> headers
            );

        void StartBackgroundThread();
        void StopBackgroundThread();

        async_queue_handle_t m_queue;
        registration_token_t m_callbackToken;
    };
}
