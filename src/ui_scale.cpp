#include "ui_scale.h"

#include <cmath>
#include <limits>

namespace Uni::GUI::Detail {
namespace {

[[nodiscard]] bool PositiveFinite(const float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] bool NearlyEqual(const float first, const float second) noexcept {
    constexpr float Epsilon = 0.001f;
    return std::abs(first - second) <= Epsilon;
}

} // namespace

bool ValidScalingConfig(const UiScalingConfig& config) noexcept {
    switch (config.mode) {
    case UiScaleMode::Automatic:
    case UiScaleMode::Fixed:
    case UiScaleMode::Manual:
        break;
    default:
        return false;
    }
    return PositiveFinite(config.fixed_scale) && PositiveFinite(config.user_scale);
}

std::optional<float> ResolveEffectiveUiScale(
    const UiScalingConfig& config,
    const float system_ui_scale) noexcept {
    if (!ValidScalingConfig(config) || !PositiveFinite(system_ui_scale)) {
        return std::nullopt;
    }
    const float base = config.mode == UiScaleMode::Fixed ? config.fixed_scale : system_ui_scale;
    const float effective = base * config.user_scale;
    if (!PositiveFinite(effective) || effective >= 99.0f) {
        return std::nullopt;
    }
    return effective;
}

std::optional<UiDisplayMetrics> ResolveDisplayMetrics(
    const UiWindowMetrics& metrics,
    const UiScalingConfig& config,
    const std::uint64_t revision) noexcept {
    if (metrics.window_size.width <= 0 || metrics.window_size.height <= 0 ||
        metrics.framebuffer_size.width <= 0 || metrics.framebuffer_size.height <= 0 ||
        !PositiveFinite(metrics.display_scale) || !PositiveFinite(metrics.pixel_density)) {
        return std::nullopt;
    }
    const float system_ui_scale = metrics.display_scale / metrics.pixel_density;
    const auto effective = ResolveEffectiveUiScale(config, system_ui_scale);
    if (!effective) {
        return std::nullopt;
    }
    return UiDisplayMetrics{
        .window_size = metrics.window_size,
        .framebuffer_size = metrics.framebuffer_size,
        .display_scale = metrics.display_scale,
        .pixel_density = metrics.pixel_density,
        .system_ui_scale = system_ui_scale,
        .effective_ui_scale = *effective,
        .applied_ui_scale = config.mode == UiScaleMode::Manual ? 1.0f : *effective,
        .revision = revision,
    };
}

bool SameDisplayMetrics(
    const UiDisplayMetrics& first,
    const UiDisplayMetrics& second) noexcept {
    return first.window_size == second.window_size &&
        first.framebuffer_size == second.framebuffer_size &&
        NearlyEqual(first.display_scale, second.display_scale) &&
        NearlyEqual(first.pixel_density, second.pixel_density) &&
        NearlyEqual(first.system_ui_scale, second.system_ui_scale) &&
        NearlyEqual(first.effective_ui_scale, second.effective_ui_scale) &&
        NearlyEqual(first.applied_ui_scale, second.applied_ui_scale);
}

std::optional<int> ScaleInitialWindowDimension(
    const int reference_size,
    const float scale) noexcept {
    if (reference_size <= 0 || !PositiveFinite(scale)) {
        return std::nullopt;
    }
    const double scaled = std::round(static_cast<double>(reference_size) * static_cast<double>(scale));
    if (!std::isfinite(scaled) || scaled < 1.0 ||
        scaled > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }
    return static_cast<int>(scaled);
}

} // namespace Uni::GUI::Detail
