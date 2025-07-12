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
        static bool win_about = false;
        static bool win_demo = false;
        static bool win_metrics = false;

        if (ImGui::Begin("demo"))
        {
            if (ImGui::Button("About"))
            {
                win_about = true;
            }
            if (ImGui::Button("Demo"))
            {
                win_demo = true;
            }
            if (ImGui::Button("Metrics"))
            {
                win_metrics = true;
            }

            ImGui::End();
        }

        if (win_about)
        {
            ImGui::ShowAboutWindow(&win_about);
        }

        if (win_demo)
        {
            ImGui::ShowDemoWindow(&win_demo);
        }

        if (win_metrics)
        {
            ImGui::ShowMetricsWindow(&win_metrics);
        }

        return true;
    }
}

