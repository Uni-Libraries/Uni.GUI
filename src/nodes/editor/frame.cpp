#include "internal/frame.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

EditorFrame::EditorFrame(
    EditorContext& context,
    GraphDocument& document,
    GraphPresentation& presentation,
    CommandStack& commands,
    const RegistryCatalog& registry,
    const NodeUiRegistry& ui,
    const LinkRouterRegistry& routers,
    const Vec2 requested_size,
    const EditorStyle& style,
    const EditorConfig& config,
    const EditorCallbacks& callbacks,
    const GraphPolicy& policy)
    : context(context),
      document(document),
      presentation(presentation),
      commands(commands),
      registry(registry),
      ui(ui),
      routers(routers),
      requested_size(requested_size),
      style(style),
      config(config),
      callbacks(callbacks),
      policy(policy),
      session(Detail::EditorAccess::Session(context)) {}

EditorFrame::~EditorFrame() {
    CloseCanvas();
}

EditorResult EditorFrame::Draw() {
    if (!Initialize() || !BeginCanvas()) return result;

    UpdateInteraction();
    if (!EnsureGeometryCache()) return result;
    if (!BuildTransientGeometry()) return result;
    if (!ApplyViewportNavigation()) return result;
    RegisterTestItems();
    if (!HitTest()) return result;
    if (!RenderScene()) return result;
    RenderCanvasControls();
    PrepareMinimap();
    ProcessGestures();
    ProcessClipboardRequests();
    ProcessShortcuts();
    if (!RenderOverlays()) return result;
    RenderMinimap();
    EndRendering();
    if (!DrawMenus()) return result;
    FlushPendingCommands();

    CloseCanvas();
    ApplyNavigation();
    NormalizeNavigation();
    result.active_graph = context.ActiveGraph();
    result.selection = context.Selection();
    return result;
}

bool EditorFrame::Initialize() {
    if (!context.ActiveGraph()) {
        if (auto initialized = context.ResetNavigation(document); !initialized) {
            session.last_error = initialized.error().message;
            ImGui::TextUnformatted(session.last_error.c_str());
            return false;
        }
    }
    NormalizeNavigation();
    graph_id = context.ActiveGraph();
    result.active_graph = graph_id;
    result.selection = context.Selection();
    RefreshGraph();
    if (graph == nullptr) {
        session.interaction = Idle{};
        session.minimap_navigation.reset();
        session.last_error = "Graph does not exist";
        ImGui::TextUnformatted(session.last_error.c_str());
        return false;
    }
    if (!ValidConfig(config) || !ValidStyle(style) ||
        !std::isfinite(requested_size.x) || !std::isfinite(requested_size.y) ||
        requested_size.x < 0.0f || requested_size.y < 0.0f ||
        requested_size.x > MaxCanvasDimension || requested_size.y > MaxCanvasDimension) {
        session.last_error = "Node editor configuration contains invalid dimensions or zoom limits";
        ImGui::TextUnformatted(session.last_error.c_str());
        return false;
    }
    UpdateLinkFlows();

    if (session.active_graph != graph_id) {
        result.selection_changed = !session.selected_nodes.empty() || !session.selected_links.empty() ||
            !session.selected_groups.empty() || !session.selected_route_points.empty();
        session.active_graph = graph_id;
        session.interaction = Idle{};
        session.selected_nodes.clear();
        session.selected_links.clear();
        session.selected_groups.clear();
        session.selected_route_points.clear();
        session.context_target.reset();
        session.context_graph = {};
        session.node_popup_graph = {};
        session.popup_origin = {};
        session.popup_reconnect = {};
        session.minimap_navigation.reset();
    }
    session.zoom = std::clamp(session.zoom, config.min_zoom, config.max_zoom);
    result.selection_changed |= PruneSelection();
    result.selection = context.Selection();
    return true;
}

void EditorFrame::UpdateLinkFlows() {
    const float delta_time = std::isfinite(ImGui::GetIO().DeltaTime) && ImGui::GetIO().DeltaTime > 0.0f
        ? ImGui::GetIO().DeltaTime
        : 0.0f;
    for (auto flow = session.link_flows.begin(); flow != session.link_flows.end();) {
        if (flow->document_identity != document.Identity() ||
            document.FindLink(flow->graph, flow->link) == nullptr) {
            flow = session.link_flows.erase(flow);
            continue;
        }
        flow->elapsed += delta_time;
        if (!std::isfinite(flow->elapsed) || flow->elapsed >= config.link_flow_duration) {
            flow = session.link_flows.erase(flow);
            continue;
        }
        result.animation_active = true;
        ++flow;
    }
}

bool EditorFrame::BeginCanvas() {
    ImGui::PushID(&context);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, style.background);
    const ImVec2 child_size{
        requested_size.x > 0.0f ? requested_size.x : 0.0f,
        requested_size.y > 0.0f ? requested_size.y : 0.0f,
    };
    const bool visible = ImGui::BeginChild(
        "##node_editor",
        child_size,
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    child_open = true;
    ImGui::PopStyleColor();
    if (!visible) {
        CloseCanvas();
        return false;
    }

    canvas_origin = ImGui::GetCursorScreenPos();
    canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.x = std::max(canvas_size.x, 1.0f);
    canvas_size.y = std::max(canvas_size.y, 1.0f);
    ImGui::Dummy(canvas_size);
    canvas_max = canvas_origin + canvas_size;
    mouse = ImGui::GetIO().MousePos;
    canvas_hovered = ImGui::IsWindowHovered(
                         ImGuiHoveredFlags_ChildWindows |
                         ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        Contains(canvas_origin, canvas_max, mouse);
    return true;
}

void EditorFrame::CloseCanvas() noexcept {
    EndRendering();
    if (child_open) {
        ImGui::EndChild();
        ImGui::PopID();
        child_open = false;
    }
}

void EditorFrame::EndRendering() noexcept {
    if (splitter_active) {
        splitter.Merge(draw_list);
        splitter_active = false;
    }
}

void EditorFrame::RefreshGraph() noexcept {
    graph = document.FindGraph(graph_id);
}

void EditorFrame::RecordChange(const CommandResult& change) noexcept {
    result.model_changed |= change.model_changed;
    result.presentation_changed |= change.presentation_changed;
}

bool EditorFrame::ClearSelection() noexcept {
    const bool changed = !session.selected_nodes.empty() || !session.selected_links.empty() ||
        !session.selected_groups.empty() || !session.selected_route_points.empty();
    session.selected_nodes.clear();
    session.selected_links.clear();
    session.selected_groups.clear();
    session.selected_route_points.clear();
    return changed;
}

bool EditorFrame::PruneSelection() {
    bool changed = false;
    const auto node_visible = [&](const NodeId node) {
        return std::ranges::none_of(presentation.Groups(), [&](const auto& entry) {
            const auto& group_state = entry.second;
            return group_state.graph == graph_id && group_state.geometry.collapsed &&
                group_state.members.contains(node);
        });
    };
    const auto link_visible = [&](const LinkId link_id) {
        if (graph == nullptr) return false;
        const auto link = graph->links.find(link_id);
        if (link == graph->links.end()) return false;
        const auto output = graph->pins.find(link->second.output);
        const auto input = graph->pins.find(link->second.input);
        return output != graph->pins.end() && input != graph->pins.end() &&
            node_visible(output->second.node) && node_visible(input->second.node);
    };
    if (graph == nullptr) {
        changed = ClearSelection();
        session.interaction = Idle{};
        return changed;
    }
    for (auto iterator = session.selected_nodes.begin(); iterator != session.selected_nodes.end();) {
        if (graph->nodes.contains(*iterator) && node_visible(*iterator)) {
            ++iterator;
        } else {
            iterator = session.selected_nodes.erase(iterator);
            changed = true;
        }
    }
    for (auto iterator = session.selected_links.begin(); iterator != session.selected_links.end();) {
        if (link_visible(*iterator)) {
            ++iterator;
        } else {
            iterator = session.selected_links.erase(iterator);
            changed = true;
        }
    }
    for (auto iterator = session.selected_groups.begin(); iterator != session.selected_groups.end();) {
        const auto* group_state = presentation.FindGroup(*iterator);
        if (group_state != nullptr && group_state->graph == graph_id) {
            ++iterator;
        } else {
            iterator = session.selected_groups.erase(iterator);
            changed = true;
        }
    }
    for (auto iterator = session.selected_route_points.begin(); iterator != session.selected_route_points.end();) {
        if (presentation.FindRoutePoint(iterator->second, iterator->first) != nullptr &&
            link_visible(iterator->second)) {
            ++iterator;
        } else {
            iterator = session.selected_route_points.erase(iterator);
            changed = true;
        }
    }
    return changed;
}

void EditorFrame::NormalizeNavigation() {
    const auto breadcrumbs = context.Breadcrumbs();
    std::size_t valid_breadcrumbs = breadcrumbs.empty() ? 0 : 1;
    for (std::size_t index = 1; index < breadcrumbs.size(); ++index) {
        const auto* parent_node = document.FindNode(breadcrumbs[index - 1].graph, breadcrumbs[index].via_node);
        std::optional<GraphId> child;
        if (parent_node != nullptr && parent_node->subgraph) {
            if (const auto* local = std::get_if<DocumentGraphTarget>(&parent_node->subgraph->target)) {
                child = local->graph;
            }
        }
        if (!child || *child != breadcrumbs[index].graph || document.FindGraph(*child) == nullptr) break;
        valid_breadcrumbs = index + 1;
    }
    if (valid_breadcrumbs != breadcrumbs.size()) {
        if (valid_breadcrumbs == 0) {
            (void)context.ResetNavigation(document);
        } else {
            (void)context.NavigateToBreadcrumb(valid_breadcrumbs - 1);
        }
        result.active_graph_changed = true;
    }
    if (document.FindGraph(context.ActiveGraph()) == nullptr) {
        (void)context.ResetNavigation(document);
        result.active_graph_changed = true;
    }
}

void EditorFrame::FlushPendingCommands() {
    if (pending_commands.empty()) return;
    std::unique_ptr<Command> command;
    if (pending_commands.size() == 1) {
        command = std::move(pending_commands.front());
    } else {
        command = std::make_unique<CompoundCommand>("Edit node widgets", std::move(pending_commands));
    }
    auto edited = commands.Execute(
        std::move(command), document, presentation, registry, policy);
    if (!edited) {
        session.last_error = edited.error().message;
        return;
    }
    RecordChange(*edited);
    RefreshGraph();
    result.selection_changed |= PruneSelection();
    if (session.context_target) {
        const auto& target = *session.context_target;
        const bool valid = target.kind == ContextMenuTargetKind::Canvas ||
            (target.kind == ContextMenuTargetKind::Node && graph != nullptr && graph->nodes.contains(target.node)) ||
            (target.kind == ContextMenuTargetKind::Pin && graph != nullptr && graph->pins.contains(target.pin)) ||
            (target.kind == ContextMenuTargetKind::Link && graph != nullptr && graph->links.contains(target.link)) ||
            (target.kind == ContextMenuTargetKind::Group && presentation.FindGroup(target.group) != nullptr) ||
            (target.kind == ContextMenuTargetKind::RoutePoint &&
                presentation.FindRoutePoint(target.link, target.route_point) != nullptr);
        if (!valid) session.context_target.reset();
    }
    session.last_error.clear();
}

void EditorFrame::ApplyNavigation() {
    if (navigate_breadcrumb) {
        result.active_graph_changed = context.NavigateToBreadcrumb(*navigate_breadcrumb);
        return;
    }
    if (!enter_subgraph) return;

    const auto* node = document.FindNode(context.ActiveGraph(), *enter_subgraph);
    const auto* external = node != nullptr && node->subgraph
        ? std::get_if<GraphAssetTarget>(&node->subgraph->target)
        : nullptr;
    if (external != nullptr) {
        if (!callbacks.resolve_graph_asset) {
            session.last_error = "No graph document resolver is configured for external assets";
        } else if (auto resolved = callbacks.resolve_graph_asset(external->asset, external->interface); resolved) {
            if (resolved->asset != external->asset || !resolved->root_graph || resolved->generation == 0) {
                session.last_error = "Graph document resolver returned an invalid asset location";
            } else {
                result.open_graph_asset = GraphAssetNavigation{
                    .asset = external->asset,
                    .expected_interface = external->interface,
                    .generation = resolved->generation,
                    .content_hash = resolved->content_hash,
                    .root_graph = resolved->root_graph,
                };
                session.last_error.clear();
            }
        } else {
            session.last_error = resolved.error().message;
        }
        return;
    }
    auto entered = context.EnterSubgraph(document, *enter_subgraph);
    if (entered) {
        result.active_graph_changed = true;
        session.last_error.clear();
    } else {
        session.last_error = entered.error().message;
    }
}

ImVec2 EditorFrame::ToScreen(const Vec2 position) const noexcept {
    return {
        canvas_origin.x + session.pan.x + position.x * session.zoom,
        canvas_origin.y + session.pan.y + position.y * session.zoom,
    };
}

Vec2 EditorFrame::ToGraph(const ImVec2 position) const noexcept {
    return {
        (position.x - canvas_origin.x - session.pan.x) / session.zoom,
        (position.y - canvas_origin.y - session.pan.y) / session.zoom,
    };
}

} // namespace Uni::GUI::Nodes::EditorDetail
