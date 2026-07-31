#pragma once

//
// Includes
//

 // SRC
#include "ui_renderer.h"

// Forward declarations
struct SDL_GPUDevice;

//
//
//

namespace Uni::GUI{
    class UiRendererSdlGpu: public UiRenderer{
    public:
        ~UiRendererSdlGpu() override;

        bool Init(void* window_handle) override;
        bool InitImgui() override;
        void ShutdownImgui() override;
        void NewFrame() override;
        bool Render() override;
        bool SetVsync(int interval) override;
        [[nodiscard]] std::string_view GetApiName() const override;
    private:
        void* m_ptr_window{};
        SDL_GPUDevice* m_gpu_device{};
        int m_vsync_interval{1};
        bool m_imgui_platform_initialized{};
        bool m_imgui_renderer_initialized{};
    };
}
