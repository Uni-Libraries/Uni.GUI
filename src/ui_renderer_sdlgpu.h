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
        void NewFrame(std::pair<size_t, size_t> new_size) override;
        void Render() override;
        bool SetVsync(int interval) override;
        [[nodiscard]] const std::string_view GetApiName() const override;
    private:
        void* m_ptr_window{};
        SDL_GPUDevice* m_gpu_device{};
        int m_vsync_interval{1};
    };
}
