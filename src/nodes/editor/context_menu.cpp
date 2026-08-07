#include "internal.h"
#include "internal/frame.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>

namespace Uni::GUI::Nodes {

EditorMenuContext Detail::EditorAccess::MakeMenuContext(
    const GraphId graph,
    const GraphDocument& document,
    const GraphPresentation& presentation,
    GraphSelection selection,
    ContextMenuTarget target,
    std::function<void(std::unique_ptr<Command>)> sink) {
    return EditorMenuContext{
        graph,
        document,
        presentation,
        std::move(selection),
        std::move(target),
        std::move(sink),
    };
}

EditorMenuContext::EditorMenuContext(
    const GraphId graph,
    const GraphDocument& document,
    const GraphPresentation& presentation,
    GraphSelection selection,
    ContextMenuTarget target,
    std::function<void(std::unique_ptr<Command>)> sink)
    : m_graph(graph),
      m_document(&document),
      m_presentation(&presentation),
      m_selection(std::move(selection)),
      m_target(std::move(target)),
      m_sink(std::move(sink)) {}

GraphId EditorMenuContext::Graph() const noexcept { return m_graph; }
const GraphDocument& EditorMenuContext::Document() const noexcept { return *m_document; }
const GraphPresentation& EditorMenuContext::Presentation() const noexcept { return *m_presentation; }
const GraphSelection& EditorMenuContext::Selection() const noexcept { return m_selection; }
const ContextMenuTarget& EditorMenuContext::Target() const noexcept { return m_target; }

void EditorMenuContext::Submit(std::unique_ptr<Command> command) {
    if (command) {
        m_sink(std::move(command));
    }
}

bool EditorDetail::InvokeContextMenuCallback(
    DrawEditorContextMenuFn callback,
    const GraphId graph,
    GraphDocument& document,
    GraphPresentation& presentation,
    GraphSelection selection,
    ContextMenuTarget target,
    std::vector<std::unique_ptr<Command>>& pending_commands,
    std::string& error) {
    const std::size_t pending_before_callback = pending_commands.size();
    const Revisions callback_revisions{
        document.ModelRevision(),
        presentation.PresentationRevision(),
    };
    const std::uint64_t document_identity = document.Identity();
    const std::uint64_t presentation_identity = presentation.Identity();
    const std::uint64_t document_allocation = document.AllocationEpoch();
    const std::uint64_t presentation_allocation = presentation.AllocationEpoch();
    auto menu_context = Detail::EditorAccess::MakeMenuContext(
        graph,
        document,
        presentation,
        std::move(selection),
        std::move(target),
        [&](std::unique_ptr<Command> command) {
            pending_commands.push_back(std::move(command));
        });
    bool callback_failed = false;
    try {
        callback(menu_context);
    } catch (const std::exception& exception) {
        callback_failed = true;
        error = std::string{"Context menu callback failed: "} + exception.what();
    } catch (...) {
        callback_failed = true;
        error = "Context menu callback failed with an unknown exception";
    }
    if (callback_failed) pending_commands.resize(pending_before_callback);
    if (callback_revisions != Revisions{
            document.ModelRevision(),
            presentation.PresentationRevision()} ||
        document_identity != document.Identity() ||
        presentation_identity != presentation.Identity() ||
        document_allocation != document.AllocationEpoch() ||
        presentation_allocation != presentation.AllocationEpoch()) {
        pending_commands.resize(pending_before_callback);
        error = "Context menu callbacks must queue changes through EditorMenuContext";
        return true;
    }
    return false;
}

void EditorDetail::EditorFrame::OpenContextMenu() {
    if (!std::holds_alternative<Idle>(session.interaction) || !canvas_hovered || minimap_hovered ||
        !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        return;
    }
    ContextMenuTarget target{.position = ToGraph(mouse)};
    if (hovered_pin) {
        target.kind = ContextMenuTargetKind::Pin;
        target.pin = hovered_pin;
    } else if (hovered_node) {
        target.kind = ContextMenuTargetKind::Node;
        target.node = hovered_node;
    } else if (hovered_route_point) {
        target.kind = ContextMenuTargetKind::RoutePoint;
        target.link = hovered_route_link;
        target.route_point = hovered_route_point;
    } else if (hovered_link) {
        target.kind = ContextMenuTargetKind::Link;
        target.link = hovered_link;
        target.route_point_insert_index = hovered_link_segment;
    } else if (hovered_group) {
        target.kind = ContextMenuTargetKind::Group;
        target.group = hovered_group;
    }
    session.context_target = target;
    session.context_graph = graph_id;
    if (target.kind == ContextMenuTargetKind::Group) {
        if (const auto* group = presentation.FindGroup(target.group)) {
            StoreText(session.group_title, group->style->title);
            StoreText(session.group_body, group->style->body);
        }
    }
    if (!append_selection) {
        const bool target_selected = target.kind == ContextMenuTargetKind::Canvas ||
            target.kind == ContextMenuTargetKind::Pin ||
            (target.kind == ContextMenuTargetKind::Node && session.selected_nodes.contains(target.node)) ||
            (target.kind == ContextMenuTargetKind::Link && session.selected_links.contains(target.link)) ||
            (target.kind == ContextMenuTargetKind::Group && session.selected_groups.contains(target.group)) ||
            (target.kind == ContextMenuTargetKind::RoutePoint &&
                session.selected_route_points.contains(target.route_point));
        if (!target_selected) {
            if (target.kind == ContextMenuTargetKind::Group) {
                result.selection_changed |= !session.selected_links.empty() ||
                    !session.selected_groups.empty() || !session.selected_route_points.empty();
                session.selected_links.clear();
                session.selected_groups.clear();
                session.selected_route_points.clear();
            } else {
                result.selection_changed |= ClearSelection();
            }
            if (target.kind == ContextMenuTargetKind::Node) session.selected_nodes.insert(target.node);
            if (target.kind == ContextMenuTargetKind::Link) session.selected_links.insert(target.link);
            if (target.kind == ContextMenuTargetKind::Group) session.selected_groups.insert(target.group);
            if (target.kind == ContextMenuTargetKind::RoutePoint) {
                session.selected_route_points.emplace(target.route_point, target.link);
            }
            result.selection_changed = target.kind != ContextMenuTargetKind::Canvas;
        }
    }
    ImGui::OpenPopup("Node editor context");
}

bool EditorDetail::EditorFrame::DrawContextMenu() {
    const auto assign_group_membership = [&](const std::vector<NodeId>& nodes_to_assign,
                                             const GroupId destination) {
        const std::unordered_set<NodeId, IdHash> assigned(nodes_to_assign.begin(), nodes_to_assign.end());
        for (const GroupId group_id : session.geometry.ordered_groups) {
            const auto& group = presentation.Groups().at(group_id);
            std::vector<NodeId> added;
            std::vector<NodeId> removed;
            if (group_id == destination) {
                for (const NodeId node : assigned) {
                    if (!group.members.contains(node)) added.push_back(node);
                }
            } else {
                for (const NodeId node : assigned) {
                    if (group.members.contains(node)) removed.push_back(node);
                }
            }
            if (!added.empty() || !removed.empty()) {
                pending_commands.push_back(std::make_unique<ChangeGroupMembersCommand>(
                    group_id, std::move(added), std::move(removed)));
            }
        }
    };
    const auto remove_group_membership = [&](const std::vector<NodeId>& nodes_to_remove,
                                             const GroupId source) {
        const auto* group = presentation.FindGroup(source);
        if (group == nullptr) return;
        const std::unordered_set<NodeId, IdHash> removed(nodes_to_remove.begin(), nodes_to_remove.end());
        std::vector<NodeId> removals;
        for (const NodeId node : removed) {
            if (group->members.contains(node)) removals.push_back(node);
        }
        if (!removals.empty()) {
            pending_commands.push_back(std::make_unique<ChangeGroupMembersCommand>(
                source, std::vector<NodeId>{}, std::move(removals)));
        }
    };

    if (!ImGui::BeginPopup("Node editor context")) return true;
    if (session.context_graph != graph_id || !session.context_target) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return true;
    }

    const auto& target = *session.context_target;
    if (target.kind == ContextMenuTargetKind::Canvas) {
        if (config.enable_node_popup && ImGui::MenuItem("Create node...")) {
            session.popup_position = target.position;
            session.popup_origin = {};
            session.popup_reconnect = {};
            session.popup_search.fill('\0');
            session.node_popup_graph = graph_id;
            open_create_popup = true;
        }
        if (ImGui::MenuItem("Add group")) {
            const GroupId group_id = presentation.AllocateGroupId();
            if (group_id) {
                pending_commands.push_back(std::make_unique<AddGroupCommand>(GroupPresentation{
                    .id = group_id,
                    .graph = graph_id,
                    .geometry = ::Uni::GUI::Nodes::GroupGeometry{
                        .position = target.position,
                        .z_order = group_id.Value(),
                    },
                    .style = MakeGroupStyle(GroupStyle{.title = "Group"}),
                    .members = context.Selection().nodes,
                }));
            } else {
                session.last_error = "Group IDs are exhausted";
            }
        }
        if (ImGui::MenuItem("Add comment")) {
            const GroupId group_id = presentation.AllocateGroupId();
            if (group_id) {
                pending_commands.push_back(std::make_unique<AddGroupCommand>(GroupPresentation{
                    .id = group_id,
                    .graph = graph_id,
                    .geometry = ::Uni::GUI::Nodes::GroupGeometry{
                        .position = target.position,
                        .z_order = group_id.Value(),
                    },
                    .style = MakeGroupStyle(GroupStyle{
                        .title = "Comment",
                        .body = "Comment",
                        .kind = GroupKind::Comment,
                    }),
                }));
            } else {
                session.last_error = "Group IDs are exhausted";
            }
        }
    } else if (target.kind == ContextMenuTargetKind::Node) {
        const auto* state = presentation.FindNode(target.node);
        const auto* semantic = document.FindNode(graph_id, target.node);
        if (semantic != nullptr && semantic->subgraph && ImGui::MenuItem("Open subgraph")) {
            enter_subgraph = target.node;
        }
        if (config.enable_node_collapse && state != nullptr && ImGui::MenuItem(state->collapsed ? "Expand" : "Collapse")) {
            pending_commands.push_back(std::make_unique<SetNodeCollapsedCommand>(
                target.node, !state->collapsed));
        }
        if (ImGui::MenuItem("Duplicate")) session.duplicate_requested = true;
        if (ImGui::BeginMenu("Group")) {
            const auto selected_nodes = context.Selection().nodes;
            const bool has_membership = std::ranges::any_of(
                session.geometry.ordered_groups,
                [&](const GroupId group) {
                    const auto& members = presentation.Groups().at(group).members;
                    return members.contains(target.node);
                });
            if (ImGui::MenuItem("No group", nullptr, !has_membership)) {
                assign_group_membership(selected_nodes, {});
            }
            if (!session.geometry.ordered_groups.empty()) ImGui::Separator();
            for (const GroupId group_id : session.geometry.ordered_groups) {
                const auto& group = presentation.Groups().at(group_id);
                const bool member = group.members.contains(target.node);
                const std::string label =
                    (group.style->title.empty() ? std::string{"Group"} : group.style->title) +
                    "##membership_" + std::to_string(group.id.Value());
                if (ImGui::MenuItem(label.c_str(), nullptr, member)) {
                    assign_group_membership(selected_nodes, group.id);
                }
            }
            ImGui::EndMenu();
        }
    } else if (target.kind == ContextMenuTargetKind::Link) {
        if (ImGui::MenuItem("Add route point")) {
            const RoutePointId point = presentation.AllocateRoutePointId();
            if (point) {
                pending_commands.push_back(std::make_unique<InsertRoutePointCommand>(
                    target.link,
                    RoutePoint{point, target.position},
                    target.route_point_insert_index));
            } else {
                session.last_error = "Route point IDs are exhausted";
            }
        }
        if (ImGui::BeginMenu("Routing")) {
            const auto* state = presentation.FindLink(target.link);
            const TypeId current = state != nullptr ? state->Style().router : TypeId{};
            if (ImGui::MenuItem("Editor default", nullptr, current.Empty())) {
                pending_commands.push_back(std::make_unique<SetLinkRouterCommand>(target.link, TypeId{}));
            }
            if (ImGui::MenuItem("Bezier", nullptr, current == BezierLinkRouterType())) {
                pending_commands.push_back(std::make_unique<SetLinkRouterCommand>(
                    target.link, BezierLinkRouterType()));
            }
            if (ImGui::MenuItem("Straight", nullptr, current == StraightLinkRouterType())) {
                pending_commands.push_back(std::make_unique<SetLinkRouterCommand>(
                    target.link, StraightLinkRouterType()));
            }
            if (ImGui::MenuItem("Orthogonal", nullptr, current == OrthogonalLinkRouterType())) {
                pending_commands.push_back(std::make_unique<SetLinkRouterCommand>(
                    target.link, OrthogonalLinkRouterType()));
            }
            ImGui::EndMenu();
        }
    } else if (target.kind == ContextMenuTargetKind::Group) {
        const auto* state = presentation.FindGroup(target.group);
        ImGui::SetNextItemWidth(ScaleUi(240.0f));
        (void)ImGui::InputText(
            "Title", session.group_title.data(), session.group_title.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        const bool title_deactivated = ImGui::IsItemDeactivatedAfterEdit();
        bool body_deactivated = false;
        if (state != nullptr && state->style->kind == GroupKind::Comment) {
            ImGui::SetNextItemWidth(ScaleUi(240.0f));
            (void)ImGui::InputTextMultiline(
                "Body",
                session.group_body.data(),
                session.group_body.size(),
                {ScaleUi(240.0f), ScaleUi(90.0f)},
                ImGuiInputTextFlags_CtrlEnterForNewLine);
            body_deactivated = ImGui::IsItemDeactivatedAfterEdit();
        }
        if (state != nullptr && (title_deactivated || body_deactivated)) {
            pending_commands.push_back(std::make_unique<SetGroupStyleCommand>(
                target.group,
                GroupStyle{
                    .title = session.group_title.data(),
                    .body = session.group_body.data(),
                    .color = state->style->color,
                    .kind = state->style->kind,
                }));
        }
        if (state != nullptr && ImGui::MenuItem(
                state->geometry.collapsed ? "Expand" : "Collapse")) {
            pending_commands.push_back(std::make_unique<SetGroupCollapsedCommand>(
                target.group, !state->geometry.collapsed));
        }
        const auto selected_nodes = context.Selection().nodes;
        if (ImGui::MenuItem("Add selected nodes", nullptr, false, !selected_nodes.empty())) {
            assign_group_membership(selected_nodes, target.group);
        }
        if (ImGui::MenuItem("Remove selected nodes", nullptr, false, !selected_nodes.empty())) {
            remove_group_membership(selected_nodes, target.group);
        }
        if (state != nullptr && ImGui::MenuItem("Clear members", nullptr, false, !state->members.empty())) {
            const std::vector<NodeId> members(state->members.begin(), state->members.end());
            remove_group_membership(members, target.group);
        }
    } else if (target.kind == ContextMenuTargetKind::RoutePoint) {
        if (ImGui::MenuItem("Remove route point")) {
            pending_commands.push_back(std::make_unique<RemoveRoutePointsCommand>(
                std::vector<RoutePointRef>{{target.link, target.route_point}}));
        }
    }

    if (target.kind != ContextMenuTargetKind::Canvas && target.kind != ContextMenuTargetKind::RoutePoint) {
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            if (target.kind == ContextMenuTargetKind::Node) {
                pending_commands.push_back(std::make_unique<DeleteElementsCommand>(
                    graph_id, std::vector<NodeId>{target.node}));
            } else if (target.kind == ContextMenuTargetKind::Link) {
                pending_commands.push_back(std::make_unique<DeleteElementsCommand>(
                    graph_id, std::vector<NodeId>{}, std::vector<LinkId>{target.link}));
            } else if (target.kind == ContextMenuTargetKind::Group) {
                pending_commands.push_back(std::make_unique<RemoveGroupCommand>(target.group));
            } else if (target.kind == ContextMenuTargetKind::Pin) {
                const auto* pin = document.FindPin(graph_id, target.pin);
                if (pin != nullptr && pin->storage == PinStorage::Dynamic) {
                    pending_commands.push_back(std::make_unique<RemoveDynamicPinCommand>(graph_id, target.pin));
                } else {
                    session.last_error = "Descriptor-owned static pins cannot be deleted";
                }
            }
        }
    }

    if (context.Selection().nodes.size() >= 2 && ImGui::BeginMenu("Align")) {
        if (ImGui::MenuItem("Left")) session.alignment_requested = NodeAlignment::Left;
        if (ImGui::MenuItem("Horizontal centers")) {
            session.alignment_requested = NodeAlignment::HorizontalCenter;
        }
        if (ImGui::MenuItem("Right")) session.alignment_requested = NodeAlignment::Right;
        if (ImGui::MenuItem("Top")) session.alignment_requested = NodeAlignment::Top;
        if (ImGui::MenuItem("Vertical centers")) {
            session.alignment_requested = NodeAlignment::VerticalCenter;
        }
        if (ImGui::MenuItem("Bottom")) session.alignment_requested = NodeAlignment::Bottom;
        ImGui::Separator();
        if (ImGui::MenuItem("Distribute horizontally")) {
            session.alignment_requested = NodeAlignment::DistributeHorizontal;
        }
        if (ImGui::MenuItem("Distribute vertically")) {
            session.alignment_requested = NodeAlignment::DistributeVertical;
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(
            context.Selection().nodes.empty() ? "Auto-layout graph" : "Auto-layout selection")) {
        session.layout_requested = LayoutOptions{};
    }
    if (callbacks.draw_context_menu) {
        ImGui::Separator();
        context_state_invalidated |= InvokeContextMenuCallback(
            callbacks.draw_context_menu,
            graph_id,
            document,
            presentation,
            context.Selection(),
            target,
            pending_commands,
            session.last_error);
    }
    ImGui::EndPopup();
    if (context_state_invalidated) {
        result.selection = context.Selection();
        return false;
    }
    return true;
}

bool EditorDetail::EditorFrame::DrawCreatePalette(const bool open_popup) {
    if (open_popup) ImGui::OpenPopup("Create node");
    if (!ImGui::BeginPopup("Create node")) return true;
    if (graph == nullptr || session.node_popup_graph != graph_id) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        result.selection = context.Selection();
        return false;
    }
    ImGui::SetNextItemWidth(ScaleUi(260.0f));
    ImGui::InputTextWithHint(
        "##search", "Search nodes...", session.popup_search.data(), session.popup_search.size());
    ImGui::Separator();
    const std::string query = Lower(session.popup_search.data());
    const RegistrySnapshot snapshot = registry.Snapshot();
    for (const auto& descriptor : snapshot.Descriptors()) {
        const TypeId& type = descriptor->type;
        if (graph->read_only) continue;
        std::optional<std::size_t> compatible_pin;
        if (session.popup_origin) {
            const auto* origin = document.FindPin(graph_id, session.popup_origin);
            const auto default_pins = snapshot.DefaultPinSchema(type);
            std::optional<std::size_t> convertible_pin;
            for (std::size_t index = 0; origin != nullptr && index < default_pins.size(); ++index) {
                const auto& candidate = default_pins[index];
                if (candidate.direction == origin->direction || candidate.kind != origin->kind) continue;
                const auto compatibility = origin->direction == PinDirection::Output
                    ? snapshot.Check(origin->type, candidate.type, origin->kind)
                    : snapshot.Check(candidate.type, origin->type, origin->kind);
                if (compatibility.status == ConnectionResult::Status::Allowed) {
                    compatible_pin = index;
                    break;
                }
                if (compatibility.status == ConnectionResult::Status::RequiresConversion && !convertible_pin) {
                    convertible_pin = index;
                }
            }
            if (!compatible_pin) compatible_pin = convertible_pin;
            if (!compatible_pin) continue;
        }
        const std::string haystack = Lower(
            descriptor->category + " " + descriptor->display_name + " " + type.Value());
        if (!query.empty() && haystack.find(query) == std::string::npos) continue;
        const std::string label = descriptor->category.empty()
            ? descriptor->display_name
            : descriptor->category + "/" + descriptor->display_name;
        const std::string selectable_label = label + "##" + type.Value();
        if (!ImGui::Selectable(selectable_label.c_str())) continue;

        auto creation = snapshot.Instantiate(document, type);
        if (!creation) {
            session.last_error = creation.error().message;
            continue;
        }
        std::uint64_t highest_z = 0;
        for (const auto node_id : session.geometry.ordered_nodes) {
            highest_z = std::max(highest_z, session.geometry.resolved_nodes.at(node_id).z_order);
        }
        std::vector<std::unique_ptr<Command>> compound;
        if (highest_z == std::numeric_limits<std::uint64_t>::max()) {
            highest_z = 0;
            SetNodeZOrderCommand::Orders orders;
            for (const auto node_id : session.geometry.ordered_nodes) {
                orders.emplace(node_id, ++highest_z);
                if (presentation.FindNode(node_id) == nullptr) {
                    compound.push_back(std::make_unique<SetNodePresentationCommand>(
                        node_id, session.geometry.resolved_nodes.at(node_id)));
                }
            }
            compound.push_back(std::make_unique<SetNodeZOrderCommand>(std::move(orders)));
        }
        const NodeId created_id = creation->node.id;
        PinId created_compatible_pin;
        if (compatible_pin && *compatible_pin < creation->pins.size()) {
            created_compatible_pin = creation->pins[*compatible_pin].id;
        }
        std::unique_ptr<Command> connection_command;
        if (session.popup_origin && created_compatible_pin) {
            const auto* origin = document.FindPin(graph_id, session.popup_origin);
            const ConnectionRequest request{
                graph_id,
                session.popup_origin,
                created_compatible_pin,
                session.popup_reconnect,
            };
            Vec2 conversion_position = session.popup_position;
            if (origin != nullptr) {
                if (const auto* state = presentation.FindNode(origin->node)) {
                    conversion_position = (state->position + session.popup_position) * 0.5f;
                }
            }
            auto prepared_connection = PrepareConnectionCommand(
                document,
                presentation,
                registry,
                request,
                conversion_position,
                policy,
                creation->pins,
                std::span<const NodeInstance>{&creation->node, 1});
            if (!prepared_connection) {
                session.last_error = prepared_connection.error().message;
                continue;
            }
            connection_command = std::move(*prepared_connection);
        }
        compound.push_back(std::make_unique<AddNodeCommand>(
            graph_id,
            std::move(*creation),
            NodePresentation{
                .position = session.popup_position,
                .z_order = highest_z + 1,
            }));
        if (connection_command) compound.push_back(std::move(connection_command));
        std::unique_ptr<Command> command = compound.size() == 1
            ? std::move(compound.front())
            : std::unique_ptr<Command>{std::make_unique<CompoundCommand>(
                session.popup_origin ? "Create connected node" : "Add node",
                std::move(compound))};
        auto added = commands.Execute(
            std::move(command), document, presentation, registry, policy);
        if (added) {
            (void)ClearSelection();
            session.selected_nodes.insert(created_id);
            session.popup_origin = {};
            session.popup_reconnect = {};
            session.last_error.clear();
            RecordChange(*added);
            RefreshGraph();
            result.selection_changed = true;
            ImGui::CloseCurrentPopup();
        } else {
            session.last_error = added.error().message;
        }
    }
    ImGui::EndPopup();
    return true;
}

bool EditorDetail::EditorFrame::DrawMenus() {
    OpenContextMenu();
    open_create_popup = false;
    if (!DrawContextMenu()) return false;
    return DrawCreatePalette(open_create_popup);
}

} // namespace Uni::GUI::Nodes
