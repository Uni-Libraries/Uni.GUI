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



//
// Implementation
//


namespace Uni::GUI {
    bool UiRendererSdl::Init(void* window_handle) {
        m_ptr_window = window_handle;
        m_ptr_render = SDL_CreateRenderer(static_cast<SDL_Window *>(m_ptr_window), nullptr);
        return m_ptr_render != nullptr;
    }

    bool UiRendererSdl::InitImgui() {
        ImGui_ImplSDL3_InitForSDLRenderer(static_cast<SDL_Window*>(m_ptr_window), static_cast<SDL_Renderer*>(m_ptr_render));
        ImGui_ImplSDLRenderer3_Init(static_cast<SDL_Renderer*>(m_ptr_render));
        return true;
    }

    void UiRendererSdl::NewFrame(std::pair<size_t, size_t> new_size) {
        ImGui_ImplSDLRenderer3_NewFrame();
    }

    void UiRendererSdl::Render() {
        ImGuiIO &io = ImGui::GetIO();
        SDL_SetRenderDrawColor(static_cast<SDL_Renderer *>(m_ptr_render), 0, 0, 0, 0);
        SDL_RenderClear(static_cast<SDL_Renderer *>(m_ptr_render));
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), static_cast<SDL_Renderer *>(m_ptr_render));
        SDL_RenderPresent(static_cast<SDL_Renderer *>(m_ptr_render));
    }

    bool UiRendererSdl::SetVsync(int interval)
    {
        if (!m_ptr_render)
        {
            return false;
        }

        return SDL_SetRenderVSync(static_cast<SDL_Renderer *>(m_ptr_render), interval);
    }
}
