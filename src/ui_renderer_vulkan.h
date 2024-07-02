#pragma once

#pragma once

//
// Includes
//

// SRC
#include "ui_renderer.h"


//
//
//

namespace Uni::GUI {
    class UiRendererVulkan: public UiRenderer{
    public:
        bool Init(void* window_handle) override;
        bool InitImgui() override;
        void NewFrame(std::pair<size_t, size_t> new_size) override;
        void Render() override;
    private:
        void* m_ptr_window{};
    };
}