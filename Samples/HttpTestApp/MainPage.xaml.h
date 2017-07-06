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
        Windows::UI::Xaml::DispatcherTimer^ m_timer;
        HANDLE m_hBackgroundThread;

        void Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
        void DispatcherTimer_Tick(Platform::Object^ sender, Platform::Object^ e);

        static void UpdateXamlUI(
            _In_ uint32_t errCode,
            _In_ std::wstring errMessage,
            _In_ uint32_t statusCode,
            _In_ std::wstring responseString,
            _In_ std::vector<std::vector<std::wstring>> headers
            );

        void ReadManualThreadingCheckbox();
        void EnableManualThreading();
        void DisableManualThreading();
    };
}
