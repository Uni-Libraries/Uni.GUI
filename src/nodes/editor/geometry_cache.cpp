#include "internal/frame.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <span>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

Result<LinkPath> EditorFrame::InvokeRouter(
    const LinkRouterDescriptor& descriptor,
    const LinkRoutingContext& routing_context,
    const std::size_t route_point_count) {
    const LinkRouterFn callback = descriptor.callback;
    const Vec2 expected_start = routing_context.output.position;
    const Vec2 expected_end = routing_context.input.position;
    const Revisions before{document.ModelRevision(), presentation.PresentationRevision()};
    const std::uint64_t document_identity = document.Identity();
    const std::uint64_t presentation_identity = presentation.Identity();
    const std::uint64_t document_allocation = document.AllocationEpoch();
    const std::uint64_t presentation_allocation = presentation.AllocationEpoch();
    const std::uint64_t router_identity = routers.Identity();
    const std::uint64_t router_revision = routers.Revision();
    Result<LinkPath> routed = std::unexpected(Error{ErrorCode::CommandFailed, "Link router failed"});
    try {
        routed = callback(routing_context);
    } catch (const std::exception& exception) {
        routed = std::unexpected(Error{
            ErrorCode::CommandFailed,
            std::string{"Link router failed: "} + exception.what(),
        });
    } catch (...) {
        routed = std::unexpected(Error{ErrorCode::CommandFailed, "Link router failed with an unknown exception"});
    }
    if (before != Revisions{document.ModelRevision(), presentation.PresentationRevision()} ||
        document_identity != document.Identity() || presentation_identity != presentation.Identity() ||
        document_allocation != document.AllocationEpoch() ||
        presentation_allocation != presentation.AllocationEpoch() ||
        router_identity != routers.Identity() || router_revision != routers.Revision()) {
        router_callback_invalidated = true;
        return std::unexpected(Error{
            ErrorCode::RevisionConflict,
            "Link router callbacks must not mutate editor state or their registry",
        });
    }
    if (!routed) return routed;
    if (auto valid = Detail::ValidateLinkPath(
            *routed, expected_start, expected_end, config.maximum_router_segments); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    if (!std::ranges::all_of(routed->segments, [&](const LinkPathSegment& segment) {
            return segment.route_point_insert_index <= route_point_count;
        })) {
        return std::unexpected(Error{
            ErrorCode::InvalidArgument,
            "Link router returned an invalid route point index",
        });
    }
    return routed;
}

std::optional<NodeUiLayout> EditorFrame::ResolveNodeLayout(
    const NodeInstance& node,
    const Vec2 size,
    const bool collapsed,
    const Vec2 origin) {
    std::size_t input_count = 0;
    std::size_t output_count = 0;
    float input_gutter = 0.0f;
    float output_gutter = 0.0f;
    for (const PinId pin_id : node.pins) {
        const auto pin = graph->pins.find(pin_id);
        if (pin == graph->pins.end()) continue;
        if (pin->second.direction == PinDirection::Input) {
            ++input_count;
            input_gutter = std::max(input_gutter, TextSizeInGraph(pin->second.label).x + 18.0f);
        } else {
            ++output_count;
            output_gutter = std::max(output_gutter, TextSizeInGraph(pin->second.label).x + 18.0f);
        }
    }

    NodeUiLayout layout;
    layout.body = GraphRect{
        {input_gutter + 8.0f, header_height + 8.0f},
        {size.x - output_gutter - 8.0f, size.y - 8.0f},
    };
    std::size_t input_index = 0;
    std::size_t output_index = 0;
    for (const PinId pin_id : node.pins) {
        const auto pin = graph->pins.find(pin_id);
        if (pin == graph->pins.end()) continue;
        const bool input = pin->second.direction == PinDirection::Input;
        const std::size_t index = input ? input_index++ : output_index++;
        const std::size_t count = input ? input_count : output_count;
        const float y = collapsed
            ? header_height * (static_cast<float>(index) + 1.0f) /
                (static_cast<float>(count) + 1.0f)
            : header_height + config.pin_spacing * (static_cast<float>(index) + 0.5f);
        layout.pins.push_back(PinPlacement{
            .pin = pin_id,
            .position = {input ? 0.0f : size.x, y},
            .outward_normal = {input ? -1.0f : 1.0f, 0.0f},
            .label = PinLabelPlacement{
                .offset = {input ? 9.0f : -9.0f, 0.0f},
                .pivot = {input ? 0.0f : 1.0f, 0.5f},
            },
        });
    }

    const auto* descriptor = ui.Find(node.type);
    if (descriptor == nullptr || !descriptor->layout) return layout;

    const LayoutNodeUiFn callback = descriptor->layout;
    const Revisions before{document.ModelRevision(), presentation.PresentationRevision()};
    const std::uint64_t document_identity = document.Identity();
    const std::uint64_t presentation_identity = presentation.Identity();
    const std::uint64_t document_allocation = document.AllocationEpoch();
    const std::uint64_t presentation_allocation = presentation.AllocationEpoch();
    const std::uint64_t ui_identity = ui.Identity();
    const std::uint64_t ui_revision = ui.Revision();
    std::optional<Result<NodeUiLayout>> custom;
    try {
        custom.emplace(callback(NodeUiLayoutContext{
            .graph = *graph,
            .node = node,
            .node_size = size,
            .collapsed = collapsed,
            .header_height = header_height,
            .pin_spacing = config.pin_spacing,
        }));
    } catch (const std::exception& exception) {
        session.last_error = std::string{"Node layout callback failed: "} + exception.what();
    } catch (...) {
        session.last_error = "Node layout callback failed with an unknown exception";
    }
    if (before != Revisions{document.ModelRevision(), presentation.PresentationRevision()} ||
        document_identity != document.Identity() || presentation_identity != presentation.Identity() ||
        document_allocation != document.AllocationEpoch() ||
        presentation_allocation != presentation.AllocationEpoch() ||
        ui_identity != ui.Identity() || ui_revision != ui.Revision()) {
        ui_callback_invalidated = true;
        session.last_error = "Node layout callbacks must not mutate editor state or their registry";
        return std::nullopt;
    }
    if (!custom) return layout;
    if (!*custom) {
        session.last_error = custom->error().message;
        return layout;
    }

    bool valid = (*custom)->pins.size() == node.pins.size();
    std::unordered_set<PinId, IdHash> seen;
    for (auto& placement : (*custom)->pins) {
        const float length = std::hypot(placement.outward_normal.x, placement.outward_normal.y);
        const Vec2 absolute = origin + placement.position;
        const Vec2 label_anchor = absolute + placement.label.offset;
        valid = valid && graph->pins.contains(placement.pin) &&
            graph->pins.at(placement.pin).node == node.id && seen.insert(placement.pin).second &&
            Bounded(placement.position) && Bounded(absolute) && Bounded(placement.label.offset) &&
            Bounded(label_anchor) && Finite(placement.outward_normal) && length > 0.0001f &&
            Finite(placement.label.pivot) && placement.label.pivot.x >= 0.0f &&
            placement.label.pivot.x <= 1.0f && placement.label.pivot.y >= 0.0f &&
            placement.label.pivot.y <= 1.0f;
        if (placement.label.visible && graph->pins.contains(placement.pin)) {
            const Vec2 text = TextSizeInGraph(graph->pins.at(placement.pin).label);
            valid = valid && Bounded({
                label_anchor.x - text.x * placement.label.pivot.x,
                label_anchor.y - text.y * placement.label.pivot.y,
            }) && Bounded({
                label_anchor.x + text.x * (1.0f - placement.label.pivot.x),
                label_anchor.y + text.y * (1.0f - placement.label.pivot.y),
            });
        }
        if (length > 0.0001f && std::isfinite(length)) {
            placement.outward_normal = placement.outward_normal * (1.0f / length);
        }
    }
    if ((*custom)->body) {
        valid = valid && Bounded((*custom)->body->min) && Bounded((*custom)->body->max) &&
            Bounded(origin + (*custom)->body->min) && Bounded(origin + (*custom)->body->max) &&
            (*custom)->body->min.x <= (*custom)->body->max.x &&
            (*custom)->body->min.y <= (*custom)->body->max.y;
    }
    if (!valid) {
        session.last_error = "Node layout callback returned invalid pin geometry";
        return layout;
    }
    if (!(*custom)->body) (*custom)->body = layout.body;
    return std::move(**custom);
}

bool EditorFrame::EnsureGeometryCache() {
    auto& cache = session.geometry;
    const SemanticRevisionSet graph_revisions = document.GraphRevisions(graph_id);
    const bool cache_matches = cache.valid && cache.graph == graph_id &&
        cache.document_identity == document.Identity() &&
        cache.presentation_identity == presentation.Identity() &&
        cache.ui_identity == ui.Identity() && cache.router_identity == routers.Identity() &&
        cache.topology_revision == graph_revisions.topology &&
        cache.layout_revision == graph_revisions.layout &&
        cache.presentation_geometry_revision == presentation.GeometryRevision() &&
        cache.ui_layout_revision == ui.LayoutRevision() && cache.router_revision == routers.Revision() &&
        cache.manual_revision == session.manual_geometry_revision && cache.font == ImGui::GetFont() &&
        cache.reference_font_size == ImGui::GetFontSize() / ui_scale && cache.node_width == config.node_width &&
        cache.header_height == header_height && cache.header_layout == config.node_header &&
        cache.pin_spacing == config.pin_spacing &&
        cache.resize_handle_size == style.resize_handle_size &&
        cache.minimum_node_size == config.minimum_node_size &&
        cache.default_router == config.default_link_router &&
        cache.maximum_router_segments == config.maximum_router_segments &&
        cache.enable_node_collapse == config.enable_node_collapse;
    if (cache_matches) return true;

    GeometryCache rebuilt;
    rebuilt.graph = graph_id;
    rebuilt.document_identity = document.Identity();
    rebuilt.presentation_identity = presentation.Identity();
    rebuilt.ui_identity = ui.Identity();
    rebuilt.router_identity = routers.Identity();
    rebuilt.topology_revision = graph_revisions.topology;
    rebuilt.layout_revision = graph_revisions.layout;
    rebuilt.presentation_geometry_revision = presentation.GeometryRevision();
    rebuilt.ui_layout_revision = ui.LayoutRevision();
    rebuilt.router_revision = routers.Revision();
    rebuilt.manual_revision = session.manual_geometry_revision;
    rebuilt.font = ImGui::GetFont();
    rebuilt.reference_font_size = ImGui::GetFontSize() / ui_scale;
    rebuilt.node_width = config.node_width;
    rebuilt.header_height = header_height;
    rebuilt.header_layout = config.node_header;
    rebuilt.pin_spacing = config.pin_spacing;
    rebuilt.resize_handle_size = style.resize_handle_size;
    rebuilt.minimum_node_size = config.minimum_node_size;
    rebuilt.default_router = config.default_link_router;
    rebuilt.maximum_router_segments = config.maximum_router_segments;
    rebuilt.enable_node_collapse = config.enable_node_collapse;

    for (const auto& [group_id, group_state] : presentation.Groups()) {
        (void)group_id;
        if (group_state.graph == graph_id && group_state.geometry.collapsed) {
            rebuilt.hidden_nodes.insert(group_state.members.begin(), group_state.members.end());
        }
    }
    rebuilt.ordered_nodes.reserve(graph->nodes.size());
    for (const auto& [node_id, node] : graph->nodes) {
        (void)node;
        rebuilt.ordered_nodes.push_back(node_id);
    }
    std::ranges::sort(rebuilt.ordered_nodes, {}, &NodeId::Value);
    rebuilt.resolved_nodes.reserve(rebuilt.ordered_nodes.size());
    for (std::size_t index = 0; index < rebuilt.ordered_nodes.size(); ++index) {
        const NodeId node = rebuilt.ordered_nodes[index];
        NodePresentation state{
            .position = {
                static_cast<float>(index % 4) * 240.0f,
                static_cast<float>(index / 4) * 160.0f,
            },
            .z_order = node.Value(),
        };
        if (const auto* persisted = presentation.FindNode(node);
            persisted != nullptr && Bounded(persisted->position) && Bounded(persisted->size) &&
            persisted->size.x >= 0.0f && persisted->size.y >= 0.0f) {
            state = *persisted;
        }
        if (!config.enable_node_collapse)
            state.collapsed = false;
        rebuilt.resolved_nodes.emplace(node, std::move(state));
    }
    std::ranges::sort(rebuilt.ordered_nodes, [&](const NodeId first, const NodeId second) {
        const auto& a = rebuilt.resolved_nodes.at(first);
        const auto& b = rebuilt.resolved_nodes.at(second);
        return a.z_order == b.z_order ? first.Value() < second.Value() : a.z_order < b.z_order;
    });

    const auto include_world = [&](const GraphRect bounds) {
        if (!rebuilt.has_world_bounds) {
            rebuilt.world_bounds = bounds;
            rebuilt.has_world_bounds = true;
        } else {
            rebuilt.world_bounds = Detail::Union(rebuilt.world_bounds, bounds);
        }
    };
    for (const NodeId node_id : rebuilt.ordered_nodes) {
        if (rebuilt.hidden_nodes.contains(node_id)) continue;
        const auto& node = graph->nodes.at(node_id);
        const auto& state = rebuilt.resolved_nodes.at(node_id);
        std::size_t input_count = 0;
        std::size_t output_count = 0;
        for (const PinId pin_id : node.pins) {
            const auto pin = graph->pins.find(pin_id);
            if (pin == graph->pins.end()) continue;
            pin->second.direction == PinDirection::Input ? ++input_count : ++output_count;
        }
        const auto* descriptor = ui.Find(node.type);
        const Vec2 minimum_size = MinimumNodeSize();
        const float requested_width = state.size.x > 0.0f
            ? state.size.x
            : std::max(config.node_width, descriptor != nullptr ? descriptor->default_size.x : 0.0f);
        const float width = std::max(requested_width, minimum_size.x);
        const float pin_rows = static_cast<float>(std::max(input_count, output_count));
        float automatic_height = state.collapsed
            ? header_height
            : std::max(header_height + 12.0f,
                header_height + pin_rows * config.pin_spacing + 10.0f);
        if (descriptor != nullptr) automatic_height = std::max(automatic_height, descriptor->default_size.y);
        const float height = state.collapsed
            ? header_height
            : std::max(state.size.y > 0.0f ? state.size.y : automatic_height, minimum_size.y);
        const Vec2 size{width, height};
        auto layout = ResolveNodeLayout(node, size, state.collapsed, state.position);
        if (ui_callback_invalidated || !layout) return false;

        const auto translate = [&](const GraphRect local) {
            return GraphRect{local.min + state.position, local.max + state.position};
        };
        CachedNodeGeometry geometry{
            .id = node_id,
            .bounds = {state.position, state.position + size},
            .spatial_bounds = {state.position, state.position + size},
            .title = {state.position, state.position + Vec2{width, header_height}},
            .body = translate(layout->body.value_or(GraphRect{})),
            .collapse = {
                state.position + Vec2{width - config.node_header.collapse_width, 2.0f},
                state.position + Vec2{width - 2.0f, header_height - 2.0f},
            },
            .resize = {
                state.position + size - Vec2{style.resize_handle_size, style.resize_handle_size},
                state.position + size,
            },
        };
        geometry.pins.reserve(layout->pins.size());
        for (const auto& placement : layout->pins) {
            CachedPinGeometry pin{
                .id = placement.pin,
                .position = state.position + placement.position,
                .outward_normal = placement.outward_normal,
                .label = placement.label,
            };
            geometry.pins.push_back(pin);
            rebuilt.pins.emplace(pin.id, pin);
            geometry.spatial_bounds = Detail::Union(
                geometry.spatial_bounds, GraphRect{pin.position, pin.position});
            const auto semantic_pin = graph->pins.find(pin.id);
            if (!state.collapsed && pin.label.visible && semantic_pin != graph->pins.end()) {
                const Vec2 text = TextSizeInGraph(semantic_pin->second.label);
                const Vec2 anchor = pin.position + pin.label.offset;
                geometry.spatial_bounds = Detail::Union(geometry.spatial_bounds, GraphRect{
                    .min = {
                        anchor.x - text.x * pin.label.pivot.x,
                        anchor.y - text.y * pin.label.pivot.y,
                    },
                    .max = {
                        anchor.x + text.x * (1.0f - pin.label.pivot.x),
                        anchor.y + text.y * (1.0f - pin.label.pivot.y),
                    },
                });
            }
        }
        if (geometry.body.min.x <= geometry.body.max.x && geometry.body.min.y <= geometry.body.max.y) {
            geometry.spatial_bounds = Detail::Union(geometry.spatial_bounds, geometry.body);
        }
        include_world(geometry.spatial_bounds);
        rebuilt.nodes.emplace(node_id, std::move(geometry));
    }

    for (const auto& [group_id, group_state] : presentation.Groups()) {
        if (group_state.graph == graph_id) rebuilt.ordered_groups.push_back(group_id);
    }
    std::ranges::sort(rebuilt.ordered_groups, [&](const GroupId first, const GroupId second) {
        const auto& a = presentation.Groups().at(first);
        const auto& b = presentation.Groups().at(second);
        return a.geometry.z_order == b.geometry.z_order
            ? first.Value() < second.Value()
            : a.geometry.z_order < b.geometry.z_order;
    });
    for (const GroupId group_id : rebuilt.ordered_groups) {
        const auto& state = presentation.Groups().at(group_id);
        const auto& group = state.geometry;
        const Vec2 visible_size = group.collapsed ? Vec2{group.size.x, 30.0f} : group.size;
        CachedGroupGeometry geometry{
            .id = group_id,
            .bounds = {group.position, group.position + visible_size},
            .title = {group.position, group.position + Vec2{group.size.x, std::min(30.0f, visible_size.y)}},
            .collapse = {
                group.position + Vec2{group.size.x - 26.0f, 2.0f},
                group.position + Vec2{group.size.x - 2.0f, 26.0f},
            },
            .resize = {
                group.position + visible_size - Vec2{style.resize_handle_size, style.resize_handle_size},
                group.position + visible_size,
            },
        };
        include_world(geometry.bounds);
        rebuilt.groups.emplace(group_id, geometry);
    }

    rebuilt.obstacles.reserve(rebuilt.nodes.size());
    for (const auto& [node_id, geometry] : rebuilt.nodes) {
        rebuilt.obstacles.push_back({node_id, geometry.bounds});
    }
    std::ranges::sort(rebuilt.obstacles, {}, [](const RoutingObstacle& obstacle) {
        return obstacle.node.Value();
    });
    for (std::size_t index = 0; index < rebuilt.obstacles.size(); ++index) {
        rebuilt.obstacle_indices.emplace(rebuilt.obstacles[index].node, index);
    }
    for (const auto& [link_id, link] : graph->links) {
        rebuilt.connected_pins.insert(link.output);
        rebuilt.connected_pins.insert(link.input);
        const auto output = rebuilt.pins.find(link.output);
        const auto input = rebuilt.pins.find(link.input);
        if (output == rebuilt.pins.end() || input == rebuilt.pins.end()) continue;
        const auto output_pin = graph->pins.find(link.output);
        const auto input_pin = graph->pins.find(link.input);
        if (output_pin == graph->pins.end() || input_pin == graph->pins.end()) continue;
        const auto* state = presentation.FindLink(link_id);
        const PersistentRoutePointSequence route_points = state != nullptr
            ? state->Route()
            : PersistentRoutePointSequence{};
        const TypeId& requested_router = state != nullptr && !state->Style().router.Empty()
            ? state->Style().router
            : config.default_link_router;
        const LinkRouterDescriptor* router = routers.Find(requested_router);
        if (router == nullptr) {
            session.last_error = "Unknown link router: " + requested_router.Value();
            router = routers.Find(BezierLinkRouterType());
        }
        if (router == nullptr) continue;
        const LinkRoutingContext routing_context{
            .graph = graph_id,
            .link = link,
            .output = {link.output, output_pin->second.node, output->second.position,
                output->second.outward_normal},
            .input = {link.input, input_pin->second.node, input->second.position,
                input->second.outward_normal},
            .route_points = route_points,
            .obstacles = rebuilt.obstacles,
        };
        const TypeId selected_router = router->type;
        Result<LinkPath> routed = InvokeRouter(*router, routing_context, route_points.size());
        ++session.metrics.routed_links;
        if (router_callback_invalidated) {
            session.last_error = routed.error().message;
            return false;
        }
        if (!routed) {
            session.last_error = routed.error().message;
            const auto* fallback = routers.Find(BezierLinkRouterType());
            if (fallback == nullptr || selected_router == BezierLinkRouterType()) continue;
            routed = InvokeRouter(*fallback, routing_context, route_points.size());
            ++session.metrics.routed_links;
            if (router_callback_invalidated) {
                session.last_error = routed.error().message;
                return false;
            }
            if (!routed) continue;
        }
        CachedLinkGeometry geometry{
            .id = link_id,
            .path = std::move(*routed),
            .output_node = output_pin->second.node,
            .input_node = input_pin->second.node,
        };
        geometry.bounds = Detail::PathBounds(geometry.path);
        include_world(geometry.bounds);
        rebuilt.links.emplace(link_id, std::move(geometry));
        for (const RoutePoint& point : route_points) {
            rebuilt.route_points.emplace(point.id, CachedRoutePointGeometry{link_id, point.id, point.position});
        }
    }

    std::vector<Detail::SpatialEntry> entries;
    entries.reserve(rebuilt.nodes.size() + rebuilt.pins.size() + rebuilt.groups.size() +
        rebuilt.route_points.size() + graph->links.size() * 2);
    for (const auto& [node_id, geometry] : rebuilt.nodes) {
        entries.push_back({geometry.spatial_bounds, Detail::SpatialKind::Node, node_id.Value(), 0});
        for (const auto& pin : geometry.pins) {
            entries.push_back({{pin.position, pin.position}, Detail::SpatialKind::Pin, pin.id.Value(), 0});
        }
    }
    for (const auto& [group_id, geometry] : rebuilt.groups) {
        entries.push_back({geometry.bounds, Detail::SpatialKind::Group, group_id.Value(), 0});
    }
    for (const auto& [point_id, point] : rebuilt.route_points) {
        entries.push_back({{point.position, point.position}, Detail::SpatialKind::RoutePoint,
            point_id.Value(), 0});
    }
    for (const auto& [link_id, geometry] : rebuilt.links) {
        for (std::size_t index = 0; index < geometry.path.segments.size(); ++index) {
            entries.push_back({Detail::PrimitiveBounds(geometry.path.segments[index]),
                Detail::SpatialKind::LinkSegment, link_id.Value(), static_cast<std::uint32_t>(index)});
        }
    }
    if (auto built = rebuilt.spatial.Build(entries); !built) {
        session.last_error = built.error().message;
        return false;
    }
    rebuilt.valid = true;
    cache = std::move(rebuilt);
    ++session.metrics.geometry_rebuilds;
    return true;
}

Vec2 EditorFrame::NodeOffset(const NodeId node) const {
    if (const auto* dragging = std::get_if<DraggingNodes>(&session.interaction)) {
        if (dragging->before.contains(node)) return dragging->delta;
    }
    if (const auto* dragging = std::get_if<DraggingGroup>(&session.interaction)) {
        if (dragging->member_positions.contains(node)) return dragging->delta;
    }
    return {};
}

std::optional<CachedPinGeometry> EditorFrame::CurrentPin(const PinId pin_id) const {
    const auto& cache = session.geometry;
    const auto found = cache.pins.find(pin_id);
    const auto semantic = graph->pins.find(pin_id);
    if (found == cache.pins.end() || semantic == graph->pins.end()) return std::nullopt;
    auto pin = found->second;
    const NodeId node_id = semantic->second.node;
    const auto node = cache.nodes.find(node_id);
    if (node == cache.nodes.end()) return std::nullopt;
    if (resized_layout && resized_layout_node == node_id) {
        const auto placement = std::ranges::find(resized_layout->pins, pin_id, &PinPlacement::pin);
        if (placement == resized_layout->pins.end()) return std::nullopt;
        pin.position = node->second.bounds.min + NodeOffset(node_id) + placement->position;
        pin.outward_normal = placement->outward_normal;
        pin.label = placement->label;
        return pin;
    }
    pin.position = pin.position + NodeOffset(node_id);
    return pin;
}

std::optional<GraphRect> EditorFrame::CurrentNodeBounds(const NodeId node_id) const {
    const auto& cache = session.geometry;
    const auto node = cache.nodes.find(node_id);
    if (node == cache.nodes.end()) return std::nullopt;
    const Vec2 offset = NodeOffset(node_id);
    GraphRect bounds{node->second.bounds.min + offset, node->second.bounds.max + offset};
    if (const auto* resizing = std::get_if<ResizingNode>(&session.interaction);
        resizing != nullptr && resizing->node == node_id) {
        bounds.max = bounds.min + resizing->current;
    }
    return bounds;
}

const LinkPath* EditorFrame::LinkPathFor(const LinkId link) const {
    if (suppressed_links.contains(link)) return nullptr;
    if (const auto overridden = overridden_link_paths.find(link); overridden != overridden_link_paths.end()) {
        return &overridden->second;
    }
    const auto cached = session.geometry.links.find(link);
    return cached != session.geometry.links.end() ? &cached->second.path : nullptr;
}

bool EditorFrame::BuildTransientGeometry() {
    auto& cache = session.geometry;
    const auto& resolved_nodes = cache.resolved_nodes;
    node_geometry.clear();
    group_geometry.clear();
    route_point_geometry.clear();
    pin_positions.clear();
    visible_links.clear();
    overridden_link_paths.clear();
    suppressed_links.clear();
    resized_layout.reset();
    resized_layout_node = {};
    dynamic_callback_invalidated = false;

    if (const auto* resizing = std::get_if<ResizingNode>(&session.interaction)) {
        const auto semantic = graph->nodes.find(resizing->node);
        const auto cached = cache.nodes.find(resizing->node);
        if (semantic != graph->nodes.end() && cached != cache.nodes.end()) {
            resized_layout_node = resizing->node;
            resized_layout = ResolveNodeLayout(
                semantic->second,
                resizing->current,
                resolved_nodes.at(resizing->node).collapsed,
                cached->second.bounds.min);
            if (ui_callback_invalidated) return false;
        }
    }

    const GraphRect viewport = Detail::Normalize({ToGraph(canvas_origin), ToGraph(canvas_max)});
    const float viewport_margin = std::max(
        std::max(64.0f, style.pin_radius) + 10.0f / session.zoom,
        (style.link_width + 4.0f) / session.zoom);
    ++session.metrics.spatial_queries;
    const auto candidates = cache.spatial.Query(Detail::Expand(viewport, viewport_margin));
    session.metrics.spatial_candidates += candidates.size();
    std::unordered_set<NodeId, IdHash> visible_nodes;
    std::unordered_set<NodeId, IdHash> affected_nodes;
    std::unordered_set<LinkId, IdHash> reroute_links;
    std::unordered_set<GroupId, IdHash> visible_groups;
    std::unordered_set<RoutePointId, IdHash> visible_points;
    for (const auto& candidate : candidates) {
        switch (candidate.kind) {
        case Detail::SpatialKind::Node: visible_nodes.insert(NodeId{candidate.id}); break;
        case Detail::SpatialKind::LinkSegment: visible_links.insert(LinkId{candidate.id}); break;
        case Detail::SpatialKind::Group: visible_groups.insert(GroupId{candidate.id}); break;
        case Detail::SpatialKind::RoutePoint: visible_points.insert(RoutePointId{candidate.id}); break;
        case Detail::SpatialKind::Pin:
            if (const auto pin = graph->pins.find(PinId{candidate.id}); pin != graph->pins.end()) {
                visible_nodes.insert(pin->second.node);
            }
            break;
        }
    }
    if (const auto* dragging = std::get_if<DraggingNodes>(&session.interaction)) {
        for (const auto& [node, before] : dragging->before) {
            (void)before;
            visible_nodes.insert(node);
            affected_nodes.insert(node);
        }
    }
    if (const auto* dragging = std::get_if<DraggingGroup>(&session.interaction)) {
        visible_groups.insert(dragging->group);
        for (const auto& [node, before] : dragging->member_positions) {
            (void)before;
            visible_nodes.insert(node);
            affected_nodes.insert(node);
        }
    }
    if (const auto* resizing = std::get_if<ResizingNode>(&session.interaction)) {
        visible_nodes.insert(resizing->node);
        affected_nodes.insert(resizing->node);
    }
    if (const auto* resizing = std::get_if<ResizingGroup>(&session.interaction)) {
        visible_groups.insert(resizing->group);
    }
    if (const auto* dragging = std::get_if<DraggingRoutePoint>(&session.interaction)) {
        visible_points.insert(dragging->point);
        visible_links.insert(dragging->link);
        reroute_links.insert(dragging->link);
    }
    if (!affected_nodes.empty()) {
        for (const auto& [link_id, link] : graph->links) {
            const auto output = graph->pins.find(link.output);
            const auto input = graph->pins.find(link.input);
            const bool incident =
                (output != graph->pins.end() && affected_nodes.contains(output->second.node)) ||
                (input != graph->pins.end() && affected_nodes.contains(input->second.node));
            TypeId router_type = config.default_link_router;
            if (const auto* state = presentation.FindLink(link_id);
                state != nullptr && !state->Style().router.Empty()) {
                router_type = state->Style().router;
            }
            const auto* router = routers.Find(router_type);
            const bool obstacle_dependent = router != nullptr && router->obstacle_aware;
            if (incident) {
                visible_links.insert(link_id);
                reroute_links.insert(link_id);
            }
            if (obstacle_dependent) reroute_links.insert(link_id);
        }
    }

    std::vector<std::pair<std::size_t, GraphRect>> restored_obstacles;
    restored_obstacles.reserve(affected_nodes.size());
    for (const NodeId node : affected_nodes) {
        const auto index = cache.obstacle_indices.find(node);
        const auto bounds = CurrentNodeBounds(node);
        if (index == cache.obstacle_indices.end() || !bounds) continue;
        restored_obstacles.emplace_back(index->second, cache.obstacles[index->second].bounds);
        cache.obstacles[index->second].bounds = *bounds;
    }

    for (const NodeId node_id : cache.ordered_nodes) {
        if (!visible_nodes.contains(node_id)) continue;
        const auto found = cache.nodes.find(node_id);
        if (found == cache.nodes.end()) continue;
        const auto& source = found->second;
        const Vec2 offset = NodeOffset(node_id);
        Vec2 graph_min = source.bounds.min + offset;
        Vec2 graph_max = source.bounds.max + offset;
        GraphRect title{source.title.min + offset, source.title.max + offset};
        GraphRect body{source.body.min + offset, source.body.max + offset};
        GraphRect collapse{source.collapse.min + offset, source.collapse.max + offset};
        GraphRect spatial{source.spatial_bounds.min + offset, source.spatial_bounds.max + offset};
        std::vector<CachedPinGeometry> frame_pins = source.pins;
        const bool resizing_current = resized_layout && resized_layout_node == node_id;
        if (resizing_current) {
            const auto* resizing = std::get_if<ResizingNode>(&session.interaction);
            graph_max = graph_min + resizing->current;
            title = {graph_min, {graph_max.x, graph_min.y + header_height}};
            const GraphRect local_body = resized_layout->body.value_or(GraphRect{});
            body = {graph_min + local_body.min, graph_min + local_body.max};
            collapse = {
                graph_min + Vec2{resizing->current.x - config.node_header.collapse_width, 2.0f},
                graph_min + Vec2{resizing->current.x - 2.0f, header_height - 2.0f},
            };
            spatial = {graph_min, graph_max};
            frame_pins.clear();
            for (const auto& placement : resized_layout->pins) {
                CachedPinGeometry pin{
                    .id = placement.pin,
                    .position = graph_min + placement.position,
                    .outward_normal = placement.outward_normal,
                    .label = placement.label,
                };
                frame_pins.push_back(pin);
                spatial = Detail::Union(spatial, GraphRect{pin.position, pin.position});
                const auto semantic_pin = graph->pins.find(pin.id);
                if (!resolved_nodes.at(node_id).collapsed && pin.label.visible &&
                    semantic_pin != graph->pins.end()) {
                    const Vec2 text = TextSizeInGraph(semantic_pin->second.label);
                    const Vec2 anchor = pin.position + pin.label.offset;
                    spatial = Detail::Union(spatial, GraphRect{
                        {
                            anchor.x - text.x * pin.label.pivot.x,
                            anchor.y - text.y * pin.label.pivot.y,
                        },
                        {
                            anchor.x + text.x * (1.0f - pin.label.pivot.x),
                            anchor.y + text.y * (1.0f - pin.label.pivot.y),
                        },
                    });
                }
            }
            if (body.min.x <= body.max.x && body.min.y <= body.max.y) {
                spatial = Detail::Union(spatial, body);
            }
        } else if (offset != Vec2{}) {
            for (auto& pin : frame_pins) pin.position = pin.position + offset;
        }
        NodeGeometry geometry{
            .id = node_id,
            .min = ToScreen(graph_min),
            .max = ToScreen(graph_max),
            .visible_min = ToScreen(spatial.min),
            .visible_max = ToScreen(spatial.max),
            .title_max = ToScreen(title.max),
            .body_min = ToScreen(body.min),
            .body_max = ToScreen(body.max),
            .collapse_min = ToScreen(collapse.min),
            .collapse_max = ToScreen(collapse.max),
            .resize_min = ToScreen(graph_max - Vec2{style.resize_handle_size, style.resize_handle_size}),
            .resize_max = ToScreen(graph_max),
        };
        for (const auto& pin : frame_pins) {
            geometry.pins.push_back({pin.id, ToScreen(pin.position), pin.outward_normal, pin.label});
            pin_positions.insert_or_assign(pin.id, ToScreen(pin.position));
        }
        if (Overlaps(geometry.visible_min, geometry.visible_max, canvas_origin, canvas_max) ||
            visible_nodes.contains(node_id)) {
            node_geometry.push_back(std::move(geometry));
        }
    }
    for (const GroupId group_id : cache.ordered_groups) {
        if (!visible_groups.contains(group_id)) continue;
        const auto& source = cache.groups.at(group_id);
        Vec2 graph_min = source.bounds.min;
        Vec2 graph_max = source.bounds.max;
        if (const auto* dragging = std::get_if<DraggingGroup>(&session.interaction);
            dragging != nullptr && dragging->group == group_id) {
            graph_min = dragging->before + dragging->delta;
            graph_max = graph_min + (source.bounds.max - source.bounds.min);
        }
        if (const auto* resizing = std::get_if<ResizingGroup>(&session.interaction);
            resizing != nullptr && resizing->group == group_id) {
            graph_max = graph_min + resizing->current;
        }
        group_geometry.push_back(GroupGeometry{
            .id = group_id,
            .min = ToScreen(graph_min),
            .max = ToScreen(graph_max),
            .title_max = ToScreen({graph_max.x, std::min(graph_max.y, graph_min.y + 30.0f)}),
            .collapse_min = ToScreen({graph_max.x - 26.0f, graph_min.y + 2.0f}),
            .collapse_max = ToScreen({graph_max.x - 2.0f, graph_min.y + 26.0f}),
            .resize_min = ToScreen(graph_max - Vec2{style.resize_handle_size, style.resize_handle_size}),
            .resize_max = ToScreen(graph_max),
        });
    }
    for (const RoutePointId point_id : visible_points) {
        const auto found = cache.route_points.find(point_id);
        if (found == cache.route_points.end()) continue;
        Vec2 position = found->second.position;
        if (const auto* dragging = std::get_if<DraggingRoutePoint>(&session.interaction);
            dragging != nullptr && dragging->point == point_id) {
            position = dragging->current;
        }
        route_point_geometry.push_back({found->second.link, point_id, ToScreen(position)});
    }

    for (const LinkId link_id : reroute_links) {
        const auto semantic = graph->links.find(link_id);
        const auto cached = cache.links.find(link_id);
        if (semantic == graph->links.end() || cached == cache.links.end()) {
            suppressed_links.insert(link_id);
            continue;
        }
        const auto output_pin = graph->pins.find(semantic->second.output);
        const auto input_pin = graph->pins.find(semantic->second.input);
        const auto* dragging_point = std::get_if<DraggingRoutePoint>(&session.interaction);
        const bool route_changed = dragging_point != nullptr && dragging_point->link == link_id;
        const auto output = CurrentPin(semantic->second.output);
        const auto input = CurrentPin(semantic->second.input);
        if (!output || !input || output_pin == graph->pins.end() || input_pin == graph->pins.end()) {
            suppressed_links.insert(link_id);
            continue;
        }
        PersistentRoutePointSequence route_points;
        TypeId router_type = config.default_link_router;
        if (const auto* state = presentation.FindLink(link_id)) {
            route_points = state->Route();
            if (!state->Style().router.Empty()) router_type = state->Style().router;
        }
        if (route_changed) {
            auto moved = route_points.WithMovedPoint(dragging_point->point, dragging_point->current);
            if (moved) route_points = std::move(*moved);
        }
        const LinkRouterDescriptor* router = routers.Find(router_type);
        if (router == nullptr) router = routers.Find(BezierLinkRouterType());
        if (router == nullptr) {
            suppressed_links.insert(link_id);
            continue;
        }
        const LinkRoutingContext routing_context{
            .graph = graph_id,
            .link = semantic->second,
            .output = {semantic->second.output, output_pin->second.node,
                output->position, output->outward_normal},
            .input = {semantic->second.input, input_pin->second.node,
                input->position, input->outward_normal},
            .route_points = route_points,
            .obstacles = cache.obstacles,
        };
        auto routed = InvokeRouter(*router, routing_context, route_points.size());
        ++session.metrics.routed_links;
        if (router_callback_invalidated) {
            for (const auto& [index, bounds] : restored_obstacles) cache.obstacles[index].bounds = bounds;
            dynamic_callback_invalidated = true;
            session.last_error = routed.error().message;
            return false;
        }
        if (!routed) {
            session.last_error = routed.error().message;
            suppressed_links.insert(link_id);
            continue;
        }
        if (!visible_links.contains(link_id) && Detail::Overlaps(
                Detail::PathBounds(*routed),
                Detail::Expand(viewport, (style.link_width + 6.0f) / session.zoom))) {
            visible_links.insert(link_id);
        }
        overridden_link_paths.emplace(link_id, std::move(*routed));
    }
    for (const auto& [index, bounds] : restored_obstacles) cache.obstacles[index].bounds = bounds;
    session.metrics.visible_nodes += node_geometry.size();
    for (const LinkId link : visible_links) {
        if (const auto found = cache.links.find(link); found != cache.links.end()) {
            session.metrics.visible_link_segments += found->second.path.segments.size();
        }
    }
    return !dynamic_callback_invalidated;
}

bool EditorFrame::ApplyViewportNavigation() {
    const bool pointer_over_minimap_region = config.show_minimap &&
        ScaleUi(config.minimap_size.x + 8.0f) < canvas_size.x &&
        ScaleUi(config.minimap_size.y + 8.0f) < canvas_size.y &&
        mouse.x >= canvas_max.x - ScaleUi(config.minimap_size.x + 8.0f) &&
        mouse.y >= canvas_max.y - ScaleUi(config.minimap_size.y + 8.0f);
    if (canvas_hovered && !PointerOverUiBody() && !pointer_over_minimap_region &&
        ImGui::GetIO().MouseWheel != 0.0f && std::holds_alternative<Idle>(session.interaction)) {
        const Vec2 graph_under_mouse = ToGraph(mouse);
        const float factor = std::pow(config.zoom_step, ImGui::GetIO().MouseWheel);
        session.zoom = std::clamp(session.zoom * factor, config.min_zoom, config.max_zoom);
        session.pan = {
            (mouse.x - canvas_origin.x) / ui_scale - graph_under_mouse.x * session.zoom,
            (mouse.y - canvas_origin.y) / ui_scale - graph_under_mouse.y * session.zoom,
        };
        session.pan = ClampPan(session.pan);
        if (!BuildTransientGeometry()) return false;
    }

    if (!session.frame_all && !session.frame_selection) return true;
    auto& cache = session.geometry;
    ImVec2 bounds_min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    ImVec2 bounds_max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    bool has_bounds = false;
    std::unordered_set<NodeId, IdHash> framed_nodes = session.selected_nodes;
    if (session.frame_selection) {
        for (const auto link_id : session.selected_links) {
            const auto link = graph->links.find(link_id);
            if (link == graph->links.end()) continue;
            if (const auto pin = graph->pins.find(link->second.output); pin != graph->pins.end()) {
                framed_nodes.insert(pin->second.node);
            }
            if (const auto pin = graph->pins.find(link->second.input); pin != graph->pins.end()) {
                framed_nodes.insert(pin->second.node);
            }
        }
    }
    const auto include_point = [&](const Vec2 point) {
        bounds_min = Min(bounds_min, ImVec2{point.x, point.y});
        bounds_max = Max(bounds_max, ImVec2{point.x, point.y});
        has_bounds = true;
    };
    for (const auto& [node_id, geometry] : cache.nodes) {
        if (session.frame_selection && !framed_nodes.contains(node_id)) continue;
        include_point(geometry.spatial_bounds.min);
        include_point(geometry.spatial_bounds.max);
    }
    for (const auto& [link_id, link_presentation] : presentation.Links()) {
        const auto link = graph->links.find(link_id);
        if (link == graph->links.end() || !cache.links.contains(link_id)) continue;
        const bool has_selected_point = std::ranges::any_of(
            link_presentation.Route(),
            [&](const RoutePoint& point) { return session.selected_route_points.contains(point.id); });
        if (session.frame_selection && !session.selected_links.contains(link_id) && !has_selected_point) continue;
        for (const auto& point : link_presentation.Route()) {
            if ((!session.frame_selection || session.selected_links.contains(link_id) ||
                    session.selected_route_points.contains(point.id)) && Bounded(point.position)) {
                include_point(point.position);
            }
        }
        if (!session.frame_selection || session.selected_links.contains(link_id)) {
            include_point(cache.links.at(link_id).bounds.min);
            include_point(cache.links.at(link_id).bounds.max);
        }
    }
    for (const auto& [group_id, group_state] : presentation.Groups()) {
        if (session.frame_selection && !session.selected_groups.contains(group_id)) continue;
        const auto& geometry = group_state.geometry;
        if (group_state.graph == graph_id && Bounded(geometry.position) && Bounded(geometry.size) &&
            geometry.size.x >= 0.0f && geometry.size.y >= 0.0f) {
            include_point(geometry.position);
            include_point(geometry.position +
                (geometry.collapsed ? Vec2{geometry.size.x, 30.0f} : geometry.size));
        }
    }
    if (has_bounds) {
        constexpr float Padding = 80.0f;
        const float graph_width = std::max(bounds_max.x - bounds_min.x, 1.0f);
        const float graph_height = std::max(bounds_max.y - bounds_min.y, 1.0f);
        session.zoom = std::clamp(
            std::min((canvas_size.x / ui_scale - Padding) / graph_width,
                     (canvas_size.y / ui_scale - Padding) / graph_height),
            config.min_zoom,
            config.max_zoom);
        const Vec2 center{
            (bounds_min.x + bounds_max.x) * 0.5f,
            (bounds_min.y + bounds_max.y) * 0.5f,
        };
        session.pan = {
            canvas_size.x * 0.5f / ui_scale - center.x * session.zoom,
            canvas_size.y * 0.5f / ui_scale - center.y * session.zoom,
        };
        session.pan = ClampPan(session.pan);
        if (!BuildTransientGeometry()) return false;
    }
    session.frame_all = false;
    session.frame_selection = false;
    return true;
}

} // namespace Uni::GUI::Nodes::EditorDetail
