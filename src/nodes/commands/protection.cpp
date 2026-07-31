#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;

struct SetGraphReadOnlyCommand::Impl final {
    GraphId graph;
    bool value;
    bool previous{false};
    bool captured{false};
};
SetGraphReadOnlyCommand::SetGraphReadOnlyCommand(const GraphId graph, const bool read_only)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .value = read_only})) {}
SetGraphReadOnlyCommand::~SetGraphReadOnlyCommand() = default;
std::string_view SetGraphReadOnlyCommand::Name() const noexcept { return "Set graph read-only"; }
Result<void> SetGraphReadOnlyCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* graph = transaction.Document().FindGraph(m_impl->graph);
    if (graph == nullptr) return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    if (!m_impl->captured) { m_impl->previous = graph->read_only; m_impl->captured = true; }
    return transaction.SetGraphReadOnly(m_impl->graph, m_impl->value);
}
Result<void> SetGraphReadOnlyCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetGraphReadOnly(m_impl->graph, m_impl->previous);
}

struct SetNodeReadOnlyCommand::Impl final {
    GraphId graph;
    NodeId node;
    bool value;
    bool previous{false};
    bool captured{false};
};
SetNodeReadOnlyCommand::SetNodeReadOnlyCommand(
    const GraphId graph, const NodeId node, const bool read_only)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .node = node, .value = read_only})) {}
SetNodeReadOnlyCommand::~SetNodeReadOnlyCommand() = default;
std::string_view SetNodeReadOnlyCommand::Name() const noexcept { return "Set node read-only"; }
Result<void> SetNodeReadOnlyCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* node = transaction.Document().FindNode(m_impl->graph, m_impl->node);
    if (node == nullptr) return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    if (!m_impl->captured) { m_impl->previous = node->read_only; m_impl->captured = true; }
    return transaction.SetNodeReadOnly(m_impl->graph, m_impl->node, m_impl->value);
}
Result<void> SetNodeReadOnlyCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetNodeReadOnly(m_impl->graph, m_impl->node, m_impl->previous);
}

struct SetPinReadOnlyCommand::Impl final {
    GraphId graph;
    PinId pin;
    bool value;
    bool previous{false};
    bool captured{false};
};
SetPinReadOnlyCommand::SetPinReadOnlyCommand(
    const GraphId graph, const PinId pin, const bool read_only)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .pin = pin, .value = read_only})) {}
SetPinReadOnlyCommand::~SetPinReadOnlyCommand() = default;
std::string_view SetPinReadOnlyCommand::Name() const noexcept { return "Set pin read-only"; }
Result<void> SetPinReadOnlyCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* pin = transaction.Document().FindPin(m_impl->graph, m_impl->pin);
    if (pin == nullptr) return std::unexpected(MakeError(ErrorCode::PinNotFound, "Pin does not exist"));
    if (!m_impl->captured) { m_impl->previous = pin->read_only; m_impl->captured = true; }
    return transaction.SetPinReadOnly(m_impl->graph, m_impl->pin, m_impl->value);
}
Result<void> SetPinReadOnlyCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetPinReadOnly(m_impl->graph, m_impl->pin, m_impl->previous);
}

struct SetLinkReadOnlyCommand::Impl final {
    GraphId graph;
    LinkId link;
    bool value;
    bool previous{false};
    bool captured{false};
};
SetLinkReadOnlyCommand::SetLinkReadOnlyCommand(
    const GraphId graph, const LinkId link, const bool read_only)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .link = link, .value = read_only})) {}
SetLinkReadOnlyCommand::~SetLinkReadOnlyCommand() = default;
std::string_view SetLinkReadOnlyCommand::Name() const noexcept { return "Set link read-only"; }
Result<void> SetLinkReadOnlyCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* link = transaction.Document().FindLink(m_impl->graph, m_impl->link);
    if (link == nullptr) return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    if (!m_impl->captured) { m_impl->previous = link->read_only; m_impl->captured = true; }
    return transaction.SetLinkReadOnly(m_impl->graph, m_impl->link, m_impl->value);
}
Result<void> SetLinkReadOnlyCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetLinkReadOnly(m_impl->graph, m_impl->link, m_impl->previous);
}

struct SetNodeLockedCommand::Impl final {
    NodeId node;
    bool value;
    bool previous{false};
    bool captured{false};
};
SetNodeLockedCommand::SetNodeLockedCommand(const NodeId node, const bool locked)
    : m_impl(std::make_unique<Impl>(Impl{.node = node, .value = locked})) {}
SetNodeLockedCommand::~SetNodeLockedCommand() = default;
std::string_view SetNodeLockedCommand::Name() const noexcept { return "Set node locked"; }
Result<void> SetNodeLockedCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* state = transaction.Presentation().FindNode(m_impl->node);
    if (state == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node has no presentation state"));
    if (!m_impl->captured) { m_impl->previous = state->locked; m_impl->captured = true; }
    return transaction.SetNodeLocked(m_impl->node, m_impl->value);
}
Result<void> SetNodeLockedCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetNodeLocked(m_impl->node, m_impl->previous);
}

struct SetLinkLockedCommand::Impl final {
    LinkId link;
    bool value;
    std::optional<LinkPresentation> previous;
    bool captured{false};
};
SetLinkLockedCommand::SetLinkLockedCommand(const LinkId link, const bool locked)
    : m_impl(std::make_unique<Impl>(Impl{.link = link, .value = locked})) {}
SetLinkLockedCommand::~SetLinkLockedCommand() = default;
std::string_view SetLinkLockedCommand::Name() const noexcept { return "Set link locked"; }
Result<void> SetLinkLockedCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* state = transaction.Presentation().FindLink(m_impl->link);
    if (state == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link has no presentation state"));
    if (!m_impl->captured) { m_impl->previous = *state; m_impl->captured = true; }
    return transaction.SetLinkLocked(m_impl->link, m_impl->value);
}
Result<void> SetLinkLockedCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetLinkPresentation(m_impl->link, m_impl->previous);
}

struct SetGroupLockedCommand::Impl final {
    GroupId group;
    bool value;
    bool previous{false};
    bool captured{false};
};
SetGroupLockedCommand::SetGroupLockedCommand(const GroupId group, const bool locked)
    : m_impl(std::make_unique<Impl>(Impl{.group = group, .value = locked})) {}
SetGroupLockedCommand::~SetGroupLockedCommand() = default;
std::string_view SetGroupLockedCommand::Name() const noexcept { return "Set group locked"; }
Result<void> SetGroupLockedCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* state = transaction.Presentation().FindGroup(m_impl->group);
    if (state == nullptr) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    if (!m_impl->captured) { m_impl->previous = state->protection.locked; m_impl->captured = true; }
    return transaction.SetGroupLocked(m_impl->group, m_impl->value);
}
Result<void> SetGroupLockedCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetGroupLocked(m_impl->group, m_impl->previous);
}

} // namespace Uni::GUI::Nodes
