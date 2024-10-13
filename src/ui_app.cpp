//
// Includes
//

// Imgui
#include <imgui.h>

#include "fa_solid_900.h"

// Uni.GUI
#include "ui_app.h"
#include "ui_font.h"

#include "ui_winsys_sdl.h"
#include "ui_renderer_sdl.h"



//
// Implementation
//

namespace Uni::GUI {
    bool Ui::Init(const std::string& title) {
        m_winsys = std::make_shared<UiWinsysSdl>();
        m_renderer = std::make_shared<UiRendererSdl>();

        if(!m_winsys->Init(title)){
            return false;
        }

        if(!m_renderer->Init(m_winsys->GetHandle())){
            return false;
        }

        m_winsys->Show();

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // Set io confugation
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Configure fonts
        io.Fonts->AddFontFromMemoryCompressedBase85TTF(Font::GetRobotoMedium(), 12 * 2.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
        static const ImWchar icons_ranges[] = { 0xf000, 0xf950, 0 };

        ImFontConfig icons_config{};
        icons_config.MergeMode = true;
        icons_config.PixelSnapH = true;
        icons_config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)fa_solid_900, sizeof(fa_solid_900), 12 * 2.0f, &icons_config, icons_ranges);

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup Platform/Renderer backends
        if(!m_winsys->InitImgui()){
            return false;
        }
        if(!m_renderer->InitImgui()) {
            return false;
        }
        m_winsys->Show();

        return true;
    }

    bool Ui::Process() {
        // Start the Dear ImGui frame
        m_renderer->NewFrame(m_winsys->ResizeRequired());
        m_winsys->NewFrame();
        ImGui::NewFrame();

        // windows
        for (auto *window: m_windows) {
            window->UiUpdate();
        }

        // Rendering
        ImGui::Render();
        m_renderer->Render();

        return true;
    }

    bool Ui::ProcessEvent(void* event)
    {
        return m_winsys->ProcessEvent(event);
    }

    bool Ui::RegisterWindow(UiElement *ui_element) {
        if(!ui_element) {
            return false;
        }
        m_windows.push_back(ui_element);
        return true;
    }

    bool Ui::SetVsync(int interval)
    {
        if (!m_renderer)
        {
            return false;
        }
        return m_renderer->SetVsync(interval);
    }
}
