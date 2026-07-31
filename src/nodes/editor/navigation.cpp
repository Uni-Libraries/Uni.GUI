#include "internal.h"

#include <variant>

namespace Uni::GUI::Nodes {

GraphId EditorContext::ActiveGraph() const noexcept {
    return Detail::EditorAccess::Session(*this).active_graph.value_or(GraphId{});
}

std::vector<Breadcrumb> EditorContext::Breadcrumbs() const {
    return Detail::EditorAccess::Session(*this).navigation;
}

Result<void> EditorContext::ResetNavigation(const GraphDocument& document, GraphId graph) {
    auto& state = Detail::EditorAccess::Session(*this);
    if (!graph) graph = document.RootGraph();
    const auto* target = document.FindGraph(graph);
    if (target == nullptr) {
        return std::unexpected(Error{ErrorCode::GraphNotFound, "Navigation root graph does not exist"});
    }
    const bool preserve_selection = state.selection_graph == graph;
    state.navigation.clear();
    state.views.clear();
    state.active_graph = graph;
    state.navigation.push_back(Breadcrumb{
        .graph = graph,
        .label = target->display_name.empty() ? "Root" : target->display_name,
    });
    state.pan = {};
    state.zoom = 1.0f;
    if (!preserve_selection) ClearSelection();
    state.selection_graph = graph;
    state.interaction = EditorDetail::Idle{};
    state.context_target.reset();
    state.node_popup_graph = {};
    return {};
}

Result<void> EditorContext::EnterSubgraph(const GraphDocument& document, const NodeId node_id) {
    auto& state = Detail::EditorAccess::Session(*this);
    if (!state.active_graph) {
        return std::unexpected(Error{ErrorCode::GraphNotFound, "Editor navigation is not initialized"});
    }
    const auto* node = document.FindNode(*state.active_graph, node_id);
    if (node == nullptr || !node->subgraph) {
        return std::unexpected(Error{ErrorCode::NodeNotFound, "Node has no subgraph target"});
    }
    const auto* local = std::get_if<DocumentGraphTarget>(&node->subgraph->target);
    if (local == nullptr) {
        return std::unexpected(Error{
            ErrorCode::InvalidArgument,
            "External graph assets must be opened through their asset document",
        });
    }
    const auto* target = document.FindGraph(local->graph);
    if (target == nullptr) {
        return std::unexpected(Error{ErrorCode::GraphNotFound, "Subgraph target does not exist"});
    }

    state.views.insert_or_assign(*state.active_graph, EditorDetail::EditorState::ViewState{
        .pan = state.pan,
        .zoom = state.zoom,
        .selection = Selection(),
    });
    state.active_graph = local->graph;
    state.navigation.push_back(Breadcrumb{
        .graph = local->graph,
        .via_node = node_id,
        .label = target->display_name.empty()
            ? (node->display_name.empty() ? node->type.Value() : node->display_name)
            : target->display_name,
    });
    if (const auto view = state.views.find(local->graph); view != state.views.end()) {
        state.pan = view->second.pan;
        state.zoom = view->second.zoom;
        SetSelection(view->second.selection);
    } else {
        state.pan = {};
        state.zoom = 1.0f;
        ClearSelection();
    }
    state.interaction = EditorDetail::Idle{};
    state.context_target.reset();
    state.node_popup_graph = {};
    state.minimap_navigation.reset();
    return {};
}

bool EditorContext::NavigateBack() noexcept {
    auto& state = Detail::EditorAccess::Session(*this);
    if (state.navigation.size() <= 1 || !state.active_graph) return false;
    state.views.insert_or_assign(*state.active_graph, EditorDetail::EditorState::ViewState{
        .pan = state.pan,
        .zoom = state.zoom,
        .selection = Selection(),
    });
    state.navigation.pop_back();
    state.active_graph = state.navigation.back().graph;
    if (const auto view = state.views.find(*state.active_graph); view != state.views.end()) {
        state.pan = view->second.pan;
        state.zoom = view->second.zoom;
        SetSelection(view->second.selection);
    } else {
        state.pan = {};
        state.zoom = 1.0f;
        ClearSelection();
    }
    state.interaction = EditorDetail::Idle{};
    state.context_target.reset();
    state.node_popup_graph = {};
    state.minimap_navigation.reset();
    return true;
}

bool EditorContext::NavigateToBreadcrumb(const std::size_t index) noexcept {
    auto& state = Detail::EditorAccess::Session(*this);
    if (index >= state.navigation.size() || index + 1 == state.navigation.size()) return false;
    bool changed = false;
    while (state.navigation.size() > index + 1) changed |= NavigateBack();
    return changed;
}

} // namespace Uni::GUI::Nodes
