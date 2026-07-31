#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;

struct AddDynamicPinCommand::Impl final {
    GraphId graph;
    PinInstance pin;
    std::size_t index;
};

AddDynamicPinCommand::AddDynamicPinCommand(const GraphId graph, PinInstance pin, const std::size_t index)
    : m_impl(std::make_unique<Impl>(Impl{graph, std::move(pin), index})) {}
AddDynamicPinCommand::~AddDynamicPinCommand() = default;
std::string_view AddDynamicPinCommand::Name() const noexcept { return "Add dynamic pin"; }
Result<void> AddDynamicPinCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    return transaction.AddDynamicPin(m_impl->graph, m_impl->pin, m_impl->index);
}
Result<void> AddDynamicPinCommand::Revert(GraphTransaction& transaction) {
    auto removed = transaction.RemoveDynamicPin(m_impl->graph, m_impl->pin.id);
    return removed ? Result<void>{} : std::unexpected(std::move(removed.error()));
}

struct RemoveDynamicPinCommand::Impl final {
    GraphId graph;
    PinId pin;
    std::optional<RemovedPin> removed;
    std::vector<std::pair<LinkId, LinkPresentation>> link_presentations;
};

RemoveDynamicPinCommand::RemoveDynamicPinCommand(const GraphId graph, const PinId pin)
    : m_impl(std::make_unique<Impl>()) { m_impl->graph = graph; m_impl->pin = pin; }
RemoveDynamicPinCommand::~RemoveDynamicPinCommand() = default;
std::string_view RemoveDynamicPinCommand::Name() const noexcept { return "Remove dynamic pin"; }
Result<void> RemoveDynamicPinCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->removed) {
        const auto* graph = transaction.Document().FindGraph(m_impl->graph);
        if (graph == nullptr) {
            return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
        }
        for (const auto& [link, value] : graph->links) {
            if (value.output == m_impl->pin || value.input == m_impl->pin) {
                if (const auto* state = transaction.Presentation().FindLink(link)) {
                    m_impl->link_presentations.emplace_back(link, *state);
                }
            }
        }
    }
    auto removed = transaction.RemoveDynamicPin(m_impl->graph, m_impl->pin);
    if (!removed) {
        return std::unexpected(std::move(removed.error()));
    }
    if (!m_impl->removed) {
        m_impl->removed = *removed;
    }
    for (const auto& link : removed->links) {
        if (auto result = transaction.SetLinkPresentation(link.id, std::nullopt); !result) return result;
    }
    return {};
}
Result<void> RemoveDynamicPinCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->removed) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Remove pin command was not executed"));
    }
    if (auto result = transaction.RestoreDynamicPin(m_impl->graph, *m_impl->removed); !result) return result;
    for (const auto& [link, state] : m_impl->link_presentations) {
        if (auto result = transaction.SetLinkPresentation(link, state); !result) return result;
    }
    return {};
}

struct UpdateDynamicPinCommand::Impl final {
    GraphId graph;
    PinInstance value;
    std::optional<PinInstance> previous;
};

UpdateDynamicPinCommand::UpdateDynamicPinCommand(const GraphId graph, PinInstance pin)
    : m_impl(std::make_unique<Impl>(Impl{graph, std::move(pin), std::nullopt})) {}
UpdateDynamicPinCommand::~UpdateDynamicPinCommand() = default;
std::string_view UpdateDynamicPinCommand::Name() const noexcept { return "Update dynamic pin"; }
Result<void> UpdateDynamicPinCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->previous) {
        const auto* current = transaction.Document().FindPin(m_impl->graph, m_impl->value.id);
        if (current == nullptr) {
            return std::unexpected(MakeError(ErrorCode::PinNotFound, "Pin does not exist"));
        }
        m_impl->previous = *current;
    }
    return transaction.UpdateDynamicPin(m_impl->graph, m_impl->value);
}
Result<void> UpdateDynamicPinCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->previous) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Update pin command was not executed"));
    }
    return transaction.UpdateDynamicPin(m_impl->graph, *m_impl->previous);
}

struct ReorderDynamicPinsCommand::Impl final {
    GraphId graph;
    NodeId node;
    std::vector<PinId> value;
    std::vector<PinId> previous;
    bool captured{false};
};

ReorderDynamicPinsCommand::ReorderDynamicPinsCommand(
    const GraphId graph,
    const NodeId node,
    std::vector<PinId> order)
    : m_impl(std::make_unique<Impl>(Impl{
          .graph = graph,
          .node = node,
          .value = std::move(order),
      })) {}
ReorderDynamicPinsCommand::~ReorderDynamicPinsCommand() = default;
std::string_view ReorderDynamicPinsCommand::Name() const noexcept { return "Reorder dynamic pins"; }
Result<void> ReorderDynamicPinsCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        const auto* node = transaction.Document().FindNode(m_impl->graph, m_impl->node);
        if (node == nullptr) {
            return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
        }
        m_impl->previous = node->pins;
        m_impl->captured = true;
    }
    return transaction.ReorderDynamicPins(m_impl->graph, m_impl->node, m_impl->value);
}
Result<void> ReorderDynamicPinsCommand::Revert(GraphTransaction& transaction) {
    return transaction.ReorderDynamicPins(m_impl->graph, m_impl->node, m_impl->previous);
}

} // namespace Uni::GUI::Nodes
