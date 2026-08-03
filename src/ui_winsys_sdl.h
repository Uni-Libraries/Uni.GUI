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

        [[nodiscard]] std::optional<Detail::UiWindowMetrics> QueryDisplayMetrics() const override;

        [[nodiscard]] bool IsMinimized() const override;

        bool SetTitle(std::string_view title) override;

        bool FeedEvent(const SDL_Event& event) override;

        [[nodiscard]] bool IsApplicationQuitEvent(const SDL_Event& event) const noexcept override;
        [[nodiscard]] bool IsMainWindowCloseEvent(const SDL_Event& event) const noexcept override;

        void NewFrame() override;

        void Show() override;

    private:
        void* m_sdl_window{};
        bool m_sdl_initialized{};
    };
}
