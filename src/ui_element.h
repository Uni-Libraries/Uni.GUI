#pragma once

//
// Includes
//



//
// Interface
//

namespace Uni::GUI {
    class UiElement {
    public:
        virtual ~UiElement() = default;

        virtual bool UiUpdate() = 0;
    };
}
