//
// Includes
//

 // Imgui
#include <imgui.h>
#include <memory>

// Uni.GUI
#include "ui_app.h"
#include "ui_font.h"

#include "ui_winsys_sdl.h"
#include "ui_renderer_sdl.h"
#include "ui_renderer_sdlgpu.h"



//
// Implementation
//

namespace Uni::GUI {
    bool UiApp::Init(const std::string& title) {
        m_winsys = std::make_unique<UiWinsysSdl>();

        // Initialize windowing / SDL first
        if (!m_winsys->Init(title)) {
            return false;
        }

        // Prefer SDL_GPU renderer if available, fall back to SDL_Renderer
        {
           /* auto renderer_gpu = std::make_unique<UiRendererSdlGpu>();
            if (renderer_gpu->Init(m_winsys->GetHandle())) {
                m_renderer = std::move(renderer_gpu);
            } else {*/
                auto renderer_sdl = std::make_unique<UiRendererSdl>();
                if (!renderer_sdl->Init(m_winsys->GetHandle())) {
                    return false;
                }
                m_renderer = std::move(renderer_sdl);
           // }
        }

        m_winsys->Show();

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // Set io configuration
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        // Configure fonts
        io.Fonts->AddFontFromMemoryCompressedBase85TTF(
            Font::GetRobotoMedium(),
            12 * 2.0f,
            nullptr,
            io.Fonts->GetGlyphRangesCyrillic());

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();

        // Setup Platform/Renderer backends
        if (!m_winsys->InitImgui()) {
            return false;
        }
        if (!m_renderer->InitImgui()) {
            return false;
        }
        m_winsys->Show();

        return true;
    }

    bool UiApp::Process() {
        // Start the Dear ImGui frame
        m_renderer->NewFrame(m_winsys->ResizeRequired());
        m_winsys->NewFrame();
        ImGui::NewFrame();

        // windows
        for (auto *window: m_windows) {
            window->UiUpdate(*this);
        }

        // Rendering
        ImGui::Render();
        m_renderer->Render();

        return true;
    }

    bool UiApp::ProcessEvent(void* event)
    {
        return m_winsys->ProcessEvent(event);
    }

    bool UiApp::RegisterWindow(UiElement *ui_element) {
        if(!ui_element) {
            return false;
        }
        m_windows.push_back(ui_element);
        return true;
    }

    bool UiApp::SetVsync(int interval)
    {
        if (!m_renderer)
        {
            return false;
        }
        return m_renderer->SetVsync(interval);
    }

    const std::string_view UiApp::GetRenderingApiName() const{
        return m_renderer->GetApiName();
    }
}
