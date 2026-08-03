//
// Includes
//

// SDL
#include <SDL3/SDL.h>

#include <cmath>
#include <string>

// Dear ImGui
#include <imgui_impl_sdl3.h>

// Uni.GUI
#include "ui_winsys_sdl.h"



//
// Implementation
//

namespace Uni::GUI{
    namespace {
        [[nodiscard]] bool PositiveFinite(const float value) {
            return std::isfinite(value) && value > 0.0f;
        }
    }

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
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_InitSubSystem(): %s", SDL_GetError());
            return UiWinsysInitResult::SdlInitializationFailed;
        }
        m_sdl_initialized = true;

        SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
        if (config.scaling.high_pixel_density) {
            window_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
        }
        float system_ui_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
        if (!PositiveFinite(system_ui_scale)) {
            system_ui_scale = 1.0f;
        }
        const auto initial_scale = Detail::ResolveEffectiveUiScale(config.scaling, system_ui_scale);
        const auto scaled_width = initial_scale
            ? Detail::ScaleInitialWindowDimension(config.initial_width, *initial_scale)
            : std::nullopt;
        const auto scaled_height = initial_scale
            ? Detail::ScaleInitialWindowDimension(config.initial_height, *initial_scale)
            : std::nullopt;
        if (!scaled_width || !scaled_height) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Scaled window dimensions are invalid");
            return UiWinsysInitResult::WindowCreationFailed;
        }

        m_sdl_window = SDL_CreateWindow(
            config.title.c_str(),
            *scaled_width,
            *scaled_height,
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

    std::optional<Detail::UiWindowMetrics> UiWinsysSdl::QueryDisplayMetrics() const {
        auto* window = static_cast<SDL_Window*>(m_sdl_window);
        if (!window) {
            return std::nullopt;
        }
        int window_width = 0;
        int window_height = 0;
        int framebuffer_width = 0;
        int framebuffer_height = 0;
        if (!SDL_GetWindowSize(window, &window_width, &window_height) ||
            !SDL_GetWindowSizeInPixels(window, &framebuffer_width, &framebuffer_height) ||
            window_width <= 0 || window_height <= 0 ||
            framebuffer_width <= 0 || framebuffer_height <= 0) {
            return std::nullopt;
        }
        float pixel_density = SDL_GetWindowPixelDensity(window);
        if (!PositiveFinite(pixel_density)) {
            const float density_x = static_cast<float>(framebuffer_width) / static_cast<float>(window_width);
            const float density_y = static_cast<float>(framebuffer_height) / static_cast<float>(window_height);
            pixel_density = (density_x + density_y) * 0.5f;
        }
        float display_scale = SDL_GetWindowDisplayScale(window);
        if (!PositiveFinite(display_scale)) {
            const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
            const float content_scale = display ? SDL_GetDisplayContentScale(display) : 0.0f;
            if (PositiveFinite(content_scale) && PositiveFinite(pixel_density)) {
                display_scale = content_scale * pixel_density;
            }
        }
        if (!PositiveFinite(display_scale) || !PositiveFinite(pixel_density)) {
            return std::nullopt;
        }
        return Detail::UiWindowMetrics{
            .window_size = {window_width, window_height},
            .framebuffer_size = {framebuffer_width, framebuffer_height},
            .display_scale = display_scale,
            .pixel_density = pixel_density,
        };
    }

    bool UiWinsysSdl::IsMinimized() const {
        if (!m_sdl_window) {
            return false;
        }
        return (SDL_GetWindowFlags(static_cast<SDL_Window*>(m_sdl_window)) & SDL_WINDOW_MINIMIZED) != 0;
    }

    bool UiWinsysSdl::SetTitle(const std::string_view title) {
        if (!m_sdl_window) {
            return false;
        }
        const std::string owned{title};
        return SDL_SetWindowTitle(static_cast<SDL_Window*>(m_sdl_window), owned.c_str());
    }

    bool UiWinsysSdl::FeedEvent(const SDL_Event& event) {
        return ImGui_ImplSDL3_ProcessEvent(&event);
    }

    bool UiWinsysSdl::IsApplicationQuitEvent(const SDL_Event& event) const noexcept {
        return event.type == SDL_EVENT_QUIT;
    }

    bool UiWinsysSdl::IsMainWindowCloseEvent(const SDL_Event& event) const noexcept {
        return m_sdl_window && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
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
