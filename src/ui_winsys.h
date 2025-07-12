#pragma once

//
// Includes
//

// stdlib
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>


//
//
//

namespace Uni::GUI {
    class UiWinsys {
    public:
        virtual ~UiWinsys() = default;

        virtual bool Init(const std::string& title) = 0;

        virtual bool InitImgui() = 0;

        virtual void* GetHandle() = 0;

        virtual bool ProcessEvent(void* event) = 0;

        virtual void NewFrame() = 0;

        virtual void Show() = 0;

        virtual std::pair<size_t,size_t> ResizeRequired() = 0;
    };
}

