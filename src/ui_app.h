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

//
//
//

namespace Uni::GUI {
    class Ui{

    public:
        bool Init(const std::string& title, bool use_vsync);
        bool Process();
        bool RegisterWindow(UiElement* ui_element);

    private:
        std::shared_ptr<UiWinsys> m_winsys{};
        std::shared_ptr<UiRenderer> m_renderer{};
        std::vector<UiElement*> m_windows{};
    };
}