#include "internal/frame.h"

#if defined(IMGUI_ENABLE_TEST_ENGINE)
#include <imgui_internal.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdio>
#include <exception>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {
namespace {

#if defined(IMGUI_ENABLE_TEST_ENGINE)
void AddTestEngineItem(const std::string& label, const ImVec2 min, const ImVec2 max) {
    ImGuiContext& g = *GImGui;
    const ImGuiID id = ImGui::GetID(label.c_str());
    const ImRect bounds{min, max};
    IMGUI_TEST_ENGINE_ITEM_ADD(id, bounds, nullptr);
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label.c_str(), ImGuiItemStatusFlags_None);
}

[[nodiscard]] std::string TestEngineLabel(const std::string_view primitive, const std::uint64_t id,
                                          const std::string_view control = {}) {
    std::string label{primitive};
    label.push_back(' ');
    label.append(std::to_string(id));
    if (!control.empty()) {
        label.push_back(' ');
        label.append(control);
    }
    return label;
}

[[nodiscard]] Vec2 TestEngineLinkPoint(const LinkPath& path) {
    if (path.segments.empty()) return {};
    const auto& segment = path.segments[path.segments.size() / 2];
    return std::visit(
        [](const auto& primitive) {
            using Primitive = std::remove_cvref_t<decltype(primitive)>;
            if constexpr (std::same_as<Primitive, LinePathSegment>) {
                return (primitive.start + primitive.end) * 0.5f;
            } else {
                return primitive.p0 * 0.125f + primitive.p1 * 0.375f + primitive.p2 * 0.375f + primitive.p3 * 0.125f;
            }
        },
        segment.primitive);
}
#endif

struct ArcPolyline final {
    std::vector<Vec2> points;
    std::vector<float> distances;
    float length{0.0f};
};

constexpr std::size_t MaximumFlowMarkersPerLink = 4096;
constexpr std::size_t FlattenedPointsPerMarker = 2;
constexpr std::size_t MaximumFlowAdaptiveDepth = 12;

[[nodiscard]] Vec2 PrimitiveStart(const LinkPathPrimitive& primitive) noexcept {
    return std::visit(
        [](const auto& value) {
            using Primitive = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Primitive, LinePathSegment>) return value.start;
            else return value.p0;
        },
        primitive);
}

[[nodiscard]] Vec2 PrimitiveEnd(const LinkPathPrimitive& primitive) noexcept {
    return std::visit(
        [](const auto& value) {
            using Primitive = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Primitive, LinePathSegment>) return value.end;
            else return value.p3;
        },
        primitive);
}

void AppendArcPoint(ArcPolyline& output, const Vec2 point) {
    if (output.points.empty()) {
        output.points.push_back(point);
        output.distances.push_back(0.0f);
        return;
    }
    const Vec2 delta = point - output.points.back();
    const float distance = std::hypot(delta.x, delta.y);
    if (!std::isfinite(distance) || distance <= std::numeric_limits<float>::epsilon()) return;
    output.length += distance;
    output.points.push_back(point);
    output.distances.push_back(output.length);
}

[[nodiscard]] Vec2 Midpoint(const Vec2 first, const Vec2 second) noexcept {
    return {std::midpoint(first.x, second.x), std::midpoint(first.y, second.y)};
}

struct CubicHalves final {
    CubicPathSegment left;
    CubicPathSegment right;
};

[[nodiscard]] CubicHalves SplitCubic(const CubicPathSegment& cubic) noexcept {
    const Vec2 p01 = Midpoint(cubic.p0, cubic.p1);
    const Vec2 p12 = Midpoint(cubic.p1, cubic.p2);
    const Vec2 p23 = Midpoint(cubic.p2, cubic.p3);
    const Vec2 p012 = Midpoint(p01, p12);
    const Vec2 p123 = Midpoint(p12, p23);
    const Vec2 p0123 = Midpoint(p012, p123);
    return {
        .left = {cubic.p0, p01, p012, p0123},
        .right = {p0123, p123, p23, cubic.p3},
    };
}

[[nodiscard]] double DistanceSquaredToLine(const Vec2 point, const Vec2 start, const Vec2 end) noexcept {
    const double x = static_cast<double>(end.x) - start.x;
    const double y = static_cast<double>(end.y) - start.y;
    const double length_squared = x * x + y * y;
    if (length_squared == 0.0) {
        const double point_x = static_cast<double>(point.x) - start.x;
        const double point_y = static_cast<double>(point.y) - start.y;
        return point_x * point_x + point_y * point_y;
    }
    const double point_x = static_cast<double>(point.x) - start.x;
    const double point_y = static_cast<double>(point.y) - start.y;
    const double projection = std::clamp((point_x * x + point_y * y) / length_squared, 0.0, 1.0);
    const double nearest_x = point_x - x * projection;
    const double nearest_y = point_y - y * projection;
    return nearest_x * nearest_x + nearest_y * nearest_y;
}

[[nodiscard]] bool CubicFlatEnough(const CubicPathSegment& cubic, const double tolerance_squared) noexcept {
    return DistanceSquaredToLine(cubic.p1, cubic.p0, cubic.p3) <= tolerance_squared &&
           DistanceSquaredToLine(cubic.p2, cubic.p0, cubic.p3) <= tolerance_squared;
}

[[nodiscard]] std::size_t AppendCubicBounded(ArcPolyline& output, const CubicPathSegment& cubic,
                                             const double tolerance_squared, const std::size_t maximum_new_points) {
    if (maximum_new_points <= 1) {
        AppendArcPoint(output, cubic.p3);
        return 1;
    }

    struct Work final {
        CubicPathSegment cubic;
        std::size_t depth{0};
    };
    std::array<Work, MaximumFlowAdaptiveDepth + 2> pending;
    std::size_t pending_count = 1;
    std::size_t emitted = 0;
    pending[0] = {cubic, 0};
    while (pending_count != 0) {
        if (emitted + 1 >= maximum_new_points) {
            // Preserve direction and the semantic endpoint when the adaptive budget
            // is exhausted.
            AppendArcPoint(output, cubic.p3);
            return emitted + 1;
        }
        const Work current = pending[--pending_count];
        if (current.depth >= MaximumFlowAdaptiveDepth || CubicFlatEnough(current.cubic, tolerance_squared)) {
            AppendArcPoint(output, current.cubic.p3);
            ++emitted;
            continue;
        }
        const CubicHalves halves = SplitCubic(current.cubic);
        pending[pending_count++] = {halves.right, current.depth + 1};
        pending[pending_count++] = {halves.left, current.depth + 1};
    }
    return emitted;
}

[[nodiscard]] ArcPolyline FlattenLinkPathBounded(const LinkPath& path, const float tolerance,
                                                 const std::size_t point_budget) {
    ArcPolyline output;
    if (path.segments.empty() || point_budget < 2) return output;
    output.points.reserve(point_budget);
    output.distances.reserve(point_budget);
    AppendArcPoint(output, PrimitiveStart(path.segments.front().primitive));

    if (path.segments.size() >= point_budget) {
        // Retain evenly distributed route endpoints instead of walking every
        // primitive recursively.
        const std::size_t retained_segments = point_budget - 1;
        for (std::size_t slot = 0; slot < retained_segments; ++slot) {
            const long double scaled = static_cast<long double>(slot + 1) *
                                       static_cast<long double>(path.segments.size()) /
                                       static_cast<long double>(retained_segments);
            const std::size_t segment =
                std::min(static_cast<std::size_t>(std::ceil(scaled)) - 1, path.segments.size() - 1);
            AppendArcPoint(output, PrimitiveEnd(path.segments[segment].primitive));
        }
        return output;
    }

    const double tolerance_squared = static_cast<double>(tolerance) * tolerance;
    std::size_t remaining_emissions = point_budget - 1;
    for (std::size_t index = 0; index < path.segments.size(); ++index) {
        const std::size_t remaining_segments = path.segments.size() - index;
        const std::size_t reserved_for_later = remaining_segments - 1;
        const std::size_t maximum_new_points =
            remaining_emissions > reserved_for_later ? remaining_emissions - reserved_for_later : 1;
        const std::size_t emitted = std::visit(
            [&](const auto& primitive) -> std::size_t {
                using Primitive = std::remove_cvref_t<decltype(primitive)>;
                if constexpr (std::same_as<Primitive, LinePathSegment>) {
                    AppendArcPoint(output, primitive.end);
                    return 1;
                } else {
                    return AppendCubicBounded(output, primitive, tolerance_squared, maximum_new_points);
                }
            },
            path.segments[index].primitive);
        remaining_emissions -= std::min(remaining_emissions, emitted);
    }
    return output;
}

[[nodiscard]] Vec2 SamplePolyline(const ArcPolyline& path, const float distance) {
    if (path.points.empty()) return {};
    if (distance <= 0.0f) return path.points.front();
    if (distance >= path.length) return path.points.back();
    const auto upper = std::ranges::lower_bound(path.distances, distance);
    const std::size_t end = static_cast<std::size_t>(upper - path.distances.begin());
    const std::size_t start = end - 1;
    const float span = path.distances[end] - path.distances[start];
    const float amount = span > 0.0f ? (distance - path.distances[start]) / span : 0.0f;
    return path.points[start] + (path.points[end] - path.points[start]) * amount;
}

} // namespace

void EditorFrame::RegisterTestItems() {
#if defined(IMGUI_ENABLE_TEST_ENGINE)
    const auto& resolved_nodes = session.geometry.resolved_nodes;
    for (const auto& geometry : node_geometry) {
        const float right = std::max(geometry.min.x + 20.0f, geometry.collapse_min.x - 4.0f);
        AddTestEngineItem(TestEngineLabel("Node", geometry.id.Value()), geometry.min + ImVec2{6.0f, 2.0f},
                          {right, geometry.title_max.y - 2.0f});
        AddTestEngineItem(TestEngineLabel("Node", geometry.id.Value(), "collapse"), geometry.collapse_min,
                          geometry.collapse_max);
        if (!resolved_nodes.at(geometry.id).collapsed) {
            AddTestEngineItem(TestEngineLabel("Node", geometry.id.Value(), "resize"), geometry.resize_min,
                              geometry.resize_max);
        }
        for (const auto& pin : geometry.pins) {
            constexpr float PinTestRadius = 12.0f;
            AddTestEngineItem(TestEngineLabel("Pin", pin.id.Value()),
                              pin.position - ImVec2{PinTestRadius, PinTestRadius},
                              pin.position + ImVec2{PinTestRadius, PinTestRadius});
        }
    }
    for (const auto& geometry : group_geometry) {
        const float right = std::max(geometry.min.x + 20.0f, geometry.collapse_min.x - 4.0f);
        AddTestEngineItem(TestEngineLabel("Group", geometry.id.Value()), geometry.min + ImVec2{6.0f, 2.0f},
                          {right, geometry.title_max.y - 2.0f});
        AddTestEngineItem(TestEngineLabel("Group", geometry.id.Value(), "collapse"), geometry.collapse_min,
                          geometry.collapse_max);
        if (!presentation.FindGroup(geometry.id)->geometry.collapsed) {
            AddTestEngineItem(TestEngineLabel("Group", geometry.id.Value(), "resize"), geometry.resize_min,
                              geometry.resize_max);
        }
    }
    for (const LinkId link : visible_links) {
        if (const LinkPath* path = LinkPathFor(link); path != nullptr && !path->segments.empty()) {
            constexpr float LinkTestRadius = 7.0f;
            const ImVec2 position = ToScreen(TestEngineLinkPoint(*path));
            AddTestEngineItem(TestEngineLabel("Link", link.Value()), position - ImVec2{LinkTestRadius, LinkTestRadius},
                              position + ImVec2{LinkTestRadius, LinkTestRadius});
        }
    }
    for (const auto& point : route_point_geometry) {
        constexpr float RoutePointTestRadius = 8.0f;
        AddTestEngineItem(TestEngineLabel("Route point", point.point.Value()),
                          point.position - ImVec2{RoutePointTestRadius, RoutePointTestRadius},
                          point.position + ImVec2{RoutePointTestRadius, RoutePointTestRadius});
    }
#endif
}

void EditorFrame::RenderLinkFlows() {
    const std::size_t marker_limit = std::min(MaximumFlowMarkersPerLink, config.maximum_router_segments);
    const std::size_t point_budget = std::max(std::size_t{2}, marker_limit * FlattenedPointsPerMarker);
    const float marker_extent = style.link_flow_marker_radius + style.link_flow_outline_width;
    const GraphRect viewport =
        Detail::Expand(Detail::Normalize({ToGraph(canvas_origin), ToGraph(canvas_max)}), marker_extent / session.zoom);
    for (const auto& flow : session.link_flows) {
        if (flow.document_identity != document.Identity() || flow.graph != graph_id) continue;
        const LinkPath* path = LinkPathFor(flow.link);
        if (path == nullptr) continue;
        if (!Detail::Overlaps(Detail::PathBounds(*path), viewport)) continue;
        const ArcPolyline flattened = FlattenLinkPathBounded(*path, config.link_flatten_tolerance, point_budget);
        if (flattened.points.size() < 2 || flattened.length <= 0.0f) continue;

        float spacing = config.link_flow_marker_spacing / session.zoom;
        const double estimated_markers =
            std::floor(static_cast<double>(flattened.length) / static_cast<double>(spacing)) + 2.0;
        if (marker_limit > 1 && estimated_markers > static_cast<double>(marker_limit)) {
            spacing = flattened.length / static_cast<float>(marker_limit - 1);
        }
        const float phase = std::fmod(flow.elapsed * config.link_flow_speed / session.zoom, spacing);
        const float radius = style.link_flow_marker_radius;
        std::size_t marker_count = 0;
        for (float forward_distance = phase; forward_distance <= flattened.length && marker_count < marker_limit;
             forward_distance += spacing, ++marker_count) {
            const float sample_distance = flow.direction == LinkFlowDirection::OutputToInput
                                              ? forward_distance
                                              : flattened.length - forward_distance;
            const ImVec2 position = ToScreen(SamplePolyline(flattened, sample_distance));
            if (position.x + radius < canvas_origin.x || position.x - radius > canvas_max.x ||
                position.y + radius < canvas_origin.y || position.y - radius > canvas_max.y) {
                continue;
            }
            if (style.link_flow_outline_width > 0.0f) {
                draw_list->AddCircleFilled(position, radius + style.link_flow_outline_width, style.link_flow_outline,
                                           12);
            }
            draw_list->AddCircleFilled(position, radius, style.link_flow, 12);
        }
    }
}

bool EditorFrame::RenderScene() {
    const auto& cache = session.geometry;
    const auto& resolved_nodes = cache.resolved_nodes;
    draw_list = ImGui::GetWindowDrawList();
    constexpr int FirstNodeChannel = 3;
    hovered_channel = FirstNodeChannel + static_cast<int>(node_geometry.size());
    overlay_channel = hovered_channel + 1;
    splitter.Split(draw_list, overlay_channel + 1);
    splitter_active = true;

    splitter.SetCurrentChannel(draw_list, 0);
    if (config.show_grid) {
        const float spacing = std::max(config.grid_size * session.zoom, 8.0f);
        const float start_x = canvas_origin.x + std::fmod(session.pan.x, spacing);
        const float start_y = canvas_origin.y + std::fmod(session.pan.y, spacing);
        for (float x = start_x; x < canvas_max.x; x += spacing) {
            const float line = std::floor((x - canvas_origin.x - session.pan.x) / spacing);
            const ImU32 color = std::fmod(std::abs(line), 4.0f) < 0.5f ? style.grid_major : style.grid_minor;
            draw_list->AddLine({x, canvas_origin.y}, {x, canvas_max.y}, color);
        }
        for (float y = start_y; y < canvas_max.y; y += spacing) {
            const float line = std::floor((y - canvas_origin.y - session.pan.y) / spacing);
            const ImU32 color = std::fmod(std::abs(line), 4.0f) < 0.5f ? style.grid_major : style.grid_minor;
            draw_list->AddLine({canvas_origin.x, y}, {canvas_max.x, y}, color);
        }
    }

    splitter.SetCurrentChannel(draw_list, 1);
    for (const auto& geometry : group_geometry) {
        const auto& group = presentation.Groups().at(geometry.id);
        const auto& group_style = *group.style;
        const auto& group_geometry = group.geometry;
        if (!Overlaps(geometry.min, geometry.max, canvas_origin, canvas_max)) continue;
        const ImU32 fill = group_style.kind == GroupKind::Comment ? style.comment : group_style.color;
        draw_list->AddRectFilled(geometry.min, geometry.max, fill, style.node_rounding * session.zoom);
        draw_list->AddRect(geometry.min, geometry.max,
                           membership_drop_group == group.id
                               ? style.compatible
                               : (session.selected_groups.contains(group.id) ? style.selection : style.group_border),
                           style.node_rounding * session.zoom, 0,
                           membership_drop_group == group.id || session.selected_groups.contains(group.id) ? 3.0f
                                                                                                           : 1.5f);
        if (!group_style.title.empty()) {
            draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * session.zoom,
                               geometry.min + ImVec2{8.0f * session.zoom, 6.0f * session.zoom}, style.text,
                               group_style.title.c_str());
        }
        if (group_style.kind == GroupKind::Comment && !group_geometry.collapsed &&
            !group_style.body.empty() && session.zoom >= 0.5f) {
            draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * session.zoom,
                               geometry.min + ImVec2{8.0f * session.zoom, 36.0f * session.zoom}, style.text,
                               group_style.body.c_str());
        }
        const ImVec2 collapse_center = (geometry.collapse_min + geometry.collapse_max) * 0.5f;
        const float collapse_radius = 4.0f * session.zoom;
        if (group_geometry.collapsed) {
            draw_list->AddTriangleFilled(collapse_center + ImVec2{-collapse_radius, -collapse_radius},
                                         collapse_center + ImVec2{-collapse_radius, collapse_radius},
                                         collapse_center + ImVec2{collapse_radius, 0.0f}, style.text);
        } else {
            draw_list->AddTriangleFilled(collapse_center + ImVec2{-collapse_radius, -collapse_radius},
                                         collapse_center + ImVec2{collapse_radius, -collapse_radius},
                                         collapse_center + ImVec2{0.0f, collapse_radius}, style.text);
            draw_list->AddTriangleFilled(geometry.resize_max, {geometry.resize_min.x, geometry.resize_max.y},
                                         {geometry.resize_max.x, geometry.resize_min.y}, style.group_border);
        }
    }

    splitter.SetCurrentChannel(draw_list, 2);
    std::vector<LinkId> ordered_visible_links(visible_links.begin(), visible_links.end());
    std::ranges::sort(ordered_visible_links, {}, &LinkId::Value);
    const GraphRect viewport_bounds = Detail::Normalize({ToGraph(canvas_origin), ToGraph(canvas_max)});
    for (const LinkId link_id : ordered_visible_links) {
        const LinkPath* path = LinkPathFor(link_id);
        if (path == nullptr) continue;
        ImU32 color = link_id == hovered_link ? style.link_hovered : style.link;
        if (const auto link_style = presentation.Links().find(link_id);
            link_style != presentation.Links().end() && link_style->second.Style().color) {
            color = *link_style->second.Style().color;
        }
        const float width = style.link_width + (session.selected_links.contains(link_id) ? 2.0f : 0.0f);
        const GraphRect stroke_viewport = Detail::Expand(viewport_bounds, (width * 0.5f + 2.0f) / session.zoom);
        for (const auto& segment : path->segments) {
            if (!Detail::Overlaps(Detail::PrimitiveBounds(segment), stroke_viewport)) continue;
            std::visit(
                [&](const auto& primitive) {
                    using Primitive = std::remove_cvref_t<decltype(primitive)>;
                    if constexpr (std::same_as<Primitive, LinePathSegment>) {
                        draw_list->AddLine(ToScreen(primitive.start), ToScreen(primitive.end), color, width);
                    } else {
                        draw_list->AddBezierCubic(ToScreen(primitive.p0), ToScreen(primitive.p1),
                                                  ToScreen(primitive.p2), ToScreen(primitive.p3), color, width);
                    }
                },
                segment.primitive);
        }
    }
    RenderLinkFlows();

    std::unordered_map<NodeId, int, IdHash> node_channels;
    node_channels.reserve(node_geometry.size());
    for (std::size_t index = 0; index < node_geometry.size(); ++index) {
        node_channels.emplace(node_geometry[index].id, FirstNodeChannel + static_cast<int>(index));
    }
    for (auto geometry_iterator = node_geometry.rbegin(); geometry_iterator != node_geometry.rend();
         ++geometry_iterator) {
        const auto& geometry = *geometry_iterator;
        splitter.SetCurrentChannel(draw_list, node_channels.at(geometry.id));
        const auto& node = graph->nodes.at(geometry.id);
        const NodeId rendered_node_id = node.id;
        const auto& node_presentation = resolved_nodes.at(geometry.id);
        const auto* current_presentation = presentation.FindNode(geometry.id);
        const ImU32 node_color =
            current_presentation != nullptr ? current_presentation->color.value_or(style.node) : style.node;
        ImU32 header_color = style.node_header;
        if (const auto* descriptor = ui.Find(node.type)) header_color = descriptor->header_color;
        if (node.role == NodeRole::BoundaryInput || node.role == NodeRole::BoundaryOutput) {
            header_color = 0xFF5E4630U;
        } else if (node.role == NodeRole::IntergraphInput || node.role == NodeRole::IntergraphOutput) {
            header_color = 0xFF5B365EU;
        }
        const float rounding = style.node_rounding * session.zoom;
        draw_list->AddRectFilled(geometry.min, geometry.max, node_color, rounding);
        draw_list->AddRectFilled(geometry.min, geometry.title_max, header_color, rounding, ImDrawFlags_RoundCornersTop);
        const ImU32 border = session.selected_nodes.contains(geometry.id) ? style.selection : style.node_border;
        const float border_width =
            session.selected_nodes.contains(geometry.id) ? style.node_border_width + 1.5f : style.node_border_width;
        draw_list->AddRect(geometry.min, geometry.max, border, rounding, 0, border_width);
        const std::string_view title = node.display_name.empty() ? node.type.Value() : node.display_name;
        if (session.zoom >= 0.4f) {
            draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * session.zoom,
                               geometry.min + ImVec2{10.0f * session.zoom, 6.0f * session.zoom}, style.text,
                               title.data(), title.data() + title.size());
        }
        const ImVec2 collapse_center = (geometry.collapse_min + geometry.collapse_max) * 0.5f;
        if (node.subgraph && session.zoom >= 0.5f) {
            const bool asset = std::holds_alternative<GraphAssetTarget>(node.subgraph->target);
            const char* badge = asset                                                  ? "ASSET"
                                : node.subgraph->ownership == SubgraphOwnership::Owned ? "OWNED"
                                                                                       : "REF";
            const ImVec2 badge_size = ImGui::CalcTextSize(badge) * session.zoom;
            draw_list->AddText(
                ImGui::GetFont(), ImGui::GetFontSize() * session.zoom,
                {geometry.collapse_min.x - badge_size.x - 8.0f * session.zoom, geometry.min.y + 6.0f * session.zoom},
                style.text, badge);
        }
        const float collapse_radius = 4.0f * session.zoom;
        if (node_presentation.collapsed) {
            draw_list->AddTriangleFilled(collapse_center + ImVec2{-collapse_radius, -collapse_radius},
                                         collapse_center + ImVec2{-collapse_radius, collapse_radius},
                                         collapse_center + ImVec2{collapse_radius, 0.0f}, style.text);
        } else {
            draw_list->AddTriangleFilled(collapse_center + ImVec2{-collapse_radius, -collapse_radius},
                                         collapse_center + ImVec2{collapse_radius, -collapse_radius},
                                         collapse_center + ImVec2{0.0f, collapse_radius}, style.text);
            draw_list->AddTriangleFilled(geometry.resize_max, {geometry.resize_min.x, geometry.resize_max.y},
                                         {geometry.resize_max.x, geometry.resize_min.y}, style.node_border);
        }

        for (const auto& pin_geometry : geometry.pins) {
            const auto& pin = graph->pins.at(pin_geometry.id);
            const PinStyle custom_style = ResolvePinStyle(pin, pin.id == hovered_pin);
            if (ui_callback_invalidated) break;
            rendered_pin_styles.insert_or_assign(pin.id, custom_style);
            const float logical_radius =
                custom_style.radius && std::isfinite(*custom_style.radius) && *custom_style.radius > 0.0f
                    ? *custom_style.radius
                    : style.pin_radius;
            const float radius = logical_radius * session.zoom;
            const ImU32 color = custom_style.color.value_or(style.pin);
            const PinShape shape =
                custom_style.shape.value_or(pin.kind == PinKind::Execution ? PinShape::Square : PinShape::Circle);
            if (shape == PinShape::Square) {
                draw_list->AddRectFilled(pin_geometry.position - ImVec2{radius, radius},
                                         pin_geometry.position + ImVec2{radius, radius}, color, 1.0f);
            } else if (shape == PinShape::Diamond) {
                const std::array<ImVec2, 4> points{
                    pin_geometry.position + ImVec2{0.0f, -radius},
                    pin_geometry.position + ImVec2{radius, 0.0f},
                    pin_geometry.position + ImVec2{0.0f, radius},
                    pin_geometry.position + ImVec2{-radius, 0.0f},
                };
                draw_list->AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color);
            } else {
                draw_list->AddCircleFilled(pin_geometry.position, radius, color, 12);
            }
            if (!node_presentation.collapsed && pin_geometry.label.visible && session.zoom >= 0.5f) {
                const ImVec2 unscaled_text_size = ImGui::CalcTextSize(pin.label.c_str());
                const ImVec2 text_size = unscaled_text_size * session.zoom;
                const ImVec2 text_position =
                    pin_geometry.position +
                    ImVec2{pin_geometry.label.offset.x, pin_geometry.label.offset.y} * session.zoom -
                    ImVec2{
                        text_size.x * pin_geometry.label.pivot.x,
                        text_size.y * pin_geometry.label.pivot.y,
                    };
                draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * session.zoom, text_position, style.text,
                                   pin.label.c_str());
            }
        }
        if (ui_callback_invalidated) break;

        const auto* ui_descriptor = ui.Find(node.type);
        if (!node_presentation.collapsed && ui_descriptor != nullptr && ui_descriptor->draw_body &&
            geometry.body_max.x > geometry.body_min.x && geometry.body_max.y > geometry.body_min.y) {
            const DrawNodeBodyFn body_callback = ui_descriptor->draw_body;
            const Revisions callback_revisions{
                document.ModelRevision(),
                presentation.PresentationRevision(),
            };
            const std::uint64_t document_identity = document.Identity();
            const std::uint64_t presentation_identity = presentation.Identity();
            const std::uint64_t ui_identity = ui.Identity();
            const std::uint64_t ui_revision = ui.Revision();
            const ImVec2 saved_cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(geometry.body_min);
            ImGui::PushClipRect(Max(geometry.body_min, canvas_origin), Min(geometry.body_max, canvas_max), true);
            ImGui::PushID(static_cast<int>(node.id.Value() & 0xFFFFFFFFU));
            ImGui::PushID(static_cast<int>(node.id.Value() >> 32U));
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * session.zoom);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImGui::GetStyle().FramePadding * session.zoom);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImGui::GetStyle().ItemSpacing * session.zoom);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImGui::GetStyle().ItemInnerSpacing * session.zoom);
            ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetStyle().IndentSpacing * session.zoom);
            ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, ImGui::GetStyle().GrabMinSize * session.zoom);
            ImGui::PushItemWidth(std::max(geometry.body_max.x - geometry.body_min.x, 1.0f));
            const bool node_read_only = graph->read_only || node.read_only;
            NodeUiContext ui_context{
                graph_id,
                document,
                node,
                session.zoom,
                {
                    (geometry.body_max.x - geometry.body_min.x) / session.zoom,
                    (geometry.body_max.y - geometry.body_min.y) / session.zoom,
                },
                [&](std::unique_ptr<Command> command) { pending_commands.push_back(std::move(command)); },
                [&] { return document.AllocatePinId(); },
                node_read_only,
            };
            ImGui::BeginGroup();
            if (node_read_only) ImGui::BeginDisabled();
            try {
                body_callback(ui_context);
            } catch (const std::exception& exception) {
                ui_callback_invalidated = true;
                session.last_error = std::string{"Node body callback failed: "} + exception.what();
            } catch (...) {
                ui_callback_invalidated = true;
                session.last_error = "Node body callback failed with an unknown exception";
            }
            if (callback_revisions != Revisions{document.ModelRevision(), presentation.PresentationRevision()} ||
                document_identity != document.Identity() || presentation_identity != presentation.Identity() ||
                ui_identity != ui.Identity() || ui_revision != ui.Revision()) {
                ui_callback_invalidated = true;
                session.last_error = "Node UI callbacks must queue changes through NodeUiContext";
            }
            if (node_read_only) ImGui::EndDisabled();
            ImGui::EndGroup();
            if (Contains(geometry.body_min, geometry.body_max, mouse) &&
                (ImGui::IsItemHovered() || ImGui::IsAnyItemHovered())) {
                hovered_bodies.insert(rendered_node_id);
            }
            ImGui::PopItemWidth();
            ImGui::PopStyleVar(5);
            ImGui::PopFont();
            ImGui::PopID();
            ImGui::PopID();
            ImGui::PopClipRect();
            ImGui::SetCursorScreenPos(saved_cursor);
            ImGui::Dummy({0.0f, 0.0f});
            if (ui_callback_invalidated) break;
        }

        const ImVec2 saved_node_cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(geometry.min);
        ImGui::PushID(static_cast<int>(rendered_node_id.Value() & 0xFFFFFFFFU));
        ImGui::PushID(static_cast<int>(rendered_node_id.Value() >> 32U));
        ImGui::InvisibleButton("##node_input", geometry.max - geometry.min,
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                                   ImGuiButtonFlags_MouseButtonRight);
        ImGui::PopID();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(saved_node_cursor);
        ImGui::Dummy({0.0f, 0.0f});
    }
    return !ui_callback_invalidated;
}

void EditorFrame::RenderCanvasControls() {
    const ImVec2 saved_canvas_cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(canvas_origin);
    ImGui::InvisibleButton("##canvas_input", canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                               ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_AllowOverlap);
    if (ImGui::IsItemHovered()) (void)ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    ImGui::SetCursorScreenPos(saved_canvas_cursor);
    ImGui::Dummy({0.0f, 0.0f});

#if defined(IMGUI_ENABLE_TEST_ENGINE)
    const ImVec2 canvas_test_min = canvas_origin + ImVec2{12.0f, canvas_size.y - 30.0f};
    AddTestEngineItem("Canvas", canvas_test_min, canvas_test_min + ImVec2{24.0f, 18.0f});
#endif

    if (!config.show_breadcrumbs) return;
    const ImVec2 saved_cursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(canvas_origin + ImVec2{8.0f, 8.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    const auto breadcrumbs = context.Breadcrumbs();
    for (std::size_t index = 0; index < breadcrumbs.size(); ++index) {
        if (index != 0) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextUnformatted(">");
            ImGui::SameLine(0.0f, 4.0f);
        }
        if (index + 1 == breadcrumbs.size()) {
            ImGui::TextUnformatted(breadcrumbs[index].label.c_str());
        } else {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::SmallButton(breadcrumbs[index].label.c_str())) navigate_breadcrumb = index;
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();
    ImGui::SetCursorScreenPos(saved_cursor);
    ImGui::Dummy({0.0f, 0.0f});
}

} // namespace Uni::GUI::Nodes::EditorDetail
