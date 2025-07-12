#pragma once

//
// Includes
//

// SRC
#include "ui_renderer.h"


//
//
//

namespace Uni::GUI{
    class UiRendererSdl: public UiRenderer{
    public:
        bool Init(void* window_handle) override;
        bool InitImgui() override;
        void NewFrame(std::pair<size_t, size_t> new_size) override;
        void Render() override;
        bool SetVsync(int interval) override;
    private:
        void* m_ptr_window{};
        void* m_ptr_render{};
    };
}
