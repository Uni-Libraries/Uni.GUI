#pragma once

/// @file
/// @brief Owning, graph-scoped editor selection values.

#include <uni/gui/nodes/ids.h>

#include <vector>

namespace Uni::GUI::Nodes {

/// A manual point and its owning link. Both IDs must match the live presentation.
struct RoutePointRef final {
    LinkId link;
    RoutePointId point;

    bool operator==(const RoutePointRef&) const = default;
};

/// A snapshot, not a live view. EditorContext::Selection() returns unique, ID-sorted
/// entities; points are ordered by (link, point). A snapshot does not keep entities alive.
struct GraphSelection final {
    GraphId graph;
    std::vector<NodeId> nodes;
    std::vector<LinkId> links;
    std::vector<GroupId> groups;
    std::vector<RoutePointRef> route_points;

    /// True when no entity is selected, independent of the graph ID.
    [[nodiscard]] bool Empty() const noexcept {
        return nodes.empty() && links.empty() && groups.empty() && route_points.empty();
    }

    bool operator==(const GraphSelection&) const = default;
};

} // namespace Uni::GUI::Nodes
