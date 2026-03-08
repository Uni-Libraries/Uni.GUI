//
// Includes
//

 // Imgui
#include <imgui.h>
#include <memory>

// Uni.GUI
#include "ui_app.h"
#include "ui_font.h"
#include "ui_texture.h"
#include "ui_texture_registry.h"

#include "ui_winsys_sdl.h"
#include "ui_renderer_sdl.h"
#include "ui_renderer_sdlgpu.h"



//
// Implementation
//

namespace Uni::GUI {
    UiApp::~UiApp() {
        Detail::UiTextureSetActiveRegistry(nullptr);
        m_renderer.reset();
        m_winsys.reset();
        m_texture_registry.reset();

        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::DestroyContext();
        }
    }

    bool UiApp::Init(const std::string& title) {
        m_winsys = std::make_unique<UiWinsysSdl>();
        m_state.app = this;

        // Initialize windowing / SDL first
        if (!m_winsys->Init(title)) {
            return false;
        }

        // Prefer SDL_GPU renderer if available, fall back to SDL_Renderer
        {
            auto renderer_gpu = std::make_unique<UiRendererSdlGpu>();
            if (renderer_gpu->Init(m_winsys->GetHandle())) {
                m_renderer = std::move(renderer_gpu);
            } else {
                auto renderer_sdl = std::make_unique<UiRendererSdl>();
                if (!renderer_sdl->Init(m_winsys->GetHandle())) {
                    return false;
                }
                m_renderer = std::move(renderer_sdl);
            }
        }

        m_winsys->Show();

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        m_texture_registry = std::make_unique<Detail::UiTextureRegistry>();

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
        Detail::UiTextureSetActiveRegistry(m_texture_registry.get());

        // Start the Dear ImGui frame
        m_renderer->NewFrame(m_winsys->ResizeRequired());
        m_winsys->NewFrame();
        ImGui::NewFrame();

        // windows
        for (auto *window: m_windows) {
            window->UiUpdate(m_state);
        }

        // Rendering
        ImGui::Render();
        m_renderer->Render();
        if (m_texture_registry) {
            m_texture_registry->CollectGarbage();
        }
        Detail::UiTextureSetActiveRegistry(nullptr);

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

    UiTexture UiApp::TextureCreate(const int width, const int height) {
        if (!m_texture_registry) {
            return {};
        }
        return m_texture_registry->CreateTexture(width, height);
    }

    bool UiApp::TextureDestroy(const UiTextureHandle handle) {
        return m_texture_registry && m_texture_registry->Destroy(handle);
    }

    UiTexture UiApp::TextureGet(const UiTextureHandle handle) const noexcept {
        return m_texture_registry ? UiTexture{m_texture_registry.get(), handle} : UiTexture{};
    }

    UiTextureHandle UiApp::TextureGetHandle(const UiTexture& texture) const noexcept {
        return texture.m_registry == m_texture_registry.get() ? texture.m_handle : UiTextureHandleInvalid;
    }

    const std::string_view UiApp::GetRenderingApiName() const{
        return m_renderer->GetApiName();
    }
}
