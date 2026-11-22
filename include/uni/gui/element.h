#pragma once

#include <uni/gui/error.h>
#include <uni/gui/frame.h>
#include <uni/gui/state.h>

namespace Uni::GUI {

/// Exclusively app-owned UI component. Callbacks run in registration order on the UI thread.
class UiElement {
public:
    virtual ~UiElement() = default;
    /// Draw this element in the current ImGui frame; state is borrowed for this call only.
    /// An error stops this iteration; exceptions become UiErrorCode::FrameRendering.
    /// Additions and removals already requested still take effect before the next iteration,
    /// including when this or a later callback fails. A removed element awaiting its turn is skipped.
    virtual UiResult<UiElementUpdate> Update(UiState& state) = 0;
};

} // namespace Uni::GUI
