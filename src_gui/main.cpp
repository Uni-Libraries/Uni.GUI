//
// INcud
//
#include <window_demo.h>

#include "ui_callbacks.h"


std::string uni_gui_app_name_get()
{
    return "Uni.GUI Example";
}

std::string uni_gui_app_version_get()
{
    return "1.0.0";
}

std::vector<std::shared_ptr<Uni::GUI::UiElement>> uni_gui_app_initialize()
{
    return {std::make_shared<Uni::GUI::Example::WindowDemo>()};
}
