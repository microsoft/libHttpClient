#pragma once
#include "MainPage.g.h"

namespace HttpTestApp
{
    public ref class MainPage sealed
    {
    public:
        MainPage();

    private:
        void Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e);
    };
}
