#pragma once

//
// Includes
//

// SRC
#include "ui_winsys.h"


//
//
//

namespace Uni::GUI {
    class UiWinsysSdl: public UiWinsys{
    public:
        bool Init(const std::string& title) override;

        bool InitImgui() override;

        void* GetHandle() override;

        bool ProcessEvent(void* event) override;

        void NewFrame() override;

        void Show() override;

        std::pair<size_t,size_t> ResizeRequired() override;
    private:
        void* m_sdl_window{};

    };
}
