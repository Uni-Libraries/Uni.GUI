#pragma once

#include <cstdint>
#include <chrono>

namespace Uni::GUI {

class UiApp;

using UiElementId = std::uint64_t;
inline constexpr UiElementId InvalidUiElementId = 0;

struct UiState final {
    UiApp& app;
    UiElementId element_id{InvalidUiElementId};
    std::uint64_t frame_index{};
    std::chrono::nanoseconds delta_time{};
};

} // namespace Uni::GUI
