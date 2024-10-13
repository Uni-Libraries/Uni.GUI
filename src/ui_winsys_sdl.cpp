//
// Includes
//

// SDL
#include <SDL3/SDL.h>

// Dear ImGUI
#include <imgui_impl_sdl3.h>

// Uni.GUI
#include "ui_winsys_sdl.h"



//
// Implementation
//

namespace Uni::GUI{
    bool UiWinsysSdl::Init(const std::string& title) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return false;
        }

        // Create window with SDL_Renderer graphics context
        uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
        m_sdl_window = SDL_CreateWindow(title.c_str(), 1280, 720, window_flags);
        if (!m_sdl_window) {
            return false;
        }

        return true;
    }

    void* UiWinsysSdl::GetHandle() {
        return m_sdl_window;
    }

    bool UiWinsysSdl::ProcessEvent(void* event)
    {
        auto* ev = static_cast<SDL_Event*>(event);
        bool result = true;

        ImGui_ImplSDL3_ProcessEvent(ev);
        if (ev->type == SDL_EVENT_QUIT)
        {
            result = false;
        }
        if (ev->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
            ev->window.windowID == SDL_GetWindowID(static_cast<SDL_Window *>(m_sdl_window)))
        {
            result = false;
        }

        return result;
    }

    void UiWinsysSdl::NewFrame() {
        ImGui_ImplSDL3_NewFrame();
    }

    void UiWinsysSdl::Show() {
        SDL_SetWindowPosition(reinterpret_cast<SDL_Window *>(m_sdl_window), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_ShowWindow(reinterpret_cast<SDL_Window *>(m_sdl_window));
    }

    bool UiWinsysSdl::InitImgui() {
        return true;
    }

    std::pair<size_t,size_t> UiWinsysSdl::ResizeRequired() {
        return {0,0};
    }
}
