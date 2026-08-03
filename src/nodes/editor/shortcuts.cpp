#include "internal/frame.h"

#include <variant>

namespace Uni::GUI::Nodes::EditorDetail {

void EditorFrame::ProcessShortcuts() {
    if (!config.enable_shortcuts || !std::holds_alternative<Idle>(session.interaction) ||
        !canvas_hovered || ImGui::GetIO().WantTextInput) {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Alt | ImGuiKey_LeftArrow) && context.Breadcrumbs().size() > 1) {
        navigate_breadcrumb = context.Breadcrumbs().size() - 2;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && session.selected_nodes.size() == 1) {
        const NodeId selected = *session.selected_nodes.begin();
        const auto* node = document.FindNode(graph_id, selected);
        if (node != nullptr && node->subgraph) enter_subgraph = selected;
    } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        auto undone = commands.Undo(
            document, presentation, policy, UndoPolicyMode::RespectCurrentPolicy, registry);
        if (undone) {
            RecordChange(*undone);
            RefreshGraph();
            if (undone->model_changed) {
                hovered_pin = {};
                hovered_node = {};
                hovered_link = {};
            }
            result.selection_changed |= PruneSelection();
            session.last_error.clear();
        } else {
            session.last_error = undone.error().message;
        }
    } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        auto redone = commands.Redo(document, presentation, registry, policy);
        if (redone) {
            RecordChange(*redone);
            RefreshGraph();
            if (redone->model_changed) {
                hovered_pin = {};
                hovered_node = {};
                hovered_link = {};
            }
            result.selection_changed |= PruneSelection();
            session.last_error.clear();
        } else {
            session.last_error = redone.error().message;
        }
    } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A)) {
        (void)ClearSelection();
        for (const auto& [node_id, node] : graph->nodes) {
            (void)node;
            if (!session.geometry.hidden_nodes.contains(node_id)) session.selected_nodes.insert(node_id);
        }
        for (const auto& [group_id, group] : presentation.Groups()) {
            if (group.graph == graph_id) session.selected_groups.insert(group_id);
        }
        result.selection_changed = true;
    } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) {
        (void)CopySelection();
    } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) {
        PasteClipboard(ToGraph(mouse));
    } else if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) {
        DuplicateSelection();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        (!session.selected_nodes.empty() || !session.selected_links.empty() ||
            !session.selected_groups.empty() || !session.selected_route_points.empty())) {
        std::vector<NodeId> selected_nodes(session.selected_nodes.begin(), session.selected_nodes.end());
        std::vector<LinkId> selected_links(session.selected_links.begin(), session.selected_links.end());
        std::vector<std::unique_ptr<Command>> deletions;
        if (!session.selected_route_points.empty()) {
            std::vector<RoutePointRef> points;
            for (const auto& [point, link] : session.selected_route_points) points.push_back({link, point});
            deletions.push_back(std::make_unique<RemoveRoutePointsCommand>(std::move(points)));
        }
        for (const GroupId group : session.selected_groups) {
            deletions.push_back(std::make_unique<RemoveGroupCommand>(group));
        }
        if (!selected_nodes.empty() || !selected_links.empty()) {
            deletions.push_back(std::make_unique<DeleteElementsCommand>(
                graph_id, std::move(selected_nodes), std::move(selected_links)));
        }
        auto deleted = commands.Execute(
            deletions.size() == 1
                ? std::move(deletions.front())
                : std::unique_ptr<Command>{
                    std::make_unique<CompoundCommand>("Delete elements", std::move(deletions))},
            document,
            presentation,
            registry,
            policy);
        if (deleted) {
            (void)ClearSelection();
            RecordChange(*deleted);
            RefreshGraph();
            hovered_pin = {};
            hovered_node = {};
            hovered_link = {};
            result.selection_changed = true;
            session.last_error.clear();
        } else {
            session.last_error = deleted.error().message;
        }
    }
}

} // namespace Uni::GUI::Nodes::EditorDetail
