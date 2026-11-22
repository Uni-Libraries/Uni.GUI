#pragma once

#include <uni/gui/error.h>

#include <SDL3/SDL_events.h>

#include <functional>

namespace Uni::GUI {

class UiApp;

/// Pass to normal handling, consume, or request exit. Return the action from the hook.
enum class UiEventAction {
    Pass,
    Consume,
    Exit,
};

/// Borrowed context valid only during one hook invocation on the UI thread.
struct UiEventContext final {
    UiApp& app;
    const SDL_Event& event;
    bool application_quit_requested{};
    bool main_window_close_requested{};
    UiEventAction current_action{UiEventAction::Pass};
    bool delivered_to_imgui{}; ///< The backend received the event, regardless of its return value.
    bool recognized_by_imgui{}; ///< The backend reported handling it; this is not an ImGui input-capture flag.
};

/// Return an action or an error to stop dispatch; exceptions become UiErrorCode::EventHandling.
using UiEventHook = std::function<UiResult<UiEventAction>(UiEventContext&)>;

/// Optional handlers. The before hook can suppress backend delivery. The after hook
/// sees the resulting action and may escalate to Consume or Exit, but cannot undo either.
struct UiEventHooks final {
    UiEventHook before_imgui;
    UiEventHook after_imgui;
};

/// Final event disposition. An exit request is also considered consumed.
struct UiEventDispatchResult final {
    bool consumed{};
    bool exit_requested{};
};

} // namespace Uni::GUI
