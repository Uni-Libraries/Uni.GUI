//
// Includes
//

// ImGUI
#include <imgui.h>

// Uni.GUI
#include "window_demo.h"


//
// Implementation
//

namespace Uni::GUI::Example {
    bool WindowDemo::UiUpdate() {
        ImGui::ShowDemoWindow();
        return true;
    }
}
