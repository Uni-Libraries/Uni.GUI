#pragma once

//
// Includes
//

// SRC
#include "ui_winsys.h"


//
//
//

namespace Uni::GUI {
    class UiWinsysSdl: public UiWinsys{
    public:
        ~UiWinsysSdl() override;
        UiWinsysInitResult Init(const UiAppConfig& config) override;

        void* GetHandle() override;

        [[nodiscard]] float GetDisplayScale() const override;

        [[nodiscard]] bool IsMinimized() const override;

        bool FeedEvent(const SDL_Event& event) override;

        [[nodiscard]] bool IsCloseEvent(const SDL_Event& event) const noexcept override;

        void NewFrame() override;

        void Show() override;

    private:
        void* m_sdl_window{};
        float m_display_scale{1.0f};
        bool m_sdl_initialized{};
    };
}
