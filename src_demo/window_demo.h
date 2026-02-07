#pragma once

//
// Includes
//

// Uni.GUI
#include "ui_element.h"

//
// Public
//

namespace Uni::GUI::Example {
    class WindowDemo: public Uni::GUI::UiElement {
    public:
        explicit WindowDemo() = default;
        bool UiUpdate(UiApp&) override;
    };
}

