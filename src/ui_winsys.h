#pragma once

//
// Includes
//

// stdlib
#include <uni/gui/config.h>

#include <SDL3/SDL_events.h>


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

        [[nodiscard]] virtual float GetDisplayScale() const = 0;

        [[nodiscard]] virtual bool IsMinimized() const = 0;

        virtual bool FeedEvent(const SDL_Event& event) = 0;

        [[nodiscard]] virtual bool IsCloseEvent(const SDL_Event& event) const noexcept = 0;

        virtual void NewFrame() = 0;

        virtual void Show() = 0;

    };
}
