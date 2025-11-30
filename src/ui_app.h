#pragma once

//
// Includes
//

// stdlib
#include <memory>
#include <string>
#include <vector>

// Uni.GUI
#include "ui_element.h"
#include "ui_renderer.h"
#include "ui_winsys.h"

#include "uni.gui_export.h"



//
//
//

namespace Uni::GUI {
    class UNI_GUI_EXPORT Ui{

    public:
        bool Init(const std::string& title);
        bool Process();
        bool ProcessEvent(void* event);
        bool RegisterWindow(UiElement* ui_element);
        bool SetVsync(int interval);

    private:
        std::shared_ptr<UiWinsys> m_winsys{};
        std::shared_ptr<UiRenderer> m_renderer{};
        std::vector<UiElement*> m_windows{};
    };
}

