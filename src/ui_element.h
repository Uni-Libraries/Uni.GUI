#pragma once

//
// Includes
//

// stdlib
#include <memory>
#include <optional>

#include "ui_state.h"


//
// Interface
//

namespace Uni::GUI {
    // Forward declaration
    class UiApp;

    class UiElement {
    public:
        virtual ~UiElement() = default;

        virtual bool UiUpdate(UiState& state) = 0;
    };
}
