#pragma once

#include <cstdint>

namespace Uni::GUI {

/// How to convert reference UI units to window coordinates.
enum class UiScaleMode {
    Automatic, ///< Apply system_ui_scale multiplied by user_scale.
    Fixed,     ///< Use fixed_scale in place of system_ui_scale.
    Manual,    ///< Report the automatic effective scale without applying it to the ImGui style.
};

/// Initial scale policy. Scale factors must be positive and finite; effective scale must be below 99.
struct UiScalingConfig final {
    bool high_pixel_density{true}; ///< Request native framebuffer density independently of UI sizing.
    UiScaleMode mode{UiScaleMode::Automatic};
    float fixed_scale{1.0f};
    float user_scale{1.0f};
};

/// Width and height in the coordinate space specified by the enclosing field.
struct UiSize final {
    int width{};
    int height{};

    bool operator==(const UiSize&) const = default;
};

/// Snapshot of the main window, suitable for scaling custom drawing without counting pixel density twice.
struct UiDisplayMetrics final {
    UiSize window_size;                 ///< SDL/ImGui window coordinates.
    UiSize framebuffer_size;            ///< Physical render-target pixels.
    float display_scale{1.0f};          ///< Combined scale from reference units to framebuffer pixels.
    float pixel_density{1.0f};          ///< Framebuffer pixels per window coordinate unit.
    float system_ui_scale{1.0f};        ///< display_scale divided by pixel_density.
    float effective_ui_scale{1.0f};     ///< Selected base scale multiplied by the configured user scale.
    float applied_ui_scale{1.0f};       ///< Scale applied to the style; always 1 in manual mode.
    std::uint64_t revision{};           ///< Starts at 1 for the first valid snapshot; changes when metrics change.
};

} // namespace Uni::GUI
