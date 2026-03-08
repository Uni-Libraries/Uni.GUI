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
#include "ui_texture.h"
#include "ui_texture_registry.h"
#include "ui_winsys.h"

#include "uni.gui_export.h"



//
//
//

namespace Uni::GUI {
    class UNI_GUI_EXPORT UiApp{

    public:
        ~UiApp();
        bool Init(const std::string& title);
        bool Process();
        bool ProcessEvent(void* event);
        bool RegisterWindow(UiElement* ui_element);
        bool SetVsync(int interval);
        [[nodiscard]] UiTexture TextureCreate(int width, int height);
        bool TextureDestroy(UiTextureHandle handle);
        [[nodiscard]] UiTexture TextureGet(UiTextureHandle handle) const noexcept;
        [[nodiscard]] UiTextureHandle TextureGetHandle(const UiTexture& texture) const noexcept;
        const std::string_view GetRenderingApiName() const;
    private:
        std::unique_ptr<UiWinsys> m_winsys{};
        std::unique_ptr<UiRenderer> m_renderer{};
        std::unique_ptr<Detail::UiTextureRegistry> m_texture_registry{};
        std::vector<UiElement*> m_windows{};

        UiState m_state{};
    };
}
