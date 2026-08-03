#include "internal.h"

#include <algorithm>
#include <limits>

namespace Uni::GUI::Nodes {

void EditorContext::ClearSelection() noexcept {
    auto& state = Detail::EditorAccess::Session(*this);
    state.selected_nodes.clear();
    state.selected_links.clear();
    state.selected_groups.clear();
    state.selected_route_points.clear();
    state.selection_graph = state.active_graph.value_or(GraphId{});
    if (state.external_revision != std::numeric_limits<std::uint64_t>::max()) ++state.external_revision;
}

void EditorContext::SetSelection(GraphSelection selection) {
    auto& state = Detail::EditorAccess::Session(*this);
    ClearSelection();
    if (state.active_graph && selection.graph && selection.graph != *state.active_graph) {
        return;
    }
    state.selection_graph = selection.graph;
    for (const NodeId node : selection.nodes) if (node) state.selected_nodes.insert(node);
    for (const LinkId link : selection.links) if (link) state.selected_links.insert(link);
    for (const GroupId group : selection.groups) if (group) state.selected_groups.insert(group);
    for (const auto& point : selection.route_points) {
        if (point.link && point.point) state.selected_route_points.insert_or_assign(point.point, point.link);
    }
}

GraphSelection EditorContext::Selection() const {
    const auto& state = Detail::EditorAccess::Session(*this);
    GraphSelection selection{
        .graph = state.active_graph.value_or(state.selection_graph),
    };
    selection.nodes.assign(state.selected_nodes.begin(), state.selected_nodes.end());
    selection.links.assign(state.selected_links.begin(), state.selected_links.end());
    selection.groups.assign(state.selected_groups.begin(), state.selected_groups.end());
    for (const auto& [point, link] : state.selected_route_points) {
        selection.route_points.push_back({link, point});
    }
    std::ranges::sort(selection.nodes, {}, &NodeId::Value);
    std::ranges::sort(selection.links, {}, &LinkId::Value);
    std::ranges::sort(selection.groups, {}, &GroupId::Value);
    std::ranges::sort(selection.route_points, [](const RoutePointRef& first, const RoutePointRef& second) {
        return first.link == second.link
            ? first.point.Value() < second.point.Value()
            : first.link.Value() < second.link.Value();
    });
    return selection;
}

} // namespace Uni::GUI::Nodes
