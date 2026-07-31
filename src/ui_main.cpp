#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <uni/gui/callbacks.h>

#include <exception>
#include <array>
#include <charconv>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace {

struct AppState final {
    Uni::GUI::UiApp app;
    bool user_initialized{};
};

void LogError(const Uni::GUI::UiError& error) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "UniGUI error (%d): %s", static_cast<int>(error.code), error.message.c_str());
}

void ApplyLoopRate(const Uni::GUI::UiLoopRate& rate) {
    switch (rate.mode) {
    case Uni::GUI::UiLoopMode::Continuous:
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "0");
        break;
    case Uni::GUI::UiLoopMode::RateLimited: {
        std::array<char, 64> value{};
        const auto converted = std::to_chars(
            value.data(),
            value.data() + value.size(),
            rate.frames_per_second,
            std::chars_format::general,
            std::numeric_limits<double>::max_digits10);
        if (converted.ec == std::errc{}) {
            *converted.ptr = '\0';
            SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, value.data());
        }
        break;
    }
    case Uni::GUI::UiLoopMode::WaitForEvent:
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, "waitevent");
        break;
    }
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    *appstate = nullptr;

    try {
        auto* state = new (std::nothrow) AppState();
        if (!state) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate application state");
            return SDL_APP_FAILURE;
        }
        *appstate = state;

        auto initialized = state->app.Initialize(uni_gui_app_configure(argc, argv));
        if (!initialized) {
            LogError(initialized.error());
            return SDL_APP_FAILURE;
        }

        auto user_initialized = uni_gui_app_initialize(state->app);
        if (!user_initialized) {
            LogError(user_initialized.error());
            return SDL_APP_FAILURE;
        }
        state->user_initialized = true;
    } catch (const std::exception& exception) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application initialization threw: %s", exception.what());
        return SDL_APP_FAILURE;
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application initialization threw an unknown exception");
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    if (!appstate) {
        return SDL_APP_FAILURE;
    }

    try {
        auto ticked = static_cast<AppState*>(appstate)->app.Tick();
        if (!ticked) {
            LogError(ticked.error());
            return SDL_APP_FAILURE;
        }
        ApplyLoopRate(ticked->next_iteration);
        return ticked->exit_requested ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
    } catch (const std::exception& exception) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application frame threw: %s", exception.what());
        return SDL_APP_FAILURE;
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application frame threw an unknown exception");
        return SDL_APP_FAILURE;
    }
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (!appstate) {
        return SDL_APP_FAILURE;
    }

    try {
        auto processed = static_cast<AppState*>(appstate)->app.DispatchEvent(*event);
        if (!processed) {
            LogError(processed.error());
            return SDL_APP_FAILURE;
        }
        return processed->exit_requested ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
    } catch (const std::exception& exception) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application event handling threw: %s", exception.what());
        return SDL_APP_FAILURE;
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application event handling threw an unknown exception");
        return SDL_APP_FAILURE;
    }
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    SDL_ResetHint(SDL_HINT_MAIN_CALLBACK_RATE);
    std::unique_ptr<AppState> state(static_cast<AppState*>(appstate));
    if (!state || !state->user_initialized) {
        return;
    }

    try {
        if (auto finalized = uni_gui_app_finalize(state->app); !finalized) {
            LogError(finalized.error());
        }
    } catch (const std::exception& exception) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application finalization threw: %s", exception.what());
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Application finalization threw an unknown exception");
    }

    if (auto shutdown = state->app.Shutdown(); !shutdown) {
        LogError(shutdown.error());
    }
}
