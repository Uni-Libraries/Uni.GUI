#include <uni/gui/callbacks.h>

#include "window_demo.h"

#include <memory>
#include <utility>

Uni::GUI::UiAppConfig uni_gui_app_configure(int, char**) {
    Uni::GUI::UiAppConfig config;
    config.title = UNIGUI_DEMO_TITLE;
    config.renderer = Uni::GUI::UiRendererPreference::Automatic;
    config.viewports = Uni::GUI::UiFeaturePolicy::IfSupported;
    config.vsync = Uni::GUI::UiVsyncMode::Enabled;
    config.persistence.path = "uni-gui-demo.settings";

    Uni::GUI::UiDockLayout default_layout;
    default_layout.id = "default";
    default_layout.splits.push_back({"root", Uni::GUI::UiDockSide::Right, 0.35f, "tools", "main"});
    default_layout.placements.push_back({"demo", "main"});
    default_layout.placements.push_back({"Dear ImGui Demo", "tools"});
    default_layout.placements.push_back({"ImPlot Demo", "tools"});
    config.docking.layouts.push_back(std::move(default_layout));
    config.docking.initial_layout = "default";
    return config;
}

Uni::GUI::UiResult<void>
uni_gui_app_initialize(Uni::GUI::UiApp& app) {
    if (auto added = app.AddElement(std::make_unique<Uni::GUI::Example::WindowDemo>()); !added) {
        return std::unexpected(std::move(added.error()));
    }
    return {};
}

Uni::GUI::UiResult<void> uni_gui_app_finalize(Uni::GUI::UiApp&) {
    return {};
}
