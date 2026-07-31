#pragma once

//
// Includes
//

// SRC
#include "ui_renderer.h"

// Forward Declaration
struct SDL_Renderer;
struct SDL_Window;



//
// Class
//

namespace Uni::GUI{
    class UiRendererSdl: public UiRenderer{
    public:
        ~UiRendererSdl() override;
        bool Init(void* window_handle) override;
        bool InitImgui() override;
        void ShutdownImgui() override;
        void NewFrame() override;
        bool Render() override;
        bool SetVsync(int interval) override;
        [[nodiscard]] std::string_view GetApiName() const override;
    private:
        SDL_Window* m_window{};
        SDL_Renderer* m_renderer{};
        bool m_imgui_platform_initialized{};
        bool m_imgui_renderer_initialized{};
    };
}
