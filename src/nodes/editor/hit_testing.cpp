#include "internal/frame.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

PinStyle EditorFrame::ResolvePinStyle(const PinInstance& pin, const bool hovered) {
    const PinId pin_id = pin.id;
    if (!hovered) {
        if (const auto cached = base_pin_styles.find(pin_id); cached != base_pin_styles.end()) {
            return cached->second;
        }
    }
    PinStyle resolved;
    if (const auto* style_callback = ui.FindPinStyle(pin.type)) {
        const auto node = graph->nodes.find(pin.node);
        if (node != graph->nodes.end()) {
            const PinStyleFn callback = *style_callback;
            const Revisions callback_revisions{
                document.ModelRevision(),
                presentation.PresentationRevision(),
            };
            const std::uint64_t document_identity = document.Identity();
            const std::uint64_t presentation_identity = presentation.Identity();
            const std::uint64_t document_allocation = document.AllocationEpoch();
            const std::uint64_t presentation_allocation = presentation.AllocationEpoch();
            const std::uint64_t ui_identity = ui.Identity();
            const std::uint64_t ui_revision = ui.Revision();
            try {
                resolved = callback(PinStyleContext{
                    .node = node->second,
                    .pin = pin,
                    .zoom = session.zoom,
                    .hovered = hovered,
                    .connected = session.geometry.connected_pins.contains(pin_id),
                });
            } catch (const std::exception& exception) {
                ui_callback_invalidated = true;
                session.last_error = std::string{"Pin style callback failed: "} + exception.what();
            } catch (...) {
                ui_callback_invalidated = true;
                session.last_error = "Pin style callback failed with an unknown exception";
            }
            if (callback_revisions != Revisions{
                    document.ModelRevision(),
                    presentation.PresentationRevision()} ||
                document_identity != document.Identity() ||
                presentation_identity != presentation.Identity() ||
                document_allocation != document.AllocationEpoch() ||
                presentation_allocation != presentation.AllocationEpoch() ||
                ui_identity != ui.Identity() || ui_revision != ui.Revision()) {
                ui_callback_invalidated = true;
                session.last_error = "Pin style callbacks must not mutate editor state or their registry";
            }
        }
    }
    if (ui_callback_invalidated) return {};
    if (resolved.radius) *resolved.radius = std::clamp(*resolved.radius, 1.0f, 64.0f);
    if (!hovered) base_pin_styles.emplace(pin_id, resolved);
    return resolved;
}

bool EditorFrame::HitTest() {
    auto& cache = session.geometry;
    const auto& resolved_nodes = cache.resolved_nodes;
    const Vec2 graph_mouse = ToGraph(mouse);
    const float maximum_pin_hit_radius = std::max(
        18.0f,
        std::max(64.0f, style.pin_radius) * session.zoom + 10.0f) / session.zoom;
    const float conservative_hit_radius = std::max(
        maximum_pin_hit_radius,
        std::max(config.link_hit_radius, style.route_point_radius * 1.4f) / session.zoom);
    ++session.metrics.spatial_queries;
    const auto hit_entries = cache.spatial.Query(Detail::Expand(
        GraphRect{graph_mouse, graph_mouse}, conservative_hit_radius));
    session.metrics.spatial_candidates += hit_entries.size();
    std::unordered_set<NodeId, IdHash> hit_nodes;
    std::unordered_set<PinId, IdHash> hit_pins;
    std::unordered_set<GroupId, IdHash> hit_groups;
    std::unordered_set<RoutePointId, IdHash> hit_route_points;
    std::unordered_set<LinkId, IdHash> hit_links;
    for (const auto& entry : hit_entries) {
        switch (entry.kind) {
        case Detail::SpatialKind::Node: hit_nodes.insert(NodeId{entry.id}); break;
        case Detail::SpatialKind::Pin: hit_pins.insert(PinId{entry.id}); break;
        case Detail::SpatialKind::LinkSegment: hit_links.insert(LinkId{entry.id}); break;
        case Detail::SpatialKind::Group: hit_groups.insert(GroupId{entry.id}); break;
        case Detail::SpatialKind::RoutePoint: hit_route_points.insert(RoutePointId{entry.id}); break;
        }
    }
    const GraphRect graph_hit_bounds = Detail::Expand(
        GraphRect{graph_mouse, graph_mouse}, config.link_hit_radius / session.zoom);
    for (const auto& [link_id, path] : overridden_link_paths) {
        if (Detail::Overlaps(Detail::PathBounds(path), graph_hit_bounds)) hit_links.insert(link_id);
    }

    for (auto node = node_geometry.rbegin();
         canvas_hovered && node != node_geometry.rend() && !hovered_pin;
         ++node) {
        const bool occludes_lower_nodes = Contains(node->min, node->max, mouse);
        float closest_pin_distance = std::numeric_limits<float>::max();
        for (const auto& pin_geometry : node->pins) {
            if (!hit_pins.contains(pin_geometry.id) && !occludes_lower_nodes) continue;
            const auto& pin = graph->pins.at(pin_geometry.id);
            const PinStyle pin_style = ResolvePinStyle(pin, false);
            if (ui_callback_invalidated) break;
            const float radius = pin_style.radius && std::isfinite(*pin_style.radius) && *pin_style.radius > 0.0f
                ? *pin_style.radius
                : style.pin_radius;
            const auto* creating = std::get_if<CreatingLink>(&session.interaction);
            const bool acquiring_link_target = creating != nullptr && creating->dragging;
            const float pin_hit_radius = std::max(
                ScaleUi(acquiring_link_target ? 18.0f : 9.0f),
                ScaleGraph(radius) + ScaleUi(acquiring_link_target ? 10.0f : 4.0f));
            if (!Overlaps(
                    pin_geometry.position - ImVec2{pin_hit_radius, pin_hit_radius},
                    pin_geometry.position + ImVec2{pin_hit_radius, pin_hit_radius},
                    canvas_origin,
                    canvas_max)) {
                continue;
            }
            const float distance = DistanceSquared(mouse, pin_geometry.position);
            if (distance <= pin_hit_radius * pin_hit_radius && distance < closest_pin_distance) {
                hovered_pin = pin_geometry.id;
                closest_pin_distance = distance;
            }
        }
        if (occludes_lower_nodes) break;
    }
    if (ui_callback_invalidated) return false;

    if (canvas_hovered && !hovered_pin) {
        for (auto node = node_geometry.rbegin(); node != node_geometry.rend(); ++node) {
            if (!hit_nodes.contains(node->id) && !Contains(node->min, node->max, mouse)) continue;
            if (Contains(node->min, node->max, mouse)) {
                hovered_node = node->id;
                if (const auto range = header_item_ranges.find(node->id); range != header_item_ranges.end()) {
                    for (std::size_t index = range->second.second; index > range->second.first; --index) {
                        const auto& item_geometry = header_item_geometry[index - 1];
                        if (Contains(item_geometry.min, item_geometry.max, mouse)) {
                            hovered_header_item = index - 1;
                            break;
                        }
                    }
                }
                hovered_node_collapse = config.enable_node_collapse && Contains(node->collapse_min, node->collapse_max, mouse);
                hovered_node_resize = !resolved_nodes.at(node->id).collapsed &&
                    Contains(node->resize_min, node->resize_max, mouse);
                break;
            }
        }
    }

    if (canvas_hovered && !hovered_pin && !hovered_node) {
        const float radius = ScaleUi(std::max(style.route_point_radius * 1.4f, 6.0f));
        for (auto point = route_point_geometry.rbegin(); point != route_point_geometry.rend(); ++point) {
            if (!hit_route_points.contains(point->point) &&
                DistanceSquared(mouse, point->position) > radius * radius) continue;
            if (DistanceSquared(mouse, point->position) <= radius * radius) {
                hovered_route_point = point->point;
                hovered_route_link = point->link;
                break;
            }
        }
    }

    if (canvas_hovered && !hovered_pin && !hovered_node && !hovered_route_point) {
        float closest_link_distance = std::numeric_limits<float>::max();
        const float graph_hit_radius = config.link_hit_radius / session.zoom;
        const float graph_tolerance = config.link_flatten_tolerance / session.zoom;
        for (const LinkId link_id : hit_links) {
            const LinkPath* path = LinkPathFor(link_id);
            if (path == nullptr) continue;
            for (const auto& segment : path->segments) {
                const auto flattened = Detail::FlattenPathSegmentAdaptive(segment, graph_tolerance);
                if (flattened.size() > 1) session.metrics.adaptive_segments += flattened.size() - 1;
                const float distance = Detail::DistanceToPathSegmentAdaptive(
                    graph_mouse, segment, graph_tolerance, graph_hit_radius);
                const bool closer = distance < closest_link_distance;
                const bool deterministic_tie = distance == closest_link_distance &&
                    (link_id.Value() < hovered_link.Value() ||
                        (link_id == hovered_link &&
                            segment.route_point_insert_index < hovered_link_segment));
                if (distance <= graph_hit_radius && (closer || deterministic_tie)) {
                    hovered_link = link_id;
                    hovered_link_segment = segment.route_point_insert_index;
                    closest_link_distance = distance;
                }
            }
        }
    }

    if (canvas_hovered && !hovered_pin && !hovered_node && !hovered_route_point && !hovered_link) {
        for (auto group = group_geometry.rbegin(); group != group_geometry.rend(); ++group) {
            if (!hit_groups.contains(group->id) && !Contains(group->min, group->max, mouse)) continue;
            if (Contains(group->min, group->max, mouse)) {
                hovered_group = group->id;
                hovered_group_collapse = Contains(group->collapse_min, group->collapse_max, mouse);
                hovered_group_resize = !presentation.FindGroup(group->id)->geometry.collapsed &&
                    Contains(group->resize_min, group->resize_max, mouse);
                break;
            }
        }
    }

    if (const auto* dragging = std::get_if<DraggingNodes>(&session.interaction)) {
        membership_drop_active = std::sqrt(DistanceSquared(mouse, dragging->start)) >=
            ImGui::GetIO().MouseDragThreshold;
        if (membership_drop_active) {
            for (auto group = group_geometry.rbegin(); group != group_geometry.rend(); ++group) {
                if (Contains(group->min, group->max, mouse)) {
                    membership_drop_group = group->id;
                    break;
                }
            }
        }
    }
    return true;
}

bool EditorFrame::PointerOverUiBody() const {
    return std::ranges::any_of(node_geometry, [&](const NodeGeometry& geometry) {
        const auto& node = graph->nodes.at(geometry.id);
        const auto* descriptor = ui.Find(node.type);
        return !session.geometry.resolved_nodes.at(geometry.id).collapsed && descriptor != nullptr &&
            descriptor->draw_body && Contains(geometry.body_min, geometry.body_max, mouse);
    });
}

} // namespace Uni::GUI::Nodes::EditorDetail
