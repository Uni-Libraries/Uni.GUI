#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/presentation.h>

#include <span>
#include <unordered_map>
#include <vector>

namespace Uni::GUI::Nodes {

using NodePositions = std::unordered_map<NodeId, Vec2, IdHash>;
using NodeSizes = std::unordered_map<NodeId, Vec2, IdHash>;

struct NodeLayout final {
    NodePositions before;
    NodePositions after;
};

enum class NodeAlignment {
    Left,
    HorizontalCenter,
    Right,
    Top,
    VerticalCenter,
    Bottom,
    DistributeHorizontal,
    DistributeVertical,
};

enum class LayoutDirection {
    LeftToRight,
    TopToBottom,
};

struct LayoutOptions final {
    LayoutDirection direction{LayoutDirection::LeftToRight};
    Vec2 fallback_node_size{190.0f, 100.0f};
    Vec2 spacing{100.0f, 50.0f};
    NodeSizes node_sizes;
};

[[nodiscard]] UNI_GUI_EXPORT Result<NodeLayout> ComputeNodeAlignment(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    GraphId graph,
    std::span<const NodeId> nodes,
    NodeAlignment alignment,
    Vec2 fallback_node_size = {190.0f, 100.0f},
    const NodeSizes& node_sizes = {});

[[nodiscard]] UNI_GUI_EXPORT Result<NodeLayout> ComputeAutoLayout(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    GraphId graph,
    std::span<const NodeId> nodes = {},
    const LayoutOptions& options = {});

} // namespace Uni::GUI::Nodes
