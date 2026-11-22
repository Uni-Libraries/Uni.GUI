#pragma once

#include <cstdint>
#include <chrono>

namespace Uni::GUI {

class UiApp;

/// Opaque element identifier scoped to the app initialization, valid until removal.
using UiElementId = std::uint64_t;
/// Reserved sentinel, never assigned to a registered element.
inline constexpr UiElementId InvalidUiElementId = 0;

/// Context passed by reference to UiElement::Update(). Do not retain this structure.
struct UiState final {
    UiApp& app;
    UiElementId element_id{InvalidUiElementId};
    std::uint64_t frame_index{}; ///< Index of the frame being attempted (first frame is 1).
    std::chrono::nanoseconds delta_time{}; ///< Elapsed since the last rendered frame, zero for the first.
};

} // namespace Uni::GUI
