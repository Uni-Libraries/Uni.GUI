//
// Includes
//

// stdlib
#include <format>

// Windows SDK
#if defined(_WIN32)
#include <windows.h>
#endif

// Uni.GUI
#include "ui_app.h"

// Uni.GUI.Example
#include "window_demo.h"



//
// Implementation
//
#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
#else
int main()
#endif
{

    // initialize UI
    Uni::GUI::Ui ui{};
    if (!ui.Init(std::format("Uni.GUI demo v{}.{}.{}", 0, 0, 0))) {
        std::printf("main() -> failed to initialize UI");
        return 1;
    }

    // register windows
    Uni::GUI::Example::WindowDemo window_demo;
    ui.RegisterWindow(&window_demo);

    // start task manager

    // display UI
    while (ui.Process()) {}

    return 0;
}
