#include <uni/gui/callbacks.h>

#include <SDL3/SDL_hints.h>

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <thread>

namespace {

std::thread Worker;

} // namespace

Uni::GUI::UiAppConfig uni_gui_app_configure(int, char**) {
    using namespace Uni::GUI;

    UiAppConfig config;
    config.title = "UniGUI SDL 3.4 callback wake test";
    config.initial_width = 320;
    config.initial_height = 240;
    config.renderer = UiRendererPreference::SdlRenderer;
    config.viewports = UiFeaturePolicy::Disabled;
    config.vsync = UiVsyncMode::Disabled;
    config.persistence.enabled = false;
    config.docking.enabled = false;
    config.frame_policy.active = {UiLoopMode::WaitForEvent, 0.0};
    config.frame_policy.idle = {UiLoopMode::WaitForEvent, 0.0};
    config.frame_policy.minimized = {UiLoopMode::WaitForEvent, 0.0};
    return config;
}

Uni::GUI::UiResult<void> uni_gui_app_initialize(Uni::GUI::UiApp& app) {
    Worker = std::thread([dispatcher = app.Dispatcher()] {
        bool wait_mode_active = false;
        for (int attempt = 0; attempt < 500; ++attempt) {
            const char* const callback_rate = SDL_GetHint(SDL_HINT_MAIN_CALLBACK_RATE);
            if (callback_rate && std::strcmp(callback_rate, "waitevent") == 0) {
                wait_mode_active = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        if (!wait_mode_active) {
            std::abort();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
        if (auto requested = dispatcher.RequestExit(); !requested) {
            std::abort();
        }
    });
    return {};
}

Uni::GUI::UiResult<void> uni_gui_app_finalize(Uni::GUI::UiApp&) {
    if (Worker.joinable()) {
        Worker.join();
    }
    return {};
}
