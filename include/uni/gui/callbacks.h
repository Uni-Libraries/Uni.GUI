#pragma once

/// @file
/// @brief Callbacks supplied by an executable linked with UniGUI::Main.

#include <uni/gui/app.h>

/// Build the configuration before initializing the UI. The arguments are those supplied by the SDL entry point.
Uni::GUI::UiAppConfig uni_gui_app_configure(int argc, char** argv);

/// Install application UI after library initialization. Returning an error aborts startup and skips finalization.
Uni::GUI::UiResult<void> uni_gui_app_initialize(Uni::GUI::UiApp& app);

/// Release application-owned resources before library shutdown, only after a successful user initialization.
Uni::GUI::UiResult<void> uni_gui_app_finalize(Uni::GUI::UiApp& app);
