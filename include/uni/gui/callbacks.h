#pragma once

#include <uni/gui/app.h>

Uni::GUI::UiAppConfig uni_gui_app_configure(int argc, char** argv);

Uni::GUI::UiResult<void> uni_gui_app_initialize(Uni::GUI::UiApp& app);

Uni::GUI::UiResult<void> uni_gui_app_finalize(Uni::GUI::UiApp& app);
