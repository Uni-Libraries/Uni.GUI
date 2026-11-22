#pragma once

/// @file
/// @brief Deterministic, non-mutating node arrangement in graph coordinates.

#include <uni/gui/export.h>
#include <uni/gui/nodes/presentation.h>

#include <span>
#include <unordered_map>
#include <vector>

namespace Uni::GUI::Nodes {

using NodePositions = std::unordered_map<NodeId, Vec2, IdHash>;
using NodeSizes = std::unordered_map<NodeId, Vec2, IdHash>;

/// Original and proposed origins keyed by the same node IDs; apply through a move command for undo.
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

/// Spacing and fallback dimensions must be finite and positive. An explicit node size takes
/// precedence over persisted dimensions; zero persisted dimensions use the corresponding fallback.
struct LayoutOptions final {
    LayoutDirection direction{LayoutDirection::LeftToRight};
    Vec2 fallback_node_size{190.0f, 100.0f};
    Vec2 spacing{100.0f, 50.0f};
    NodeSizes node_sizes;
};

/// Align or distribute the bounds of at least two distinct nodes. An empty span selects the whole
/// graph. Duplicate IDs are ignored; a missing node or presentation entry produces an error.
[[nodiscard]] UNI_GUI_EXPORT Result<NodeLayout> ComputeNodeAlignment(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    GraphId graph,
    std::span<const NodeId> nodes,
    NodeAlignment alignment,
    Vec2 fallback_node_size = {190.0f, 100.0f},
    const NodeSizes& node_sizes = {});

/// Layer the induced graph (all nodes for an empty span), keeping strongly connected components
/// on one level and ordering each level by node ID. Requires a nonempty set with presentation entries.
[[nodiscard]] UNI_GUI_EXPORT Result<NodeLayout> ComputeAutoLayout(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    GraphId graph,
    std::span<const NodeId> nodes = {},
    const LayoutOptions& options = {});

} // namespace Uni::GUI::Nodes
