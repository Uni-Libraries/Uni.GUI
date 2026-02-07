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
        bool Init(void* window_handle) override;
        bool InitImgui() override;
        void NewFrame(std::pair<size_t, size_t> new_size) override;
        void Render() override;
        bool SetVsync(int interval) override;
        [[nodiscard]] const std::string_view GetApiName() const override;
    private:
        SDL_Window* m_window{};
        SDL_Renderer* m_renderer{};
    };
}
