//
// Includes
//

// SDL
#include <SDL3/SDL.h>

#include <cmath>
#include <limits>

// Dear ImGui
#include <imgui_impl_sdl3.h>

// Uni.GUI
#include "ui_winsys_sdl.h"



//
// Implementation
//

namespace Uni::GUI{
    UiWinsysSdl::~UiWinsysSdl() {
        if (m_sdl_window != nullptr) {
            SDL_DestroyWindow(static_cast<SDL_Window*>(m_sdl_window));
            m_sdl_window = nullptr;
        }

        if (m_sdl_initialized) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
            m_sdl_initialized = false;
        }
    }

    UiWinsysInitResult UiWinsysSdl::Init(const UiAppConfig& config) {
#if defined(SDL_PLATFORM_LINUX)
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif

        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_InitSubSystem(): %s", SDL_GetError());
            return UiWinsysInitResult::SdlInitializationFailed;
        }
        m_sdl_initialized = true;

        m_display_scale = config.dpi.high_density_window
            ? SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay())
            : 1.0f;
        if (!std::isfinite(m_display_scale) || m_display_scale <= 0.0f) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetDisplayContentScale(): %s", SDL_GetError());
            m_display_scale = 1.0f;
        }

        SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
        if (config.dpi.high_density_window) {
            window_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }
        const double scaled_width = static_cast<double>(config.initial_width) * m_display_scale;
        const double scaled_height = static_cast<double>(config.initial_height) * m_display_scale;
        if (!std::isfinite(scaled_width) ||
            !std::isfinite(scaled_height) ||
            scaled_width > std::numeric_limits<int>::max() ||
            scaled_height > std::numeric_limits<int>::max()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scaled window dimensions exceed SDL integer limits");
            return UiWinsysInitResult::WindowCreationFailed;
        }

        m_sdl_window = SDL_CreateWindow(
            config.title.c_str(),
            static_cast<int>(scaled_width),
            static_cast<int>(scaled_height),
            window_flags);
        if (!m_sdl_window) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow(): %s", SDL_GetError());
            return UiWinsysInitResult::WindowCreationFailed;
        }

        return UiWinsysInitResult::Success;
    }

    void* UiWinsysSdl::GetHandle() {
        return m_sdl_window;
    }

    float UiWinsysSdl::GetDisplayScale() const {
        return m_display_scale;
    }

    bool UiWinsysSdl::IsMinimized() const {
        if (!m_sdl_window) {
            return false;
        }
        return (SDL_GetWindowFlags(static_cast<SDL_Window*>(m_sdl_window)) & SDL_WINDOW_MINIMIZED) != 0;
    }

    bool UiWinsysSdl::FeedEvent(const SDL_Event& event) {
        return ImGui_ImplSDL3_ProcessEvent(&event);
    }

    bool UiWinsysSdl::IsCloseEvent(const SDL_Event& event) const noexcept {
        if (event.type == SDL_EVENT_QUIT) {
            return true;
        }
        return event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
               event.window.windowID == SDL_GetWindowID(static_cast<SDL_Window*>(m_sdl_window));
    }

    void UiWinsysSdl::NewFrame() {
        ImGui_ImplSDL3_NewFrame();
    }

    void UiWinsysSdl::Show() {
        auto* window = static_cast<SDL_Window*>(m_sdl_window);
        if (!window) {
            return;
        }
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(window);
    }
}
