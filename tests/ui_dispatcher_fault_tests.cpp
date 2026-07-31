#include <uni/gui/app.h>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using namespace Uni::GUI;

    UiAppConfig config;
    config.title = "UniGUI dispatcher queue fault test";
    config.initial_width = 320;
    config.initial_height = 240;
    config.renderer = UiRendererPreference::SdlRenderer;
    config.viewports = UiFeaturePolicy::Disabled;
    config.vsync = UiVsyncMode::Disabled;
    config.persistence.enabled = false;
    config.docking.enabled = false;
    config.frame_policy.active = {UiLoopMode::WaitForEvent, 0.0};
    config.frame_policy.idle = {UiLoopMode::WaitForEvent, 0.0};

    UiApp app;
    Expect(app.Initialize(std::move(config)).has_value(), "Queue-fault application must initialize");
    Expect(app.Tick().has_value() && app.Tick().has_value(),
           "Queue-fault application must drain its initial frame request");

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    std::vector<SDL_Event> saturation_batch(1024);
    for (auto& event : saturation_batch) {
        event.type = SDL_EVENT_USER;
    }
    std::size_t saturated_event_count = 0;
    while (true) {
        const int added = SDL_PeepEvents(
            saturation_batch.data(),
            static_cast<int>(saturation_batch.size()),
            SDL_ADDEVENT,
            0,
            0);
        if (added > 0) {
            saturated_event_count += static_cast<std::size_t>(added);
        }
        if (added != static_cast<int>(saturation_batch.size())) {
            break;
        }
    }
    Expect(saturated_event_count >= 65535, "SDL event queue must be saturated for wake rollback coverage");

    UiDispatcher dispatcher = app.Dispatcher();
    std::optional<UiResult<void>> exit_request;
    std::thread worker([&] { exit_request.emplace(dispatcher.RequestExit()); });
    worker.join();
    Expect(exit_request && !*exit_request && exit_request->error().code == UiErrorCode::WakeupFailed,
           "A saturated SDL queue must reject a wake before committing exit intent");

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    SDL_ClearError();
    const auto tick = app.Tick();
    Expect(tick && !tick->exit_requested,
           "Failed wake scheduling must not mutate dispatcher frame or exit state");
    Expect(app.Shutdown().has_value(), "Queue-fault application must shut down");
    return EXIT_SUCCESS;
}
