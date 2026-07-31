#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {
namespace {

using CommandDetail::MakeError;
using CommandDetail::ValidGraphInterface;

[[nodiscard]] const GraphInterface* ResolveInterface(
    const GraphDocument& document,
    const SubgraphReference& reference) {
    if (const auto* local = std::get_if<DocumentGraphTarget>(&reference.target)) {
        const auto* graph = document.FindGraph(local->graph);
        return graph != nullptr ? &graph->interface : nullptr;
    }
    return &std::get<GraphAssetTarget>(reference.target).interface;
}

[[nodiscard]] bool PinConnected(const GraphTransaction& transaction, const PinId pin) {
    return !transaction.Document().IncidentLinks(pin).empty();
}

[[nodiscard]] Result<void> SyncInterfaceProjection(
    Graph& graph,
    const NodeId node_id,
    const GraphInterface& interface,
    const NodeRole role,
    GraphTransaction& transaction) {
    const auto node_found = graph.nodes.find(node_id);
    if (node_found == graph.nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Interface projection node does not exist"));
    }
    NodeInstance node = node_found->second;
    std::vector<const GraphInterfacePin*> expected;
    for (const auto& pin : interface.pins) {
        if (role == NodeRole::Subgraph ||
            (role == NodeRole::BoundaryInput && pin.direction == PinDirection::Input) ||
            (role == NodeRole::BoundaryOutput && pin.direction == PinDirection::Output)) {
            expected.push_back(&pin);
        }
    }

    std::vector<PinId> static_pins;
    std::unordered_map<std::string, PinId> dynamic_by_key;
    for (const PinId pin_id : node.pins) {
        const auto found = graph.pins.find(pin_id);
        if (found == graph.pins.end()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node references a missing pin"));
        }
        if (found->second.storage == PinStorage::Static) {
            static_pins.push_back(pin_id);
        } else {
            dynamic_by_key.emplace(found->second.key, pin_id);
        }
    }

    std::unordered_set<PinId, IdHash> retained;
    std::vector<PinId> projected;
    for (const auto* source : expected) {
        const PinDirection direction = role == NodeRole::BoundaryInput
            ? PinDirection::Output
            : role == NodeRole::BoundaryOutput ? PinDirection::Input : source->direction;
        const PinCardinality cardinality = role == NodeRole::Subgraph
            ? source->caller_cardinality
            : source->boundary_cardinality;
        PinId id;
        if (const auto existing = dynamic_by_key.find(source->key); existing != dynamic_by_key.end()) {
            id = existing->second;
            PinInstance pin = graph.pins.at(id);
            const bool structural = pin.type != source->type || pin.direction != direction ||
                pin.kind != source->kind || pin.cardinality != cardinality;
            if (structural && PinConnected(transaction, id)) {
                return std::unexpected(MakeError(
                    ErrorCode::InvalidGraph,
                    "Connected interface pins cannot change structural metadata"));
            }
            pin.label = source->label;
            pin.type = source->type;
            pin.direction = direction;
            pin.kind = source->kind;
            pin.cardinality = cardinality;
            graph.pins.insert_or_assign(id, std::move(pin));
        } else {
            id = transaction.AllocatePinId();
            if (!id) {
                return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pin IDs are exhausted"));
            }
            graph.pins.emplace(id, PinInstance{
                .id = id,
                .node = node.id,
                .key = source->key,
                .label = source->label,
                .type = source->type,
                .direction = direction,
                .kind = source->kind,
                .cardinality = cardinality,
                .storage = PinStorage::Dynamic,
            });
        }
        retained.insert(id);
        projected.push_back(id);
    }
    for (const auto& [key, pin] : dynamic_by_key) {
        (void)key;
        if (retained.contains(pin)) {
            continue;
        }
        if (PinConnected(transaction, pin)) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidGraph,
                "Connected interface pins cannot be removed"));
        }
        graph.pins.erase(pin);
    }
    node.pins = std::move(static_pins);
    node.pins.insert(node.pins.end(), projected.begin(), projected.end());
    graph.nodes.insert_or_assign(node_id, std::move(node));
    return {};
}

[[nodiscard]] Result<NodeId> EnsureBoundaryNode(
    Graph& graph,
    const NodeRole role,
    GraphTransaction& transaction) {
    const auto existing = transaction.Document().BoundaryNodes(graph.id, role);
    if (existing.size() > 1) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph has duplicate boundary nodes"));
    }
    if (!existing.empty()) {
        return *existing.begin();
    }
    const NodeId id = transaction.AllocateNodeId();
    if (!id) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node IDs are exhausted"));
    }
    const bool input = role == NodeRole::BoundaryInput;
    graph.nodes.emplace(id, NodeInstance{
        .id = id,
        .type = TypeId{input ? "uni.gui.nodes.boundary_input" : "uni.gui.nodes.boundary_output"},
        .display_name = input ? "Graph Input" : "Graph Output",
        .role = role,
    });
    return id;
}

} // namespace

struct SetNodeSubgraphCommand::Impl final {
    GraphId graph;
    NodeId node;
    std::optional<SubgraphReference> value;
    std::optional<Graph> before;
    std::optional<Graph> after;
    bool captured{false};
};

SetNodeSubgraphCommand::SetNodeSubgraphCommand(
    const GraphId graph, const NodeId node, std::optional<SubgraphReference> subgraph)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .node = node, .value = std::move(subgraph)})) {}
SetNodeSubgraphCommand::~SetNodeSubgraphCommand() = default;
std::string_view SetNodeSubgraphCommand::Name() const noexcept { return "Set node subgraph"; }
Result<void> SetNodeSubgraphCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (m_impl->after) {
        return transaction.ReplaceGraph(*m_impl->after);
    }
    const auto* current_graph = transaction.Document().FindGraph(m_impl->graph);
    const auto* current_node = transaction.Document().FindNode(m_impl->graph, m_impl->node);
    if (current_graph == nullptr || current_node == nullptr) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Subgraph node does not exist"));
    }
    if (current_node->role != NodeRole::Regular && current_node->role != NodeRole::Subgraph) {
        return std::unexpected(MakeError(
            ErrorCode::InvalidArgument,
            "Boundary and intergraph proxy nodes cannot become subgraph call-sites"));
    }
    if (!current_node->subgraph && m_impl->value &&
        std::ranges::any_of(current_node->pins, [&](const PinId pin) {
            const auto* value = transaction.Document().FindPin(m_impl->graph, pin);
            return value != nullptr && value->storage == PinStorage::Dynamic;
        })) {
        return std::unexpected(MakeError(
            ErrorCode::InvalidArgument,
            "A subgraph binding cannot take ownership of existing dynamic pins"));
    }
    if (current_node->subgraph && current_node->subgraph->ownership == SubgraphOwnership::Owned &&
        current_node->subgraph != m_impl->value) {
        return std::unexpected(MakeError(
            ErrorCode::InvalidGraph,
            "Owned subgraphs must be removed with their owner or explicitly promoted"));
    }
    if (m_impl->value) {
        if (m_impl->value->ownership == SubgraphOwnership::Owned &&
            std::holds_alternative<GraphAssetTarget>(m_impl->value->target)) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidArgument,
                "External graph assets cannot be owned directly"));
        }
        const auto* interface = ResolveInterface(transaction.Document(), *m_impl->value);
        if (interface == nullptr || !ValidGraphInterface(*interface)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph target interface is invalid"));
        }
    }

    m_impl->before = *current_graph;
    Graph changed = *current_graph;
    NodeInstance node = changed.nodes.at(m_impl->node);
    node.subgraph = m_impl->value;
    node.role = node.subgraph ? NodeRole::Subgraph : NodeRole::Regular;
    const GraphInterface empty;
    const GraphInterface* interface = node.subgraph
        ? ResolveInterface(transaction.Document(), *node.subgraph)
        : &empty;
    if (interface == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph target does not exist"));
    }
    changed.nodes.insert_or_assign(node.id, node);
    if (auto synced = SyncInterfaceProjection(changed, node.id, *interface, NodeRole::Subgraph, transaction);
        !synced) {
        return synced;
    }
    m_impl->after = changed;
    m_impl->captured = true;
    return transaction.ReplaceGraph(std::move(changed));
}
Result<void> SetNodeSubgraphCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->before) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Set subgraph command was not executed"));
    }
    return transaction.ReplaceGraph(*m_impl->before);
}

struct SetGraphInterfaceCommand::Impl final {
    GraphId graph;
    GraphInterface value;
    std::vector<Graph> before;
    std::vector<Graph> after;
};

SetGraphInterfaceCommand::SetGraphInterfaceCommand(const GraphId graph, GraphInterface interface)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .value = std::move(interface)})) {}
SetGraphInterfaceCommand::~SetGraphInterfaceCommand() = default;
std::string_view SetGraphInterfaceCommand::Name() const noexcept { return "Set graph interface"; }
Result<void> SetGraphInterfaceCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->after.empty()) {
        for (const auto& graph : m_impl->after) {
            if (auto replaced = transaction.ReplaceGraph(graph); !replaced) {
                return replaced;
            }
        }
        return {};
    }
    if (!ValidGraphInterface(m_impl->value)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Graph interface is invalid"));
    }
    const auto* target = transaction.Document().FindGraph(m_impl->graph);
    if (target == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Interface graph does not exist"));
    }

    std::unordered_map<GraphId, Graph, IdHash> changed;
    changed.emplace(target->id, *target);
    auto& target_copy = changed.at(target->id);
    target_copy.interface = m_impl->value;
    auto input = EnsureBoundaryNode(target_copy, NodeRole::BoundaryInput, transaction);
    if (!input) {
        return std::unexpected(std::move(input.error()));
    }
    auto output = EnsureBoundaryNode(target_copy, NodeRole::BoundaryOutput, transaction);
    if (!output) {
        return std::unexpected(std::move(output.error()));
    }
    if (auto synced = SyncInterfaceProjection(
            target_copy, *input, target_copy.interface, NodeRole::BoundaryInput, transaction); !synced) {
        return synced;
    }
    if (auto synced = SyncInterfaceProjection(
            target_copy, *output, target_copy.interface, NodeRole::BoundaryOutput, transaction); !synced) {
        return synced;
    }

    for (const SubgraphCallSite& caller : transaction.Document().SubgraphCallers(m_impl->graph)) {
        const auto* graph = transaction.Document().FindGraph(caller.graph);
        if (graph == nullptr) continue;
        auto [entry, inserted] = changed.try_emplace(graph->id, *graph);
        (void)inserted;
        if (auto synced = SyncInterfaceProjection(
                entry->second, caller.node, m_impl->value, NodeRole::Subgraph, transaction); !synced) {
            return synced;
        }
    }

    std::vector<GraphId> order;
    order.reserve(changed.size());
    for (const auto& [graph_id, graph] : changed) {
        (void)graph;
        order.push_back(graph_id);
    }
    std::ranges::sort(order, {}, &GraphId::Value);
    for (const GraphId graph_id : order) {
        const auto* before = transaction.Document().FindGraph(graph_id);
        m_impl->before.push_back(*before);
        m_impl->after.push_back(changed.at(graph_id));
    }
    for (const auto& graph : m_impl->after) {
        if (auto replaced = transaction.ReplaceGraph(graph); !replaced) {
            return replaced;
        }
    }
    return {};
}
Result<void> SetGraphInterfaceCommand::Revert(GraphTransaction& transaction) {
    if (m_impl->before.empty()) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Set interface command was not executed"));
    }
    for (const auto& graph : m_impl->before) {
        if (auto replaced = transaction.ReplaceGraph(graph); !replaced) {
            return replaced;
        }
    }
    return {};
}

struct ConnectIntergraphCommand::Impl final { IntergraphLink link; };
ConnectIntergraphCommand::ConnectIntergraphCommand(IntergraphLink link)
    : m_impl(std::make_unique<Impl>(Impl{std::move(link)})) {}
ConnectIntergraphCommand::~ConnectIntergraphCommand() = default;
std::string_view ConnectIntergraphCommand::Name() const noexcept { return "Connect graphs"; }
Result<void> ConnectIntergraphCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    return transaction.AddIntergraphLink(m_impl->link);
}
Result<void> ConnectIntergraphCommand::Revert(GraphTransaction& transaction) {
    auto removed = transaction.RemoveIntergraphLink(m_impl->link.id);
    return removed ? Result<void>{} : std::unexpected(std::move(removed.error()));
}

struct DisconnectIntergraphCommand::Impl final {
    IntergraphLinkId link;
    std::optional<IntergraphLink> removed;
};
DisconnectIntergraphCommand::DisconnectIntergraphCommand(const IntergraphLinkId link)
    : m_impl(std::make_unique<Impl>(Impl{.link = link})) {}
DisconnectIntergraphCommand::~DisconnectIntergraphCommand() = default;
std::string_view DisconnectIntergraphCommand::Name() const noexcept { return "Disconnect graphs"; }
Result<void> DisconnectIntergraphCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    auto removed = transaction.RemoveIntergraphLink(m_impl->link);
    if (!removed) {
        return std::unexpected(std::move(removed.error()));
    }
    if (!m_impl->removed) {
        m_impl->removed = *removed;
    }
    return {};
}
Result<void> DisconnectIntergraphCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->removed) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Disconnect command was not executed"));
    }
    return transaction.AddIntergraphLink(*m_impl->removed);
}

} // namespace Uni::GUI::Nodes
