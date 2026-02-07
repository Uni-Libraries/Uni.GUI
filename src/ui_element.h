#pragma once

//
// Includes
//

// stdlib
#include <memory>
#include <optional>



//
// Interface
//

namespace Uni::GUI {
    // Forward declaration
    class UiApp;

    class UiElement {
    public:
        virtual ~UiElement() = default;

        virtual bool UiUpdate(std::optional<std::shared_ptr<UiApp>>) = 0;
    };
}
