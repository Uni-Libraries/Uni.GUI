#pragma once

#include <uni/gui/config.h>
#include <uni/gui/display.h>

#include <optional>

namespace Uni::GUI::Detail {

struct UiWindowMetrics final {
    UiSize window_size;
    UiSize framebuffer_size;
    float display_scale{1.0f};
    float pixel_density{1.0f};
};

[[nodiscard]] bool ValidScalingConfig(const UiScalingConfig& config) noexcept;
[[nodiscard]] std::optional<float> ResolveEffectiveUiScale(
    const UiScalingConfig& config,
    float system_ui_scale) noexcept;
[[nodiscard]] std::optional<UiDisplayMetrics> ResolveDisplayMetrics(
    const UiWindowMetrics& metrics,
    const UiScalingConfig& config,
    std::uint64_t revision) noexcept;
[[nodiscard]] bool SameDisplayMetrics(
    const UiDisplayMetrics& first,
    const UiDisplayMetrics& second) noexcept;
[[nodiscard]] std::optional<int> ScaleInitialWindowDimension(
    int reference_size,
    float scale) noexcept;

} // namespace Uni::GUI::Detail
