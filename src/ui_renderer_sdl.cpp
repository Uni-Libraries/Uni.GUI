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
    bool UiRendererSdl::Init(void* window_handle) {
        m_window = static_cast<SDL_Window*>(window_handle);
        m_renderer = SDL_CreateRenderer(m_window, nullptr);
        return m_renderer != nullptr;
    }

    bool UiRendererSdl::InitImgui() {
        ImGui_ImplSDL3_InitForSDLRenderer(m_window, m_renderer);
        ImGui_ImplSDLRenderer3_Init(m_renderer);
        return true;
    }

    void UiRendererSdl::NewFrame(std::pair<size_t, size_t> new_size) {
        ImGui_ImplSDLRenderer3_NewFrame();
    }

    void UiRendererSdl::Render() {
        ImGuiIO &io = ImGui::GetIO();
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 0);
        SDL_RenderClear(m_renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), static_cast<SDL_Renderer *>(m_renderer));
        SDL_RenderPresent(m_renderer);
    }

    bool UiRendererSdl::SetVsync(int interval)
    {
        if (!m_renderer)
        {
            return false;
        }

        // SDL_SetRenderVSync returns 0 on success, negative on error.
        return SDL_SetRenderVSync(m_renderer, interval) == 0;
    }

    const std::string_view UiRendererSdl::GetApiName() const {
        return SDL_GetRendererName(m_renderer);
    }
}
