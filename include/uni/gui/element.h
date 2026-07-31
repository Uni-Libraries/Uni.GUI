#pragma once

#include <uni/gui/error.h>
#include <uni/gui/frame.h>
#include <uni/gui/state.h>

namespace Uni::GUI {

class UiElement {
public:
    virtual ~UiElement() = default;
    virtual UiResult<UiElementUpdate> Update(UiState& state) = 0;
};

} // namespace Uni::GUI
