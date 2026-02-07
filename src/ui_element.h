#pragma once

//
// Includes
//


//
// Interface
//

namespace Uni::GUI {
    // Forward declaration
    class UiApp;

    class UiElement {
    public:
        virtual ~UiElement() = default;

        virtual bool UiUpdate(UiApp&) = 0;
    };
}
