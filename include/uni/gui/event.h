#pragma once

#include <uni/gui/error.h>

#include <SDL3/SDL_events.h>

#include <functional>

namespace Uni::GUI {

class UiApp;

enum class UiEventAction {
    Pass,
    Consume,
    Exit,
};

struct UiEventContext final {
    UiApp& app;
    const SDL_Event& event;
    UiEventAction current_action{UiEventAction::Pass};
    bool delivered_to_imgui{};
    bool recognized_by_imgui{};
};

using UiEventHook = std::function<UiResult<UiEventAction>(UiEventContext&)>;

struct UiEventHooks final {
    UiEventHook before_imgui;
    UiEventHook after_imgui;
};

struct UiEventDispatchResult final {
    bool consumed{};
    bool exit_requested{};
};

} // namespace Uni::GUI
