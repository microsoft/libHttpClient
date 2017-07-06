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

		void Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
		void DispatcherTimer_Tick(Platform::Object^ sender, Platform::Object^ e);
    };
}
