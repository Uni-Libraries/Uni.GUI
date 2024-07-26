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
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
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

    bool UiWinsysSdl::ProcessEvent() {
        bool result = true;

        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                result = false;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(reinterpret_cast<SDL_Window *>(m_sdl_window)))
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
