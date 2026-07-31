#pragma once

#include <uni/gui/nodes/ids.h>

#include <vector>

namespace Uni::GUI::Nodes {

struct RoutePointRef final {
    LinkId link;
    RoutePointId point;

    bool operator==(const RoutePointRef&) const = default;
};

struct GraphSelection final {
    GraphId graph;
    std::vector<NodeId> nodes;
    std::vector<LinkId> links;
    std::vector<GroupId> groups;
    std::vector<RoutePointRef> route_points;

    [[nodiscard]] bool Empty() const noexcept {
        return nodes.empty() && links.empty() && groups.empty() && route_points.empty();
    }

    bool operator==(const GraphSelection&) const = default;
};

} // namespace Uni::GUI::Nodes
