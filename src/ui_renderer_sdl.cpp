//
// Includes
//

// SDL
#include <SDL3/SDL.h>

// ImGUI
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

// ImWrap
#include "ui_renderer_sdl.h"
#include "SDL3/SDL_video.h"



//
// Implementation
//


namespace Uni::GUI {
    UiRendererSdl::~UiRendererSdl() {
        ShutdownImgui();

        if (m_renderer != nullptr) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }

        m_window = nullptr;
    }

    bool UiRendererSdl::Init(void* window_handle) {
        m_window = static_cast<SDL_Window*>(window_handle);
        if (!m_window) {
            return false;
        }

        m_renderer = SDL_CreateRenderer(m_window, nullptr);
        if (!m_renderer) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_CreateRenderer(): %s", SDL_GetError());
            return false;
        }
        return true;
    }

    bool UiRendererSdl::InitImgui() {
        if (!m_window || !m_renderer || ImGui::GetCurrentContext() == nullptr) {
            return false;
        }

        if (!ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer)) {
            return false;
        }
        m_imgui_platform_initialized = true;

        if (!ImGui_ImplSDLRenderer3_Init(m_renderer)) {
            ImGui_ImplSDL3_Shutdown();
            m_imgui_platform_initialized = false;
            return false;
        }
        m_imgui_renderer_initialized = true;
        return true;
    }

    void UiRendererSdl::ShutdownImgui() {
        if (ImGui::GetCurrentContext() == nullptr) {
            return;
        }

        if (m_imgui_renderer_initialized) {
            ImGui_ImplSDLRenderer3_Shutdown();
            m_imgui_renderer_initialized = false;
        }
        if (m_imgui_platform_initialized) {
            ImGui_ImplSDL3_Shutdown();
            m_imgui_platform_initialized = false;
        }
    }

    void UiRendererSdl::NewFrame() {
        ImGui_ImplSDLRenderer3_NewFrame();
    }

    bool UiRendererSdl::Render() {
        ImDrawData* const draw_data = ImGui::GetDrawData();
        if (!draw_data) {
            return false;
        }
        if (draw_data->Textures) {
            for (ImTextureData* texture : *draw_data->Textures) {
                if (texture->Status != ImTextureStatus_OK) {
                    ImGui_ImplSDLRenderer3_UpdateTexture(texture);
                }
            }
        }

        ImGuiIO& io = ImGui::GetIO();
        if (!SDL_SetRenderScale(m_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_SetRenderScale(): %s", SDL_GetError());
            return false;
        }
        if (!SDL_SetRenderDrawColorFloat(m_renderer, 0.0f, 0.0f, 0.0f, 1.0f) ||
            !SDL_RenderClear(m_renderer)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL renderer clear failed: %s", SDL_GetError());
            return false;
        }

        ImGui_ImplSDLRenderer3_RenderDrawData(draw_data, m_renderer);
        if (!SDL_RenderPresent(m_renderer)) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "SDL_RenderPresent(): %s", SDL_GetError());
            return false;
        }
        return true;
    }

    bool UiRendererSdl::SetVsync(int interval)
    {
        if (!m_renderer)
        {
            return false;
        }

        return SDL_SetRenderVSync(m_renderer, interval);
    }

    std::string_view UiRendererSdl::GetApiName() const {
        const char* name = m_renderer ? SDL_GetRendererName(m_renderer) : nullptr;
        return name ? std::string_view{name} : std::string_view{};
    }
}
