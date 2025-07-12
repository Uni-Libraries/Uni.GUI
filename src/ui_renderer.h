#pragma once

//
// Includes
//

// stdlib
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>


//
//
//

namespace Uni::GUI {
    class UiRenderer {
    public:
        virtual ~UiRenderer() = default;

        virtual bool Init(void* window_handle) = 0;
        virtual bool InitImgui() = 0;
        virtual void NewFrame(std::pair<size_t, size_t> new_size) = 0;
        virtual void Render() = 0;
        virtual bool SetVsync(int interval) = 0;
    };
}

