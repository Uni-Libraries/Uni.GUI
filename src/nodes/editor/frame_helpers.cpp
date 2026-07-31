#include "internal/frame.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Uni::GUI::Nodes::EditorDetail {

bool Contains(const ImVec2 min, const ImVec2 max, const ImVec2 point) noexcept {
    return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
}

bool Overlaps(
    const ImVec2 first_min,
    const ImVec2 first_max,
    const ImVec2 second_min,
    const ImVec2 second_max) noexcept {
    return first_min.x <= second_max.x && first_max.x >= second_min.x &&
        first_min.y <= second_max.y && first_max.y >= second_min.y;
}

ImVec2 Min(const ImVec2 first, const ImVec2 second) noexcept {
    return {std::min(first.x, second.x), std::min(first.y, second.y)};
}

ImVec2 Max(const ImVec2 first, const ImVec2 second) noexcept {
    return {std::max(first.x, second.x), std::max(first.y, second.y)};
}

float DistanceSquared(const ImVec2 first, const ImVec2 second) noexcept {
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    return x * x + y * y;
}

std::string Lower(const std::string_view value) {
    std::string output(value);
    std::ranges::transform(output, output.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return output;
}

bool ValidConfig(const EditorConfig& config) noexcept {
    return std::isfinite(config.min_zoom) && std::isfinite(config.max_zoom) &&
        std::isfinite(config.zoom_step) && std::isfinite(config.grid_size) &&
        std::isfinite(config.node_width) && std::isfinite(config.title_height) &&
        std::isfinite(config.pin_spacing) && std::isfinite(config.link_hit_radius) &&
        std::isfinite(config.link_flatten_tolerance) && std::isfinite(config.snap_size) &&
        std::isfinite(config.link_flow_duration) && std::isfinite(config.link_flow_speed) &&
        std::isfinite(config.link_flow_marker_spacing) &&
        std::isfinite(config.minimum_node_size.x) && std::isfinite(config.minimum_node_size.y) &&
        std::isfinite(config.minimum_group_size.x) && std::isfinite(config.minimum_group_size.y) &&
        std::isfinite(config.minimap_size.x) && std::isfinite(config.minimap_size.y) &&
        config.min_zoom > 0.0f && config.min_zoom <= config.max_zoom &&
        config.zoom_step > 0.0f && config.grid_size > 0.0f && config.node_width > 0.0f &&
        config.title_height > 0.0f && config.pin_spacing > 0.0f && config.link_hit_radius >= 0.0f &&
        config.link_flatten_tolerance > 0.0f && !config.default_link_router.Empty() &&
        config.maximum_router_segments > 0 && config.snap_size > 0.0f &&
        config.link_flow_duration > 0.0f && config.link_flow_speed > 0.0f &&
        config.link_flow_marker_spacing > 0.0f &&
        config.minimum_node_size.x > 0.0f && config.minimum_node_size.y > 0.0f &&
        config.minimum_group_size.x > 0.0f && config.minimum_group_size.y > 0.0f &&
        config.minimap_size.x > 12.0f && config.minimap_size.y > 12.0f;
}

bool ValidStyle(const EditorStyle& style) noexcept {
    return std::isfinite(style.node_rounding) && std::isfinite(style.node_border_width) &&
        std::isfinite(style.link_width) && std::isfinite(style.pin_radius) &&
        std::isfinite(style.handle_size) && std::isfinite(style.link_flow_marker_radius) &&
        std::isfinite(style.link_flow_outline_width) && std::isfinite(style.debug_line_width) &&
        std::isfinite(style.debug_pin_normal_length) && style.node_rounding >= 0.0f &&
        style.node_border_width >= 0.0f && style.link_width > 0.0f &&
        style.pin_radius > 0.0f && style.handle_size > 0.0f &&
        style.link_flow_marker_radius > 0.0f && style.link_flow_outline_width >= 0.0f &&
        style.debug_line_width > 0.0f && style.debug_pin_normal_length > 0.0f;
}

bool Finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

bool Bounded(const Vec2 value) noexcept {
    return Finite(value) && std::abs(value.x) <= MaxEditorCoordinate &&
        std::abs(value.y) <= MaxEditorCoordinate;
}

Vec2 ClampPan(const Vec2 value) noexcept {
    return {
        std::clamp(value.x, -MaxEditorCoordinate, MaxEditorCoordinate),
        std::clamp(value.y, -MaxEditorCoordinate, MaxEditorCoordinate),
    };
}

float Snap(const float value, const float spacing) noexcept {
    return std::round(value / spacing) * spacing;
}

Vec2 Snap(const Vec2 value, const float spacing) noexcept {
    return {Snap(value.x, spacing), Snap(value.y, spacing)};
}

void StoreText(const std::span<char> destination, const std::string_view value) {
    std::ranges::fill(destination, '\0');
    const std::size_t count = std::min(value.size(), destination.size() - 1);
    std::ranges::copy_n(value.begin(), count, destination.begin());
}

} // namespace Uni::GUI::Nodes::EditorDetail
