#include "internal/frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdio>
#include <string>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

void EditorFrame::RenderDebugOverlays() {
    if (config.debug_overlays == EditorDebugOverlay::None) return;

    const auto enabled = [&](const EditorDebugOverlay overlay) {
        return HasEditorDebugOverlay(config.debug_overlays, overlay);
    };
    if (enabled(EditorDebugOverlay::NodeBounds) || enabled(EditorDebugOverlay::NodeBodyBounds) ||
        enabled(EditorDebugOverlay::PinNormals) || enabled(EditorDebugOverlay::EntityIds)) {
        for (const auto& node : node_geometry) {
            if (enabled(EditorDebugOverlay::NodeBounds)) {
                draw_list->AddRect(node.min, node.max, style.debug_node_bounds, 0.0f, 0,
                                   ScaleUi(style.debug_line_width));
            }
            if (enabled(EditorDebugOverlay::NodeBodyBounds) && node.body_max.x > node.body_min.x &&
                node.body_max.y > node.body_min.y) {
                draw_list->AddRect(node.body_min, node.body_max, style.debug_node_body_bounds, 0.0f, 0,
                                    ScaleUi(style.debug_line_width));
            }
            if (enabled(EditorDebugOverlay::PinNormals)) {
                for (const auto& pin : node.pins) {
                    const float length = std::hypot(pin.outward_normal.x, pin.outward_normal.y);
                    if (!std::isfinite(length) || length <= 0.0f) continue;
                    const ImVec2 normal{
                        pin.outward_normal.x / length,
                        pin.outward_normal.y / length,
                    };
                    draw_list->AddLine(pin.position, pin.position + normal * ScaleUi(style.debug_pin_normal_length),
                                       style.debug_pin_normals, ScaleUi(style.debug_line_width));
                    draw_list->AddCircleFilled(pin.position, ScaleUi(2.0f), style.debug_pin_normals, 8);
                }
            }
            if (enabled(EditorDebugOverlay::EntityIds)) {
                const std::string label = "Node " + std::to_string(node.id.Value());
                draw_list->AddText(node.min + ImVec2{ScaleUi(4.0f), -ImGui::GetFontSize()}, style.debug_text,
                                   label.c_str());
            }
        }
    }

    if (enabled(EditorDebugOverlay::LinkBounds) || enabled(EditorDebugOverlay::LinkRoutes) ||
        enabled(EditorDebugOverlay::EntityIds)) {
        std::vector<LinkId> ordered_links(visible_links.begin(), visible_links.end());
        std::ranges::sort(ordered_links, {}, &LinkId::Value);
        for (const LinkId link : ordered_links) {
            const LinkPath* path = LinkPathFor(link);
            if (path == nullptr || path->segments.empty()) continue;
            const GraphRect bounds = Detail::PathBounds(*path);
            if (enabled(EditorDebugOverlay::LinkBounds)) {
                draw_list->AddRect(ToScreen(bounds.min), ToScreen(bounds.max), style.debug_link_bounds, 0.0f, 0,
                                    ScaleUi(style.debug_line_width));
            }
            if (enabled(EditorDebugOverlay::LinkRoutes)) {
                for (const auto& segment : path->segments) {
                    std::visit(
                        [&](const auto& primitive) {
                            using Primitive = std::remove_cvref_t<decltype(primitive)>;
                            if constexpr (std::same_as<Primitive, LinePathSegment>) {
                                draw_list->AddLine(ToScreen(primitive.start), ToScreen(primitive.end),
                                                   style.debug_link_routes, ScaleUi(style.debug_line_width));
                                draw_list->AddCircleFilled(ToScreen(primitive.start), ScaleUi(2.0f),
                                                           style.debug_link_routes, 8);
                                draw_list->AddCircleFilled(ToScreen(primitive.end), ScaleUi(2.0f),
                                                           style.debug_link_routes, 8);
                            } else {
                                const std::array<ImVec2, 4> controls{
                                    ToScreen(primitive.p0),
                                    ToScreen(primitive.p1),
                                    ToScreen(primitive.p2),
                                    ToScreen(primitive.p3),
                                };
                                draw_list->AddPolyline(controls.data(), static_cast<int>(controls.size()),
                                                       style.debug_link_routes, ImDrawFlags_None,
                                                        ScaleUi(style.debug_line_width));
                                for (const ImVec2 point : controls) {
                                    draw_list->AddCircleFilled(point, ScaleUi(2.0f), style.debug_link_routes, 8);
                                }
                            }
                        },
                        segment.primitive);
                }
            }
            if (enabled(EditorDebugOverlay::EntityIds)) {
                const std::string label = "Link " + std::to_string(link.Value());
                draw_list->AddText(ToScreen(bounds.min), style.debug_text, label.c_str());
            }
        }
    }

    if (enabled(EditorDebugOverlay::WorldBounds) && session.geometry.has_world_bounds) {
        draw_list->AddRect(ToScreen(session.geometry.world_bounds.min), ToScreen(session.geometry.world_bounds.max),
                           style.debug_world_bounds, 0.0f, 0, ScaleUi(style.debug_line_width));
    }

    if (enabled(EditorDebugOverlay::Metrics)) {
        const EditorMetrics& metrics = session.metrics;
        std::array<char, 320> text{};
        std::snprintf(text.data(), text.size(),
                      "geometry rebuilds: %llu\nrouted links: %llu\nspatial queries: %llu\n"
                      "spatial candidates: %llu\nadaptive segments: %llu\nvisible nodes: "
                      "%llu\n"
                      "visible link segments: %llu",
                      static_cast<unsigned long long>(metrics.geometry_rebuilds),
                      static_cast<unsigned long long>(metrics.routed_links),
                      static_cast<unsigned long long>(metrics.spatial_queries),
                      static_cast<unsigned long long>(metrics.spatial_candidates),
                      static_cast<unsigned long long>(metrics.adaptive_segments),
                      static_cast<unsigned long long>(metrics.visible_nodes),
                      static_cast<unsigned long long>(metrics.visible_link_segments));
        const ImVec2 padding{ScaleUi(6.0f), ScaleUi(5.0f)};
        const ImVec2 text_size = ImGui::CalcTextSize(text.data());
        const ImVec2 panel_max = canvas_max - ImVec2{ScaleUi(8.0f), ScaleUi(8.0f)};
        const ImVec2 panel_min = panel_max - text_size - padding * 2.0f;
        draw_list->AddRectFilled(panel_min, panel_max, 0xD0101010U, ScaleUi(3.0f));
        draw_list->AddRect(panel_min, panel_max, style.debug_text, ScaleUi(3.0f));
        draw_list->AddText(panel_min + padding, style.debug_text, text.data());
    }
}

bool EditorFrame::RenderOverlays() {
    const auto& cache = session.geometry;
    splitter.SetCurrentChannel(draw_list, hovered_channel);
    if (hovered_pin && graph != nullptr) {
        const auto pin = graph->pins.find(hovered_pin);
        const auto position = pin_positions.find(hovered_pin);
        if (pin == graph->pins.end() || position == pin_positions.end()) hovered_pin = {};
    }
    if (hovered_pin && graph != nullptr) {
        const auto rendered_style = rendered_pin_styles.find(hovered_pin);
        const PinStyle pin_style = rendered_style != rendered_pin_styles.end()
                                       ? rendered_style->second
                                       : ResolvePinStyle(graph->pins.at(hovered_pin), false);
        if (ui_callback_invalidated) return false;
        const float logical_radius = pin_style.radius && std::isfinite(*pin_style.radius) && *pin_style.radius > 0.0f
                                         ? *pin_style.radius
                                         : style.pin_radius;
        draw_list->AddCircle(pin_positions.at(hovered_pin), ScaleGraph(logical_radius) + ScaleUi(4.0f),
                             style.link_hovered, 16, ScaleUi(2.0f));
    }
    for (const auto& point : route_point_geometry) {
        if (!session.selected_links.contains(point.link) && !session.selected_route_points.contains(point.point) &&
            point.point != hovered_route_point) {
            continue;
        }
        const bool selected = session.selected_route_points.contains(point.point);
        const float radius = ScaleUi(selected ? style.route_point_radius * 1.4f : style.route_point_radius);
        draw_list->AddCircleFilled(point.position, radius, selected ? style.selection : style.route_point, 12);
        draw_list->AddCircle(point.position, radius + ScaleUi(2.0f), style.background, 12, ScaleUi(1.5f));
    }

    splitter.SetCurrentChannel(draw_list, overlay_channel);
    if (const auto* marquee = std::get_if<MarqueeSelecting>(&session.interaction)) {
        const ImVec2 min = Min(marquee->start, marquee->current);
        const ImVec2 max = Max(marquee->start, marquee->current);
        draw_list->AddRectFilled(min, max, (style.selection & 0x00FFFFFFU) | 0x40000000U);
        draw_list->AddRect(min, max, style.selection, 0.0f, 0, ScaleUi(1.5f));
    }
    if (const auto* creating = std::get_if<CreatingLink>(&session.interaction);
        creating != nullptr && creating->dragging) {
        const auto origin = pin_positions.find(creating->origin);
        if (origin != pin_positions.end()) {
            ImU32 color = style.link_hovered;
            if (has_link_preview) {
                switch (link_preview.status) {
                case ConnectionResult::Status::Allowed: color = style.compatible; break;
                case ConnectionResult::Status::RequiresConversion: color = style.convertible; break;
                case ConnectionResult::Status::Rejected: color = style.rejected; break;
                }
            }
            const auto* pin = document.FindPin(graph_id, creating->origin);
            const bool origin_is_output = pin != nullptr && pin->direction == PinDirection::Output;
            const auto target = pin_positions.find(hovered_pin);
            const ImVec2 target_position =
                hovered_pin && hovered_pin != creating->origin && target != pin_positions.end() ? target->second
                                                                                                : creating->current;
            const auto origin_geometry = cache.pins.find(creating->origin);
            const auto target_geometry = cache.pins.find(hovered_pin);
            if (origin_geometry != cache.pins.end()) {
                const Vec2 free_position = ToGraph(target_position);
                const Vec2 target_normal = target_geometry != cache.pins.end()
                                               ? target_geometry->second.outward_normal
                                               : origin_geometry->second.outward_normal * -1.0f;
                const Link preview_link{
                    .id = creating->reconnect,
                    .output = origin_is_output ? creating->origin : hovered_pin,
                    .input = origin_is_output ? hovered_pin : creating->origin,
                };
                const LinkEndpoint origin_endpoint{
                    creating->origin,
                    pin != nullptr ? pin->node : NodeId{},
                    ToGraph(origin->second),
                    origin_geometry->second.outward_normal,
                };
                const LinkEndpoint free_endpoint{
                    hovered_pin,
                    target_geometry != cache.pins.end() ? graph->pins.at(hovered_pin).node : NodeId{},
                    free_position,
                    target_normal,
                };
                const LinkEndpoint output_endpoint = origin_is_output ? origin_endpoint : free_endpoint;
                const LinkEndpoint input_endpoint = origin_is_output ? free_endpoint : origin_endpoint;
                TypeId preview_router = config.default_link_router;
                if (creating->reconnect) {
                    if (const auto* state = presentation.FindLink(creating->reconnect);
                        state != nullptr && !state->Style().router.Empty()) {
                        preview_router = state->Style().router;
                    }
                }
                const auto* router = routers.Find(preview_router);
                if (router == nullptr) router = routers.Find(BezierLinkRouterType());
                if (router != nullptr) {
                    const LinkRoutingContext routing_context{
                        .graph = graph_id,
                        .link = preview_link,
                        .output = output_endpoint,
                        .input = input_endpoint,
                        .route_points = {},
                        .obstacles = cache.obstacles,
                    };
                    auto preview = InvokeRouter(*router, routing_context, 0);
                    if (!preview) {
                        session.last_error = preview.error().message;
                    } else {
                        for (const auto& segment : preview->segments) {
                            std::visit(
                                [&](const auto& primitive) {
                                    using Primitive = std::remove_cvref_t<decltype(primitive)>;
                                    if constexpr (std::same_as<Primitive, LinePathSegment>) {
                                         draw_list->AddLine(ToScreen(primitive.start), ToScreen(primitive.end), color,
                                                           ScaleUi(style.link_width));
                                    } else {
                                        draw_list->AddBezierCubic(ToScreen(primitive.p0), ToScreen(primitive.p1),
                                                                  ToScreen(primitive.p2), ToScreen(primitive.p3), color,
                                                                  ScaleUi(style.link_width));
                                    }
                                },
                                segment.primitive);
                        }
                    }
                }
            }
        }
    }
    if (router_callback_invalidated) {
        session.last_error = "Link router callbacks must not mutate editor state or their registry";
        return false;
    }
    if (membership_drop_active) {
        std::string hint;
        if (const auto* group = presentation.FindGroup(membership_drop_group)) {
            hint = "Add to " +
                (group->style->title.empty() ? std::string{"group"} : group->style->title);
        } else {
            const auto* dragging = std::get_if<DraggingNodes>(&session.interaction);
            const bool had_group =
                dragging != nullptr && std::ranges::any_of(presentation.Groups(), [&](const auto& entry) {
                    return entry.second.graph == graph_id &&
                           std::ranges::any_of(entry.second.members,
                                               [&](const NodeId node) { return dragging->before.contains(node); });
                });
            if (had_group) hint = "Remove from group";
        }
        if (!hint.empty()) draw_list->AddText(mouse + ImVec2{ScaleUi(14.0f), ScaleUi(18.0f)}, style.text,
                                              hint.c_str());
    }
    if (has_link_preview && link_preview.status != ConnectionResult::Status::Allowed && hovered_pin) {
        ImGui::SetTooltip("%s", link_preview.reason.c_str());
    }
    if (hovered_header_item) {
        const auto& geometry = header_item_geometry[*hovered_header_item];
        const auto header = node_headers.find(geometry.node);
        if (header != node_headers.end() && geometry.item_index < header->second.items.size()) {
            const auto& tooltip = header->second.items[geometry.item_index].tooltip;
            if (!tooltip.empty()) ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }
    RenderDebugOverlays();
    return true;
}

} // namespace Uni::GUI::Nodes::EditorDetail
