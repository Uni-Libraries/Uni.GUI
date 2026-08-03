#include "internal/frame.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

void EditorFrame::UpdateInteraction() {
    if (session.minimap_navigation) {
        session.pan = ClampPan({
            canvas_size.x * 0.5f / ui_scale - session.minimap_navigation->x * session.zoom,
            canvas_size.y * 0.5f / ui_scale - session.minimap_navigation->y * session.zoom,
        });
        session.minimap_navigation.reset();
    }
    if (auto* dragging = std::get_if<DraggingNodes>(&session.interaction)) {
        dragging->delta = {
            (mouse.x - dragging->start.x) / GraphScale(),
            (mouse.y - dragging->start.y) / GraphScale(),
        };
        if (config.snap_to_grid && !ImGui::GetIO().KeyAlt && !dragging->before.empty()) {
            const auto anchor = std::ranges::min_element(
                dragging->before,
                [](const auto& first, const auto& second) { return first.first.Value() < second.first.Value(); });
            dragging->delta = Snap(anchor->second + dragging->delta, config.snap_size) - anchor->second;
        }
    } else if (auto* marquee = std::get_if<MarqueeSelecting>(&session.interaction)) {
        marquee->current = mouse;
    } else if (auto* creating = std::get_if<CreatingLink>(&session.interaction)) {
        creating->current = mouse;
        creating->dragging = creating->dragging ||
            std::sqrt(DistanceSquared(mouse, creating->start)) >= ImGui::GetIO().MouseDragThreshold;
    } else if (auto* route = std::get_if<DraggingRoutePoint>(&session.interaction)) {
        route->current = ToGraph(mouse);
        if (config.snap_to_grid && !ImGui::GetIO().KeyAlt) {
            route->current = Snap(route->current, config.snap_size);
        }
    } else if (auto* group = std::get_if<DraggingGroup>(&session.interaction)) {
        group->delta = {
            (mouse.x - group->start.x) / GraphScale(),
            (mouse.y - group->start.y) / GraphScale(),
        };
        if (config.snap_to_grid && !ImGui::GetIO().KeyAlt) {
            group->delta = Snap(group->before + group->delta, config.snap_size) - group->before;
        }
    } else if (auto* resize = std::get_if<ResizingNode>(&session.interaction)) {
        const Vec2 minimum_size = MinimumNodeSize();
        resize->current = {
            std::max(minimum_size.x,
                resize->before.x + (mouse.x - resize->start.x) / GraphScale()),
            std::max(minimum_size.y,
                resize->before.y + (mouse.y - resize->start.y) / GraphScale()),
        };
        if (config.snap_to_grid && !ImGui::GetIO().KeyAlt) {
            resize->current = Snap(resize->current, config.snap_size);
            resize->current.x = std::max(resize->current.x, minimum_size.x);
            resize->current.y = std::max(resize->current.y, minimum_size.y);
        }
    } else if (auto* resize = std::get_if<ResizingGroup>(&session.interaction)) {
        resize->current = {
            std::max(config.minimum_group_size.x,
                resize->before.x + (mouse.x - resize->start.x) / GraphScale()),
            std::max(config.minimum_group_size.y,
                resize->before.y + (mouse.y - resize->start.y) / GraphScale()),
        };
        if (config.snap_to_grid && !ImGui::GetIO().KeyAlt) {
            resize->current = Snap(resize->current, config.snap_size);
        }
    } else if (std::holds_alternative<Panning>(session.interaction)) {
        session.pan.x += ImGui::GetIO().MouseDelta.x / ui_scale;
        session.pan.y += ImGui::GetIO().MouseDelta.y / ui_scale;
    }
    session.pan = ClampPan(session.pan);
}

void EditorFrame::ProcessGestures() {
    auto& cache = session.geometry;
    const auto& resolved_nodes = cache.resolved_nodes;
    const bool body_hovered = hovered_node && hovered_bodies.contains(hovered_node);
    bool actionable_header_hovered = false;
    if (hovered_header_item) {
        const auto& geometry = header_item_geometry[*hovered_header_item];
        const auto header = node_headers.find(geometry.node);
        if (header != node_headers.end() && geometry.item_index < header->second.items.size()) {
            const auto& item = header->second.items[geometry.item_index];
            actionable_header_hovered = item.enabled && !item.action.empty();
        }
    }
    append_selection = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
    if (std::holds_alternative<Idle>(session.interaction) && canvas_hovered && !minimap_hovered &&
        !PointerOverUiBody() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        session.interaction = Panning{};
    }
    if (std::holds_alternative<Panning>(session.interaction) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        session.interaction = Idle{};
    }

    const bool node_activated = std::holds_alternative<Idle>(session.interaction) && canvas_hovered &&
        !minimap_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered_node &&
        !body_hovered && !actionable_header_hovered && !hovered_node_collapse && !hovered_node_resize;
    if (node_activated) {
        result.activated_node = hovered_node;
        const auto* activated = document.FindNode(graph_id, hovered_node);
        if (activated != nullptr && activated->subgraph) enter_subgraph = hovered_node;
    }

    bool inserted_route_point = false;
    if (std::holds_alternative<Idle>(session.interaction) && canvas_hovered && !minimap_hovered &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hovered_link) {
        const RoutePointId point = presentation.AllocateRoutePointId();
        if (!point) {
            session.last_error = "Route point IDs are exhausted";
        } else {
            auto inserted = commands.Execute(
                std::make_unique<InsertRoutePointCommand>(
                    hovered_link,
                    RoutePoint{.id = point, .position = ToGraph(mouse)},
                    hovered_link_segment),
                document,
                presentation,
                registry,
                policy);
            if (inserted) {
                RecordChange(*inserted);
                result.selection_changed |= ClearSelection();
                session.selected_route_points.emplace(point, hovered_link);
                result.selection_changed = true;
                inserted_route_point = true;
                session.last_error.clear();
            } else {
                session.last_error = inserted.error().message;
            }
        }
    }

    if (std::holds_alternative<Idle>(session.interaction) && canvas_hovered && !minimap_hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inserted_route_point && !node_activated) {
        if (actionable_header_hovered) {
            const auto& geometry = header_item_geometry[*hovered_header_item];
            const auto header = node_headers.find(geometry.node);
            if (header != node_headers.end() && geometry.item_index < header->second.items.size()) {
                const auto& item = header->second.items[geometry.item_index];
                if (item.enabled && !item.action.empty()) {
                    result.header_actions.push_back({graph_id, geometry.node, item.id, item.action});
                }
            }
        } else if (body_hovered) {
            if (!append_selection) {
                result.selection_changed |= SelectOnly(session.selected_nodes, hovered_node, false);
                result.selection_changed |= !session.selected_links.empty() || !session.selected_groups.empty() ||
                    !session.selected_route_points.empty();
                session.selected_links.clear();
                session.selected_groups.clear();
                session.selected_route_points.clear();
            } else {
                result.selection_changed |= session.selected_nodes.insert(hovered_node).second;
            }
        } else if (hovered_pin) {
            LinkId reconnect;
            PinId fixed = hovered_pin;
            const auto* pin = document.FindPin(graph_id, hovered_pin);
            if (pin != nullptr && pin->cardinality == PinCardinality::Single) {
                for (const auto& [link_id, link] : graph->links) {
                    if (link.output == hovered_pin || link.input == hovered_pin) {
                        reconnect = link_id;
                        fixed = link.output == hovered_pin ? link.input : link.output;
                        break;
                    }
                }
            }
            session.interaction = CreatingLink{fixed, mouse, mouse, reconnect, hovered_pin, false};
        } else if (hovered_node && hovered_node_collapse) {
            auto collapsed = commands.Execute(
                std::make_unique<SetNodeCollapsedCommand>(
                    hovered_node, !resolved_nodes.at(hovered_node).collapsed),
                document, presentation, registry, policy);
            if (collapsed) {
                RecordChange(*collapsed);
                session.last_error.clear();
            } else {
                session.last_error = collapsed.error().message;
            }
        } else if (hovered_node && hovered_node_resize) {
            const auto geometry = std::ranges::find(node_geometry, hovered_node, &NodeGeometry::id);
            const Vec2 size{
                (geometry->max.x - geometry->min.x) / GraphScale(),
                (geometry->max.y - geometry->min.y) / GraphScale(),
            };
            session.interaction = ResizingNode{hovered_node, mouse, size, size};
        } else if (hovered_node) {
            const bool was_selected = session.selected_nodes.contains(hovered_node);
            if (!was_selected) {
                if (!append_selection) {
                    result.selection_changed |= !session.selected_nodes.empty() || !session.selected_links.empty();
                    result.selection_changed |= ClearSelection();
                }
                result.selection_changed |= session.selected_nodes.insert(hovered_node).second;
            }
            std::uint64_t highest_z = 0;
            for (const auto node_id : cache.ordered_nodes) {
                highest_z = std::max(highest_z, resolved_nodes.at(node_id).z_order);
            }
            SetNodeZOrderCommand::Orders orders;
            if (highest_z == std::numeric_limits<std::uint64_t>::max()) {
                highest_z = 0;
                for (const auto node_id : cache.ordered_nodes) orders.insert_or_assign(node_id, ++highest_z);
            }
            orders.insert_or_assign(hovered_node, highest_z + 1);
            std::vector<std::unique_ptr<Command>> raise_commands;
            for (const auto& [node, z_order] : orders) {
                (void)z_order;
                if (presentation.FindNode(node) == nullptr) {
                    raise_commands.push_back(std::make_unique<SetNodePresentationCommand>(
                        node, resolved_nodes.at(node)));
                }
            }
            raise_commands.push_back(std::make_unique<SetNodeZOrderCommand>(std::move(orders)));
            std::unique_ptr<Command> raise_command = raise_commands.size() == 1
                ? std::move(raise_commands.front())
                : std::unique_ptr<Command>{
                    std::make_unique<CompoundCommand>("Raise node", std::move(raise_commands))};
            auto raised = commands.Execute(
                std::move(raise_command), document, presentation, registry, policy);
            if (raised) {
                RecordChange(*raised);
                session.last_error.clear();
            } else {
                session.last_error = raised.error().message;
            }

            DraggingNodes dragging;
            dragging.start = mouse;
            for (const auto selected : session.selected_nodes) {
                if (graph->nodes.contains(selected)) {
                    dragging.before.emplace(selected, resolved_nodes.at(selected).position);
                }
            }
            session.interaction = std::move(dragging);
        } else if (hovered_route_point) {
            if (!append_selection) result.selection_changed |= ClearSelection();
            result.selection_changed |= session.selected_route_points
                .insert_or_assign(hovered_route_point, hovered_route_link).second;
            const auto* point = presentation.FindRoutePoint(hovered_route_link, hovered_route_point);
            if (point != nullptr) {
                session.interaction = DraggingRoutePoint{
                    hovered_route_link,
                    hovered_route_point,
                    mouse,
                    point->position,
                    point->position,
                };
            }
        } else if (hovered_link) {
            if (!append_selection) result.selection_changed |= ClearSelection();
            result.selection_changed |= SelectOnly(session.selected_links, hovered_link, append_selection);
        } else if (hovered_group && hovered_group_collapse) {
            const auto* state = presentation.FindGroup(hovered_group);
            auto collapsed = commands.Execute(
                std::make_unique<SetGroupCollapsedCommand>(hovered_group, !state->geometry.collapsed),
                document, presentation, registry, policy);
            if (collapsed) {
                RecordChange(*collapsed);
                result.selection_changed |= PruneSelection();
                session.last_error.clear();
            } else {
                session.last_error = collapsed.error().message;
            }
        } else if (hovered_group && hovered_group_resize) {
            const auto* state = presentation.FindGroup(hovered_group);
            session.interaction = ResizingGroup{
                hovered_group, mouse, state->geometry.size, state->geometry.size};
        } else if (hovered_group) {
            if (!append_selection) result.selection_changed |= ClearSelection();
            result.selection_changed |= session.selected_groups.insert(hovered_group).second;
            const auto* state = presentation.FindGroup(hovered_group);
            DraggingGroup dragging{
                .group = hovered_group,
                .start = mouse,
                .before = state->geometry.position,
            };
            for (const NodeId member : state->members) {
                if (const auto resolved = resolved_nodes.find(member); resolved != resolved_nodes.end()) {
                    dragging.member_positions.emplace(member, resolved->second.position);
                }
            }
            session.interaction = std::move(dragging);
        } else {
            if (!append_selection) result.selection_changed |= ClearSelection();
            session.interaction = MarqueeSelecting{mouse, mouse, append_selection};
        }
    }

    if (auto* dragging = std::get_if<DraggingNodes>(&session.interaction);
        dragging != nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float screen_distance = std::sqrt(DistanceSquared(mouse, dragging->start));
        if (screen_distance >= ImGui::GetIO().MouseDragThreshold) {
            MoveNodesCommand::Positions after;
            for (const auto& [node, position] : dragging->before) after.emplace(node, position + dragging->delta);
            std::vector<std::unique_ptr<Command>> move_commands;
            for (const auto& [node, position] : dragging->before) {
                (void)position;
                if (presentation.FindNode(node) == nullptr) {
                    move_commands.push_back(std::make_unique<SetNodePresentationCommand>(
                        node, resolved_nodes.at(node)));
                }
            }
            move_commands.push_back(std::make_unique<MoveNodesCommand>(
                graph_id, dragging->before, std::move(after)));
            for (const GroupId group_id : cache.ordered_groups) {
                const auto& group = presentation.Groups().at(group_id);
                std::vector<NodeId> added;
                std::vector<NodeId> removed;
                for (const auto& [node, position] : dragging->before) {
                    (void)position;
                    const bool member = group.members.contains(node);
                    if (group_id == membership_drop_group) {
                        if (!member) added.push_back(node);
                    } else if (member) {
                        removed.push_back(node);
                    }
                }
                if (!added.empty() || !removed.empty()) {
                    move_commands.push_back(std::make_unique<ChangeGroupMembersCommand>(
                        group_id, std::move(added), std::move(removed)));
                }
            }
            std::unique_ptr<Command> move_command = move_commands.size() == 1
                ? std::move(move_commands.front())
                : std::unique_ptr<Command>{
                    std::make_unique<CompoundCommand>("Move nodes", std::move(move_commands))};
            auto moved = commands.Execute(
                std::move(move_command), document, presentation, registry, policy);
            if (moved) {
                RecordChange(*moved);
                session.last_error.clear();
            } else {
                session.last_error = moved.error().message;
            }
        }
        session.interaction = Idle{};
    }

    if (auto* dragging = std::get_if<DraggingRoutePoint>(&session.interaction);
        dragging != nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        auto moved = commands.Execute(
            std::make_unique<MoveRoutePointCommand>(dragging->link, dragging->point, dragging->current),
            document, presentation, registry, policy);
        if (moved) {
            RecordChange(*moved);
            session.last_error.clear();
        } else {
            session.last_error = moved.error().message;
        }
        session.interaction = Idle{};
    }

    if (auto* dragging = std::get_if<DraggingGroup>(&session.interaction);
        dragging != nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        std::vector<std::unique_ptr<Command>> group_commands;
        group_commands.push_back(std::make_unique<MoveGroupCommand>(
            dragging->group, dragging->before + dragging->delta));
        if (!dragging->member_positions.empty()) {
            for (const auto& [node, position] : dragging->member_positions) {
                (void)position;
                if (presentation.FindNode(node) == nullptr) {
                    group_commands.push_back(std::make_unique<SetNodePresentationCommand>(
                        node, resolved_nodes.at(node)));
                }
            }
            MoveNodesCommand::Positions after;
            for (const auto& [node, position] : dragging->member_positions) {
                after.emplace(node, position + dragging->delta);
            }
            group_commands.push_back(std::make_unique<MoveNodesCommand>(
                graph_id, dragging->member_positions, std::move(after)));
        }
        auto moved = commands.Execute(
            group_commands.size() == 1
                ? std::move(group_commands.front())
                : std::unique_ptr<Command>{
                    std::make_unique<CompoundCommand>("Move group", std::move(group_commands))},
            document, presentation, registry, policy);
        if (moved) {
            RecordChange(*moved);
            session.last_error.clear();
        } else {
            session.last_error = moved.error().message;
        }
        session.interaction = Idle{};
    }

    if (auto* resizing = std::get_if<ResizingNode>(&session.interaction);
        resizing != nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const Vec2 minimum_size = MinimumNodeSize();
        resizing->current.x = std::max(resizing->current.x, minimum_size.x);
        resizing->current.y = std::max(resizing->current.y, minimum_size.y);
        auto resized = commands.Execute(
            std::make_unique<ResizeNodeCommand>(resizing->node, resizing->current),
            document, presentation, registry, policy);
        if (resized) {
            RecordChange(*resized);
            session.last_error.clear();
        } else {
            session.last_error = resized.error().message;
        }
        session.interaction = Idle{};
    }

    if (auto* resizing = std::get_if<ResizingGroup>(&session.interaction);
        resizing != nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        resizing->current.x = std::max(resizing->current.x, config.minimum_group_size.x);
        resizing->current.y = std::max(resizing->current.y, config.minimum_group_size.y);
        auto resized = commands.Execute(
            std::make_unique<ResizeGroupCommand>(resizing->group, resizing->current),
            document, presentation, registry, policy);
        if (resized) {
            RecordChange(*resized);
            session.last_error.clear();
        } else {
            session.last_error = resized.error().message;
        }
        session.interaction = Idle{};
    }

    if (auto* marquee = std::get_if<MarqueeSelecting>(&session.interaction);
        marquee != nullptr && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 min = Min(marquee->start, marquee->current);
        const ImVec2 max = Max(marquee->start, marquee->current);
        const auto previous_nodes = session.selected_nodes;
        const auto previous_links = session.selected_links;
        const auto previous_groups = session.selected_groups;
        const auto previous_points = session.selected_route_points;
        if (!marquee->append) (void)ClearSelection();
        for (const auto& geometry : node_geometry) {
            if (Overlaps(min, max, geometry.min, geometry.max)) session.selected_nodes.insert(geometry.id);
        }
        for (const auto& geometry : group_geometry) {
            if (Overlaps(min, max, geometry.min, geometry.max)) session.selected_groups.insert(geometry.id);
        }
        result.selection_changed |= previous_nodes != session.selected_nodes ||
            previous_links != session.selected_links || previous_groups != session.selected_groups ||
            previous_points != session.selected_route_points;
        session.interaction = Idle{};
    }

    if (auto* creating = std::get_if<CreatingLink>(&session.interaction)) {
        if (creating->dragging && hovered_pin && hovered_pin != creating->origin) {
            link_preview = ValidateConnection(
                document,
                presentation,
                ConnectionRequest{graph_id, creating->origin, hovered_pin, creating->reconnect},
                registry,
                policy);
            has_link_preview = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (creating->dragging && creating->reconnect && hovered_pin == creating->detached) {
                session.last_error.clear();
            } else if (creating->dragging && hovered_pin && hovered_pin != creating->origin) {
                const ConnectionRequest request{
                    graph_id, creating->origin, hovered_pin, creating->reconnect,
                };
                const auto compatibility = ValidateConnection(document, presentation, request, registry, policy);
                if (compatibility.status != ConnectionResult::Status::Rejected) {
                    const Vec2 first_position = ToGraph(pin_positions.at(creating->origin));
                    const Vec2 second_position = ToGraph(pin_positions.at(hovered_pin));
                    auto command = PrepareConnectionCommand(
                        document,
                        presentation,
                        registry,
                        request,
                        (first_position + second_position) * 0.5f,
                        policy);
                    if (!command) {
                        session.last_error = command.error().message;
                    } else {
                        auto connected = commands.Execute(
                            std::move(*command), document, presentation, registry, policy);
                        if (connected) {
                            RecordChange(*connected);
                            RefreshGraph();
                            session.last_error.clear();
                        } else {
                            session.last_error = connected.error().message;
                        }
                    }
                } else {
                    session.last_error = compatibility.reason;
                }
            } else if (creating->dragging && !hovered_pin && config.enable_node_popup &&
                canvas_hovered && !minimap_hovered) {
                session.popup_position = ToGraph(mouse);
                session.popup_origin = creating->origin;
                session.popup_reconnect = creating->reconnect;
                session.popup_search.fill('\0');
                session.node_popup_graph = graph_id;
                ImGui::OpenPopup("Create node");
            }
            session.interaction = Idle{};
        }
    }

    if (!std::holds_alternative<Idle>(session.interaction) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        session.interaction = Idle{};
        session.minimap_navigation.reset();
    }
}

} // namespace Uni::GUI::Nodes::EditorDetail
