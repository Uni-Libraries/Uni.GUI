#pragma once

//
// Includes
//

// stdlib
#include <uni/gui/config.h>

#include "ui_scale.h"

#include <SDL3/SDL_events.h>

#include <optional>
#include <string_view>


//
//
//

namespace Uni::GUI {
    enum class UiWinsysInitResult {
        Success,
        SdlInitializationFailed,
        WindowCreationFailed,
    };

    class UiWinsys {
    public:
        virtual ~UiWinsys() = default;

        virtual UiWinsysInitResult Init(const UiAppConfig& config) = 0;

        virtual void* GetHandle() = 0;

        [[nodiscard]] virtual std::optional<Detail::UiWindowMetrics> QueryDisplayMetrics() const = 0;

        [[nodiscard]] virtual bool IsMinimized() const = 0;

        virtual bool SetTitle(std::string_view title) = 0;

        virtual bool FeedEvent(const SDL_Event& event) = 0;

        [[nodiscard]] virtual bool IsApplicationQuitEvent(const SDL_Event& event) const noexcept = 0;
        [[nodiscard]] virtual bool IsMainWindowCloseEvent(const SDL_Event& event) const noexcept = 0;

        virtual void NewFrame() = 0;

        virtual void Show() = 0;

    };
}
