#pragma once

#include <cstdint>

namespace Uni::GUI {

enum class UiScaleMode {
    Automatic,
    Fixed,
    Manual,
};

struct UiScalingConfig final {
    bool high_pixel_density{true};
    UiScaleMode mode{UiScaleMode::Automatic};
    float fixed_scale{1.0f};
    float user_scale{1.0f};
};

struct UiSize final {
    int width{};
    int height{};

    bool operator==(const UiSize&) const = default;
};

struct UiDisplayMetrics final {
    UiSize window_size;
    UiSize framebuffer_size;
    float display_scale{1.0f};
    float pixel_density{1.0f};
    float system_ui_scale{1.0f};
    float effective_ui_scale{1.0f};
    float applied_ui_scale{1.0f};
    std::uint64_t revision{};
};

} // namespace Uni::GUI
