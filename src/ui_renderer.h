#pragma once

//
// Includes
//

// stdlib
#include <string_view>


//
//
//

namespace Uni::GUI {
    class UiRenderer {
    public:
        virtual ~UiRenderer() = default;

        virtual bool Init(void* window_handle) = 0;
        virtual bool InitImgui() = 0;
        virtual void ShutdownImgui() = 0;
        virtual void NewFrame() = 0;
        virtual bool Render() = 0;
        virtual bool SetVsync(int interval) = 0;
        [[nodiscard]] virtual std::string_view GetApiName() const { return "unknown"; }
    };
}
