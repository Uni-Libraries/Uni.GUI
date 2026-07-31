#include <uni/gui/nodes/layout.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_set>

namespace Uni::GUI::Nodes {
namespace {

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

[[nodiscard]] bool ValidSize(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && value.x > 0.0f && value.y > 0.0f;
}

[[nodiscard]] bool Finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] Result<std::vector<NodeId>> ResolveNodes(
    const Graph& graph,
    const std::span<const NodeId> requested) {
    std::vector<NodeId> nodes;
    if (requested.empty()) {
        nodes.reserve(graph.nodes.size());
        for (const auto& [node, value] : graph.nodes) {
            (void)value;
            nodes.push_back(node);
        }
    } else {
        nodes.assign(requested.begin(), requested.end());
    }
    std::ranges::sort(nodes, {}, &NodeId::Value);
    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
    if (std::ranges::any_of(nodes, [&](const NodeId node) { return !graph.nodes.contains(node); })) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Layout contains a missing node"));
    }
    return nodes;
}

[[nodiscard]] Vec2 NodeSize(
    const GraphPresentation& presentation,
    const NodeId node,
    const Vec2 fallback,
    const NodeSizes& overrides) noexcept {
    if (const auto found = overrides.find(node); found != overrides.end()) {
        return found->second;
    }
    const auto* state = presentation.FindNode(node);
    if (state == nullptr) {
        return fallback;
    }
    return {
        state->size.x > 0.0f ? state->size.x : fallback.x,
        state->size.y > 0.0f ? state->size.y : fallback.y,
    };
}

} // namespace

Result<NodeLayout> ComputeNodeAlignment(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    const GraphId graph_id,
    const std::span<const NodeId> requested,
    const NodeAlignment alignment,
    const Vec2 fallback_node_size,
    const NodeSizes& node_sizes) {
    const auto* graph = document.FindGraph(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Layout graph does not exist"));
    }
    if (!ValidSize(fallback_node_size)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Fallback node size is invalid"));
    }
    auto resolved = ResolveNodes(*graph, requested);
    if (!resolved) {
        return std::unexpected(std::move(resolved.error()));
    }
    if (resolved->size() < 2) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Alignment requires at least two nodes"));
    }

    NodeLayout layout;
    struct Bounds final { NodeId id; Vec2 position; Vec2 size; };
    std::vector<Bounds> bounds;
    bounds.reserve(resolved->size());
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const NodeId node : *resolved) {
        const auto* state = presentation.FindNode(node);
        if (state == nullptr) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Aligned node has no presentation"));
        }
        const Vec2 size = NodeSize(presentation, node, fallback_node_size, node_sizes);
        if (!Finite(state->position) || !ValidSize(size)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Aligned node geometry is invalid"));
        }
        bounds.push_back({node, state->position, size});
        layout.before.emplace(node, state->position);
        min_x = std::min(min_x, state->position.x);
        min_y = std::min(min_y, state->position.y);
        max_x = std::max(max_x, state->position.x + size.x);
        max_y = std::max(max_y, state->position.y + size.y);
    }

    if (alignment == NodeAlignment::DistributeHorizontal || alignment == NodeAlignment::DistributeVertical) {
        const bool horizontal = alignment == NodeAlignment::DistributeHorizontal;
        std::ranges::sort(bounds, [horizontal](const Bounds& first, const Bounds& second) {
            const float first_value = horizontal ? first.position.x : first.position.y;
            const float second_value = horizontal ? second.position.x : second.position.y;
            return first_value == second_value ? first.id.Value() < second.id.Value() : first_value < second_value;
        });
        float total_size = 0.0f;
        for (const auto& value : bounds) {
            total_size += horizontal ? value.size.x : value.size.y;
        }
        const float extent = horizontal ? max_x - min_x : max_y - min_y;
        const float gap = (extent - total_size) / static_cast<float>(bounds.size() - 1);
        float cursor = horizontal ? min_x : min_y;
        for (const auto& value : bounds) {
            Vec2 position = value.position;
            if (horizontal) {
                position.x = cursor;
                cursor += value.size.x + gap;
            } else {
                position.y = cursor;
                cursor += value.size.y + gap;
            }
            layout.after.emplace(value.id, position);
        }
        if (std::ranges::any_of(layout.after, [](const auto& value) { return !Finite(value.second); })) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Alignment result is out of range"));
        }
        return layout;
    }

    for (const auto& value : bounds) {
        Vec2 position = value.position;
        switch (alignment) {
        case NodeAlignment::Left: position.x = min_x; break;
        case NodeAlignment::HorizontalCenter: position.x = (min_x + max_x - value.size.x) * 0.5f; break;
        case NodeAlignment::Right: position.x = max_x - value.size.x; break;
        case NodeAlignment::Top: position.y = min_y; break;
        case NodeAlignment::VerticalCenter: position.y = (min_y + max_y - value.size.y) * 0.5f; break;
        case NodeAlignment::Bottom: position.y = max_y - value.size.y; break;
        case NodeAlignment::DistributeHorizontal:
        case NodeAlignment::DistributeVertical: break;
        }
        layout.after.emplace(value.id, position);
    }
    if (std::ranges::any_of(layout.after, [](const auto& value) { return !Finite(value.second); })) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Alignment result is out of range"));
    }
    return layout;
}

Result<NodeLayout> ComputeAutoLayout(
    const GraphDocument& document,
    const GraphPresentation& presentation,
    const GraphId graph_id,
    const std::span<const NodeId> requested,
    const LayoutOptions& options) {
    const auto* graph = document.FindGraph(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Layout graph does not exist"));
    }
    if (!ValidSize(options.fallback_node_size) || !ValidSize(options.spacing)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Layout size or spacing is invalid"));
    }
    auto resolved = ResolveNodes(*graph, requested);
    if (!resolved) {
        return std::unexpected(std::move(resolved.error()));
    }
    if (resolved->empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Layout contains no nodes"));
    }

    std::unordered_set<NodeId, IdHash> included(resolved->begin(), resolved->end());
    std::unordered_map<NodeId, std::vector<NodeId>, IdHash> outgoing;
    std::unordered_map<NodeId, std::size_t, IdHash> level;
    for (const NodeId node : *resolved) {
        level.emplace(node, 0);
    }
    for (const auto& [link_id, link] : graph->links) {
        (void)link_id;
        const auto output = graph->pins.find(link.output);
        const auto input = graph->pins.find(link.input);
        if (output == graph->pins.end() || input == graph->pins.end() ||
            !included.contains(output->second.node) || !included.contains(input->second.node) ||
            output->second.node == input->second.node) {
            continue;
        }
        auto& targets = outgoing[output->second.node];
        if (std::ranges::find(targets, input->second.node) == targets.end()) {
            targets.push_back(input->second.node);
        }
    }
    for (auto& [node, targets] : outgoing) {
        (void)node;
        std::ranges::sort(targets, {}, &NodeId::Value);
    }

    std::unordered_map<NodeId, std::vector<NodeId>, IdHash> incoming;
    for (const auto& [node, targets] : outgoing) {
        for (const NodeId target : targets) incoming[target].push_back(node);
    }
    for (auto& [node, sources] : incoming) {
        (void)node;
        std::ranges::sort(sources, {}, &NodeId::Value);
    }

    struct DfsFrame final {
        NodeId node;
        std::size_t next_edge{0};
    };
    std::unordered_set<NodeId, IdHash> visited;
    std::vector<NodeId> finish_order;
    finish_order.reserve(resolved->size());
    for (const NodeId root : *resolved) {
        if (!visited.insert(root).second) continue;
        std::vector<DfsFrame> traversal{{root, 0}};
        while (!traversal.empty()) {
            auto& frame = traversal.back();
            const auto& targets = outgoing[frame.node];
            if (frame.next_edge < targets.size()) {
                const NodeId target = targets[frame.next_edge++];
                if (visited.insert(target).second) traversal.push_back({target, 0});
                continue;
            }
            finish_order.push_back(frame.node);
            traversal.pop_back();
        }
    }

    std::unordered_set<NodeId, IdHash> assigned;
    std::vector<std::vector<NodeId>> components;
    for (auto root = finish_order.rbegin(); root != finish_order.rend(); ++root) {
        if (!assigned.insert(*root).second) continue;
        std::vector<NodeId> component;
        std::vector<NodeId> traversal{*root};
        while (!traversal.empty()) {
            const NodeId member = traversal.back();
            traversal.pop_back();
            component.push_back(member);
            for (auto source = incoming[member].rbegin(); source != incoming[member].rend(); ++source) {
                if (assigned.insert(*source).second) traversal.push_back(*source);
            }
        }
        std::ranges::sort(component, {}, &NodeId::Value);
        components.push_back(std::move(component));
    }

    std::unordered_map<NodeId, std::size_t, IdHash> component_of;
    for (std::size_t component = 0; component < components.size(); ++component) {
        for (const NodeId node : components[component]) component_of.emplace(node, component);
    }
    std::vector<std::vector<std::size_t>> component_edges(components.size());
    std::vector<std::size_t> component_indegree(components.size(), 0);
    std::vector<std::size_t> component_level(components.size(), 0);
    for (const auto& [node, targets] : outgoing) {
        for (const NodeId target : targets) {
            const std::size_t source_component = component_of.at(node);
            const std::size_t target_component = component_of.at(target);
            if (source_component == target_component) continue;
            auto& edges = component_edges[source_component];
            if (std::ranges::find(edges, target_component) == edges.end()) {
                edges.push_back(target_component);
                ++component_indegree[target_component];
            }
        }
    }
    std::vector<std::size_t> pending_components;
    for (std::size_t component = 0; component < components.size(); ++component) {
        if (component_indegree[component] == 0) pending_components.push_back(component);
    }
    const auto component_order = [&](const std::size_t first, const std::size_t second) {
        return components[first].front().Value() < components[second].front().Value();
    };
    std::ranges::sort(pending_components, component_order);
    while (!pending_components.empty()) {
        const std::size_t component = pending_components.front();
        pending_components.erase(pending_components.begin());
        for (const std::size_t target : component_edges[component]) {
            component_level[target] = std::max(component_level[target], component_level[component] + 1);
            if (--component_indegree[target] == 0) {
                pending_components.push_back(target);
                std::ranges::sort(pending_components, component_order);
            }
        }
    }
    for (const NodeId node : *resolved) level[node] = component_level[component_of.at(node)];

    std::map<std::size_t, std::vector<NodeId>> layers;
    for (const NodeId node : *resolved) {
        layers[level[node]].push_back(node);
    }
    float origin_x = std::numeric_limits<float>::max();
    float origin_y = std::numeric_limits<float>::max();
    NodeLayout layout;
    for (const NodeId node : *resolved) {
        const auto* state = presentation.FindNode(node);
        if (state == nullptr) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Layout node has no presentation"));
        }
        if (!Finite(state->position)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Layout node position is invalid"));
        }
        layout.before.emplace(node, state->position);
        origin_x = std::min(origin_x, state->position.x);
        origin_y = std::min(origin_y, state->position.y);
    }

    float primary = options.direction == LayoutDirection::LeftToRight ? origin_x : origin_y;
    for (auto& [layer_index, nodes] : layers) {
        (void)layer_index;
        std::ranges::sort(nodes, {}, &NodeId::Value);
        float secondary = options.direction == LayoutDirection::LeftToRight ? origin_y : origin_x;
        float primary_extent = 0.0f;
        for (const NodeId node : nodes) {
            const Vec2 size = NodeSize(presentation, node, options.fallback_node_size, options.node_sizes);
            if (!ValidSize(size)) {
                return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Layout node size is invalid"));
            }
            const Vec2 position = options.direction == LayoutDirection::LeftToRight
                ? Vec2{primary, secondary}
                : Vec2{secondary, primary};
            layout.after.emplace(node, position);
            secondary += (options.direction == LayoutDirection::LeftToRight ? size.y : size.x) +
                (options.direction == LayoutDirection::LeftToRight ? options.spacing.y : options.spacing.x);
            primary_extent = std::max(
                primary_extent,
                options.direction == LayoutDirection::LeftToRight ? size.x : size.y);
        }
        primary += primary_extent +
            (options.direction == LayoutDirection::LeftToRight ? options.spacing.x : options.spacing.y);
    }
    if (!std::isfinite(primary) ||
        std::ranges::any_of(layout.after, [](const auto& value) { return !Finite(value.second); })) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Auto-layout result is out of range"));
    }
    return layout;
}

} // namespace Uni::GUI::Nodes
