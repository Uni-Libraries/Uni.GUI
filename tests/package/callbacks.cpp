#include <uni/gui/callbacks.h>

#include <memory>
#include <string>
#include <utility>

namespace {

class PackageWindow final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        return Uni::GUI::UiElementUpdate{};
    }
};

} // namespace

Uni::GUI::UiAppConfig uni_gui_app_configure(int, char**) {
    Uni::GUI::UiAppConfig config;
    config.title = "UniGUI package test";
    config.persistence.enabled = false;
    config.docking.enabled = false;
    return config;
}

Uni::GUI::UiResult<void> uni_gui_app_initialize(Uni::GUI::UiApp& app) {
    auto added = app.AddElement(std::make_unique<PackageWindow>());
    if (!added) {
        return std::unexpected(std::move(added.error()));
    }
    return {};
}

Uni::GUI::UiResult<void> uni_gui_app_finalize(Uni::GUI::UiApp&) {
    return {};
}
