#include "nodes/commands/transaction_internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;
using namespace TransactionDetail;

#define Record(...) StoreOperationLazy([&]() -> OperationIntent { return OperationIntent __VA_ARGS__; })

bool GraphTransaction::Impl::GraphReadOnly(const GraphId graph) const {
    const auto* baseline = baseline_document.FindGraph(graph);
    const auto* current = staged_document.FindGraph(graph);
    return (baseline != nullptr && baseline->read_only) || (current != nullptr && current->read_only);
}

bool GraphTransaction::Impl::NodeReadOnly(const GraphId graph, const NodeId node) const {
    const auto* baseline = baseline_document.FindNode(graph, node);
    const auto* current = staged_document.FindNode(graph, node);
    return (baseline != nullptr && baseline->read_only) || (current != nullptr && current->read_only);
}

bool GraphTransaction::Impl::PinReadOnly(const GraphId graph, const PinId pin) const {
    const auto* baseline = baseline_document.FindPin(graph, pin);
    const auto* current = staged_document.FindPin(graph, pin);
    return (baseline != nullptr && baseline->read_only) || (current != nullptr && current->read_only);
}

bool GraphTransaction::Impl::LinkReadOnly(const GraphId graph, const LinkId link) const {
    const auto* baseline = baseline_document.FindLink(graph, link);
    const auto* current = staged_document.FindLink(graph, link);
    return (baseline != nullptr && baseline->read_only) || (current != nullptr && current->read_only);
}

bool GraphTransaction::Impl::NodeLocked(const NodeId node) const {
    const auto* baseline = baseline_presentation.FindNode(node);
    const auto* current = staged_presentation.FindNode(node);
    return (baseline != nullptr && baseline->locked) || (current != nullptr && current->locked);
}

bool GraphTransaction::Impl::LinkLocked(const LinkId link) const {
    const auto* baseline = baseline_presentation.FindLink(link);
    const auto* current = staged_presentation.FindLink(link);
    return (baseline != nullptr && baseline->Style().locked) ||
        (current != nullptr && current->Style().locked);
}

bool GraphTransaction::Impl::GroupLocked(const GroupId group) const {
    const auto* baseline = baseline_presentation.FindGroup(group);
    const auto* current = staged_presentation.FindGroup(group);
    return (baseline != nullptr && baseline->protection.locked) ||
        (current != nullptr && current->protection.locked);
}

Result<void> GraphTransaction::Impl::CheckLinkRemoval(const GraphId graph, const LinkId link) const {
    if (staged_document.FindGraph(graph) == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto* current = staged_document.FindLink(graph, link);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    }
    const auto* output = staged_document.FindPin(graph, current->output);
    const auto* input = staged_document.FindPin(graph, current->input);
    if (GraphReadOnly(graph) || LinkReadOnly(graph, link) ||
        (output != nullptr && (PinReadOnly(graph, output->id) || NodeReadOnly(graph, output->node))) ||
        (input != nullptr && (PinReadOnly(graph, input->id) || NodeReadOnly(graph, input->node)))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph, link, or endpoint is read-only"));
    }
    return {};
}

Result<void> GraphTransaction::SetGraphReadOnly(const GraphId graph, const bool read_only) {
    const auto* current = Document().FindGraph(graph);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (current->read_only == read_only) return {};
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.SetGraphReadOnly(graph, read_only);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchGraph(graph, SemanticDomain::Value);
        m_impl->Record({OperationKind::SetProtection, OperationAction::Set,
                        ProtectionOperation{GraphProtectionTarget{graph}, ProtectionKind::ReadOnly, read_only}});
    }
    return result;
}

Result<void> GraphTransaction::SetNodeReadOnly(const GraphId graph, const NodeId node, const bool read_only) {
    const auto* target = Document().FindGraph(graph);
    const auto* current = Document().FindNode(graph, node);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    if (current->read_only == read_only) return {};
    if (m_impl->enforce_protection && target != nullptr && m_impl->GraphReadOnly(graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.SetNodeReadOnly(graph, node, read_only);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node, SemanticDomain::Value);
        m_impl->Record({OperationKind::SetProtection, OperationAction::Set,
                        ProtectionOperation{NodeProtectionTarget{graph, node}, ProtectionKind::ReadOnly, read_only}});
    }
    return result;
}

Result<void> GraphTransaction::SetPinReadOnly(const GraphId graph, const PinId pin, const bool read_only) {
    const auto* target = Document().FindGraph(graph);
    const auto* current = Document().FindPin(graph, pin);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Pin does not exist"));
    }
    if (current->read_only == read_only) return {};
    const auto* node = Document().FindNode(graph, current->node);
    if (m_impl->enforce_protection && target != nullptr &&
        (m_impl->GraphReadOnly(graph) || (node != nullptr && m_impl->NodeReadOnly(graph, node->id)))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.SetPinReadOnly(graph, pin, read_only);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchPin(graph, pin, SemanticDomain::Value);
        m_impl->Record({OperationKind::SetProtection, OperationAction::Set,
                        ProtectionOperation{PinProtectionTarget{graph, pin}, ProtectionKind::ReadOnly, read_only}});
    }
    return result;
}

Result<void> GraphTransaction::SetLinkReadOnly(const GraphId graph, const LinkId link, const bool read_only) {
    const auto* target = Document().FindGraph(graph);
    const auto* current = Document().FindLink(graph, link);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    }
    if (current->read_only == read_only) return {};
    if (m_impl->enforce_protection && target != nullptr && m_impl->GraphReadOnly(graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.SetLinkReadOnly(graph, link, read_only);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchLink(graph, link, SemanticDomain::Value);
        m_impl->Record({OperationKind::SetProtection, OperationAction::Set,
                        ProtectionOperation{LinkProtectionTarget{graph, link}, ProtectionKind::ReadOnly, read_only}});
    }
    return result;
}

Result<void> GraphTransaction::SetNodeLocked(const NodeId node, const bool locked) {
    const auto* current = Presentation().FindNode(node);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node has no presentation state"));
    }
    if (current->locked == locked) return {};
    const GraphId graph_id = FindNodeGraph(Document(), node);
    const auto* graph = graph_id ? Document().FindGraph(graph_id) : nullptr;
    if (m_impl->enforce_protection && graph != nullptr && m_impl->GraphReadOnly(graph_id)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    auto value = *current;
    value.locked = locked;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    m_impl->staged_presentation.SetNode(node, std::move(value));
    m_impl->presentation_changed = true;
    m_impl->journal.node_presentations.insert(node);
    m_impl->Record({OperationKind::SetProtection, OperationAction::Set,
                    ProtectionOperation{NodeProtectionTarget{graph_id, node}, ProtectionKind::Locked, locked}});
    return {};
}

Result<void> GraphTransaction::SetLinkLocked(const LinkId link, const bool locked) {
    const auto* current = Presentation().FindLink(link);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link has no presentation state"));
    }
    if (current->Style().locked == locked) return {};
    const GraphId graph_id = FindLinkGraph(Document(), link);
    const auto* graph = graph_id ? Document().FindGraph(graph_id) : nullptr;
    if (m_impl->enforce_protection && graph != nullptr && m_impl->GraphReadOnly(graph_id)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    if (auto result = m_impl->staged_presentation.SetLinkLocked(link, locked); !result) {
        m_impl->ReleaseOperations();
        return result;
    }
    m_impl->presentation_changed = true;
    m_impl->TouchLinkPresentation(
        link,
        LinkPresentationImpact::Style | LinkPresentationImpact::Protection);
    m_impl->Record({OperationKind::SetProtection, OperationAction::Set,
                    ProtectionOperation{LinkProtectionTarget{graph_id, link}, ProtectionKind::Locked, locked}});
    return {};
}

Result<void> GraphTransaction::SetGroupLocked(const GroupId group, const bool locked) {
    const auto* current = Presentation().FindGroup(group);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group does not exist"));
    }
    if (current->protection.locked == locked) return {};
    const auto* graph = Document().FindGraph(current->graph);
    if (m_impl->enforce_protection && graph != nullptr && m_impl->GraphReadOnly(current->graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    const GraphId graph_id = current->graph;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_presentation.SetGroupLocked(group, locked);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->presentation_changed = true;
        m_impl->journal.groups[group] |= GroupImpact::Protection;
        m_impl->Record({OperationKind::SetProtection, OperationAction::Set,
                        ProtectionOperation{GroupProtectionTarget{graph_id, group}, ProtectionKind::Locked, locked}});
    }
    return result;
}

#undef Record

} // namespace Uni::GUI::Nodes
