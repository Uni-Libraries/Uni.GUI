//
// Includes
//

// ImGUI
#include <imgui.h>
#include <implot.h>

// Uni.GUI
#include "window_demo.h"


//
// Implementation
//

namespace Uni::GUI::Example {
    bool WindowDemo::UiUpdate(UiApp&) {
        static bool win_about = false;
        static bool win_demo = false;
        static bool win_demo_implot = false;
        static bool win_metrics = false;

        ImGui::SetNextWindowSize({800,600});
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
            if (ImGui::Button("ImPlot Demo"))
            {
                win_demo_implot = true;
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

        if(win_demo_implot){
            ImPlot::ShowDemoWindow(&win_demo_implot);
        }

        if (win_metrics)
        {
            ImGui::ShowMetricsWindow(&win_metrics);
        }

        return true;
    }
}

