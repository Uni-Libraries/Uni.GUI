#include "nodes/commands/transaction_internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;
using namespace TransactionDetail;

#define Record(...) StoreOperationLazy([&]() -> OperationIntent { return OperationIntent __VA_ARGS__; })

Result<ConnectionResult> GraphTransaction::AuthorizeConnection(const ConnectionRequest& request) {
    if (!m_impl->enforce_protection) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Connection admission is unavailable"));
    }
    const auto* first = Document().FindPin(request.graph, request.first);
    const auto* second = Document().FindPin(request.graph, request.second);
    if (first != nullptr && second != nullptr &&
        (m_impl->GraphReadOnly(request.graph) || m_impl->PinReadOnly(request.graph, first->id) ||
         m_impl->PinReadOnly(request.graph, second->id) || m_impl->NodeReadOnly(request.graph, first->node) ||
         m_impl->NodeReadOnly(request.graph, second->node))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or connection endpoint is read-only"));
    }
    if (request.replacing) {
        if (m_impl->LinkLocked(request.replacing)) {
            return std::unexpected(MakeError(ErrorCode::Locked, "Reconnected link presentation is locked"));
        }
        if (auto removable = m_impl->CheckLinkRemoval(request.graph, request.replacing); !removable) {
            return std::unexpected(std::move(removable.error()));
        }
    }
    const auto result = Detail::ValidateConnection(Document(), Presentation(), request, m_impl->registry);
    if (result.status == ConnectionResult::Status::Rejected) {
        return std::unexpected(MakeError(result.error, result.reason));
    }
    return result;
}

Result<void> GraphTransaction::AddPlannedLink(const GraphId graph, Link link) {
    const auto* output = Document().FindPin(graph, link.output);
    const auto* input = Document().FindPin(graph, link.input);
    if (output == nullptr || input == nullptr) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Planned link endpoint does not exist"));
    }
    const auto compatibility = m_impl->registry.Check(output->type, input->type, output->kind);
    if (compatibility.status != ConnectionResult::Status::Allowed) {
        return std::unexpected(MakeError(ErrorCode::IncompatiblePins, compatibility.reason));
    }
    const Link value = link;
    const LinkId id = link.id;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.AddLink(graph, std::move(link));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchLink(graph, id, SemanticDomain::Topology);
        m_impl->Record({OperationKind::Connect, OperationAction::Set, LinkOperation{graph, value}});
    }
    return result;
}

Result<Link> GraphTransaction::RemovePlannedLink(const GraphId graph, const LinkId link) {
    if (auto budget = m_impl->ConsumeOperations(); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    auto result = m_impl->staged_document.RemoveLink(graph, link);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchLink(graph, link, SemanticDomain::Topology);
        m_impl->Record({OperationKind::Connect, OperationAction::Erase, LinkOperation{graph, *result}});
    }
    return result;
}

Result<void> GraphTransaction::SetSchemaVersion(const std::uint32_t version) {
    if (version == 0) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Schema version cannot be zero"));
    }
    if (Document().SchemaVersion() != version) {
        if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
        m_impl->staged_document.SetSchemaVersion(version);
        m_impl->model_changed = true;
        m_impl->journal.schema = true;
        m_impl->Record({OperationKind::SetSchemaVersion, OperationAction::Set, SchemaVersionOperation{version}});
    }
    return {};
}

Result<void> GraphTransaction::SetRootGraph(const GraphId graph) {
    if (Document().RootGraph() == graph) {
        return {};
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    if (auto result = m_impl->staged_document.SetRootGraph(graph); !result) {
        m_impl->ReleaseOperations();
        return result;
    }
    m_impl->model_changed = true;
    m_impl->journal.root = true;
    m_impl->Record({OperationKind::SetRootGraph, OperationAction::Set, RootGraphOperation{graph}});
    return {};
}

Result<GraphId> GraphTransaction::AddGraph(Graph graph) {
    const GraphId id = graph.id;
    const std::size_t operation_count = 1 + graph.nodes.size() + graph.pins.size() + graph.links.size();
    if (m_impl->enforce_protection) {
        for (const auto& [link_id, link] : graph.links) {
            (void)link_id;
            const auto output = graph.pins.find(link.output);
            const auto input = graph.pins.find(link.input);
            if (output == graph.pins.end() || input == graph.pins.end()) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph link endpoint is missing"));
            }
            if (m_impl->registry.Check(output->second.type, input->second.type, output->second.kind).status !=
                    ConnectionResult::Status::Allowed) {
                return std::unexpected(MakeError(ErrorCode::IncompatiblePins, "Graph contains an incompatible link"));
            }
        }
    }
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    if (auto result = m_impl->staged_document.RestoreGraph(std::move(graph)); !result) {
        m_impl->ReleaseOperations(operation_count);
        return std::unexpected(std::move(result.error()));
    }
    m_impl->model_changed = true;
    m_impl->TouchGraph(id, SemanticDomain::Topology | SemanticDomain::Layout);
    m_impl->RecordGraphAdded(*Document().FindGraph(id));
    return id;
}

Result<Graph> GraphTransaction::RemoveGraph(const GraphId graph) {
    const auto* current = Document().FindGraph(graph);
    if (current != nullptr && m_impl->enforce_protection) {
        if (m_impl->GraphReadOnly(graph)) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
        }
        for (const auto& [node, value] : current->nodes) {
            if (m_impl->NodeReadOnly(graph, node)) {
                return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph contains a read-only node"));
            }
            for (const PinId pin : value.pins) {
                if (m_impl->PinReadOnly(graph, pin)) {
                    return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph contains a read-only pin"));
                }
            }
        }
        for (const auto& [link, value] : current->links) {
            (void)value;
            if (auto allowed = m_impl->CheckLinkRemoval(graph, link); !allowed) {
                return std::unexpected(std::move(allowed.error()));
            }
        }
    }
    const std::size_t operation_count =
        current != nullptr ? 1 + current->nodes.size() + current->pins.size() + current->links.size() : 1;
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    auto result = m_impl->staged_document.RemoveGraph(graph);
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchGraph(graph, SemanticDomain::Topology | SemanticDomain::Layout);
        m_impl->RecordGraphRemoved(*result);
    }
    return result;
}

Result<void> GraphTransaction::RestoreGraph(Graph graph) {
    const GraphId graph_id = graph.id;
    if (m_impl->enforce_protection) {
        return std::unexpected(
            MakeError(ErrorCode::CommandFailed, "Graph snapshots can only be restored while undoing history"));
    }
    const std::size_t operation_count = 1 + graph.nodes.size() + graph.pins.size() + graph.links.size();
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) return budget;
    auto result = m_impl->staged_document.RestoreGraph(std::move(graph));
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchGraph(graph_id, SemanticDomain::Topology | SemanticDomain::Layout);
        m_impl->RecordGraphAdded(*Document().FindGraph(graph_id));
    }
    return result;
}

Result<void> GraphTransaction::ReplaceGraph(Graph graph) {
    const GraphId graph_id = graph.id;
    const auto* current = Document().FindGraph(graph.id);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (m_impl->enforce_protection && m_impl->GraphReadOnly(graph.id)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    if (m_impl->enforce_protection) {
        if (current->read_only != graph.read_only) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph protection must be changed explicitly"));
        }
        bool protected_node = false;
        current->nodes.ForEachDifference(
            graph.nodes, [&](const NodeId id, const NodeInstance* before, const NodeInstance* after) {
                protected_node |= before != nullptr && (m_impl->NodeReadOnly(graph.id, id) ||
                                                        (after != nullptr && before->read_only != after->read_only));
            });
        if (protected_node) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Read-only nodes cannot be replaced and node "
                                                                  "protection must be changed explicitly"));
        }
        bool protected_pin = false;
        current->pins.ForEachDifference(
            graph.pins, [&](const PinId id, const PinInstance* before, const PinInstance* after) {
                protected_pin |= before != nullptr &&
                                 (m_impl->NodeReadOnly(graph.id, before->node) || m_impl->PinReadOnly(graph.id, id) ||
                                  (after != nullptr && before->read_only != after->read_only));
            });
        if (protected_pin) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Read-only pins cannot be replaced and pin "
                                                                  "protection must be changed explicitly"));
        }
        bool protected_link = false;
        current->links.ForEachDifference(graph.links, [&](const LinkId id, const Link* before, const Link* after) {
            protected_link |= before != nullptr && (m_impl->LinkReadOnly(graph.id, id) ||
                                                    (after != nullptr && before->read_only != after->read_only));
        });
        if (protected_link) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Read-only links cannot be replaced and link "
                                                                  "protection must be changed explicitly"));
        }
    }
    if (*current == graph) {
        return {};
    }
    const Graph before = *current;
    const Graph after = graph;
    const std::size_t operation_count =
        m_impl->record_operations ? Impl::GraphReplacementOperationCount(before, after, m_impl->AvailableOperations())
                                  : 0;
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) return budget;
    m_impl->replacement_baselines.try_emplace(graph_id, before);
    auto result = m_impl->staged_document.ReplaceGraph(std::move(graph));
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchGraph(graph_id, SemanticDomain::Topology | SemanticDomain::Layout | SemanticDomain::Value);
        m_impl->journal.replaced_graphs.insert(graph_id);
        m_impl->RecordGraphReplaced(before, after);
    }
    return result;
}

Result<void> GraphTransaction::AddNode(const GraphId graph, NodeInstance node,
                                       const std::span<const PinInstance> pins) {
    const auto* target = Document().FindGraph(graph);
    if (target != nullptr && m_impl->enforce_protection && m_impl->GraphReadOnly(graph)) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph is read-only"));
    }
    const NodeId node_id = node.id;
    const std::size_t operation_count = 1 + pins.size();
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) return budget;
    auto result = m_impl->staged_document.AddNode(graph, std::move(node), pins);
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node_id, SemanticDomain::Topology | SemanticDomain::Layout);
        for (const auto& pin : pins) {
            m_impl->TouchPin(graph, pin.id, SemanticDomain::Topology | SemanticDomain::Layout);
        }
        const auto* added = Document().FindGraph(graph);
        m_impl->Record({OperationKind::AddNode, OperationAction::Set,
                        NodeOperation{graph, node_id, added->nodes.SharedAt(node_id), {}}});
        for (const auto& pin : pins) {
            m_impl->Record(
                {OperationKind::AddPin, OperationAction::Set,
                 PinOperation{graph, pin.node, pin.id, PinOperation::NoIndex, added->pins.SharedAt(pin.id)}});
        }
    }
    return result;
}

Result<RemovedNode> GraphTransaction::RemoveNode(const GraphId graph, const NodeId node) {
    const auto* target = Document().FindGraph(graph);
    const auto* current = Document().FindNode(graph, node);
    if (target != nullptr && current != nullptr && m_impl->enforce_protection) {
        if (m_impl->GraphReadOnly(graph) || m_impl->NodeReadOnly(graph, node)) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
        }
        for (const PinId pin : current->pins) {
            const auto* value = Document().FindPin(graph, pin);
            if (value != nullptr && m_impl->PinReadOnly(graph, pin)) {
                return std::unexpected(MakeError(ErrorCode::ReadOnly, "Node contains a read-only pin"));
            }
        }
        std::unordered_set<LinkId, IdHash> checked_links;
        for (const PinId pin : current->pins) {
            for (const LinkId link : Document().IncidentLinks(pin)) {
                if (!checked_links.insert(link).second) continue;
                if (auto allowed = m_impl->CheckLinkRemoval(graph, link); !allowed) {
                    return std::unexpected(std::move(allowed.error()));
                }
            }
        }
    }
    const std::optional<Graph> before = target != nullptr ? std::optional<Graph>{*target} : std::nullopt;
    std::unordered_set<LinkId, IdHash> incident_links;
    if (current != nullptr) {
        for (const PinId pin : current->pins) {
            incident_links.insert(Document().IncidentLinks(pin).begin(), Document().IncidentLinks(pin).end());
        }
    }
    const std::size_t operation_count = 1 + (current != nullptr ? current->pins.size() : 0) + incident_links.size();
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    auto result = m_impl->staged_document.RemoveNode(graph, node);
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node, SemanticDomain::Topology | SemanticDomain::Layout);
        for (const auto& pin : result->pins) {
            m_impl->TouchPin(graph, pin.id, SemanticDomain::Topology | SemanticDomain::Layout);
        }
        for (const auto& link : result->links) {
            m_impl->TouchLink(graph, link.id, SemanticDomain::Topology);
        }
        for (const auto& link : result->links) {
            m_impl->Record({OperationKind::Connect, OperationAction::Erase, LinkOperation{graph, link}});
        }
        for (const auto& pin : result->pins) {
            m_impl->Record({OperationKind::RemovePin, OperationAction::Erase,
                            PinOperation{graph, pin.node, pin.id, PinOperation::NoIndex,
                                         before ? before->pins.SharedAt(pin.id) : nullptr}});
        }
        m_impl->Record({OperationKind::DeleteElements, OperationAction::Erase,
                        NodeOperation{graph, node, before ? before->nodes.SharedAt(node) : nullptr, {}}});
    }
    return result;
}

Result<void> GraphTransaction::RestoreNode(const GraphId graph, RemovedNode removed) {
    if (m_impl->enforce_protection) {
        return std::unexpected(
            MakeError(ErrorCode::CommandFailed, "Node snapshots can only be restored while undoing history"));
    }
    const NodeId node = removed.node.id;
    std::vector<PinId> pins;
    pins.reserve(removed.pins.size());
    for (const auto& pin : removed.pins) pins.push_back(pin.id);
    std::vector<LinkId> links;
    links.reserve(removed.links.size());
    for (const auto& link : removed.links) links.push_back(link.id);
    const std::size_t operation_count = 1 + removed.pins.size() + removed.links.size();
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) return budget;
    auto result = m_impl->staged_document.RestoreNode(graph, std::move(removed));
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node, SemanticDomain::Topology | SemanticDomain::Layout);
        for (const PinId pin : pins) {
            m_impl->TouchPin(graph, pin, SemanticDomain::Topology | SemanticDomain::Layout);
        }
        for (const LinkId link : links) {
            m_impl->TouchLink(graph, link, SemanticDomain::Topology);
        }
        const auto* restored = Document().FindGraph(graph);
        m_impl->Record({OperationKind::AddNode, OperationAction::Set,
                        NodeOperation{graph, node, restored->nodes.SharedAt(node), {}}});
        for (const PinId pin : pins) {
            const auto handle = restored->pins.SharedAt(pin);
            m_impl->Record({OperationKind::AddPin, OperationAction::Set,
                            PinOperation{graph, handle ? handle->node : NodeId{}, pin, PinOperation::NoIndex, handle}});
        }
        for (const LinkId link : links) {
            const auto handle = restored->links.SharedAt(link);
            if (handle) {
                m_impl->Record({OperationKind::Connect, OperationAction::Set, LinkOperation{graph, *handle}});
            }
        }
    }
    return result;
}

Result<void> GraphTransaction::AddDynamicPin(const GraphId graph, PinInstance pin, const std::size_t index) {
    const auto* target = Document().FindGraph(graph);
    const auto* node = Document().FindNode(graph, pin.node);
    if (m_impl->enforce_protection && target != nullptr && node != nullptr &&
        (m_impl->GraphReadOnly(graph) || m_impl->NodeReadOnly(graph, node->id))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
    }
    const NodeId node_id = pin.node;
    const PinId pin_id = pin.id;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.AddDynamicPin(graph, std::move(pin), index);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        const auto* current = Document().FindNode(graph, node_id);
        if (current != nullptr) {
            m_impl->TouchNode(graph, current->id, SemanticDomain::Layout);
            m_impl->TouchPin(graph, pin_id, SemanticDomain::Topology | SemanticDomain::Layout);
        }
        const auto handle = Document().FindGraph(graph)->pins.SharedAt(pin_id);
        m_impl->Record(
            {OperationKind::AddDynamicPin, OperationAction::Set, PinOperation{graph, node_id, pin_id, index, handle}});
    }
    return result;
}

Result<RemovedPin> GraphTransaction::RemoveDynamicPin(const GraphId graph, const PinId pin) {
    const auto* target = Document().FindGraph(graph);
    const auto* current = Document().FindPin(graph, pin);
    const auto* node = current != nullptr ? Document().FindNode(graph, current->node) : nullptr;
    if (m_impl->enforce_protection && target != nullptr && current != nullptr) {
        if (m_impl->GraphReadOnly(graph) || m_impl->PinReadOnly(graph, pin) ||
            (node != nullptr && m_impl->NodeReadOnly(graph, node->id))) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph, node, or pin is read-only"));
        }
        for (const LinkId link_id : Document().IncidentLinks(pin)) {
            if (auto allowed = m_impl->CheckLinkRemoval(graph, link_id); !allowed) {
                return std::unexpected(std::move(allowed.error()));
            }
        }
    }
    const std::optional<Graph> before = target != nullptr ? std::optional<Graph>{*target} : std::nullopt;
    const std::size_t operation_count = 1 + Document().IncidentLinks(pin).size();
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    auto result = m_impl->staged_document.RemoveDynamicPin(graph, pin);
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchPin(graph, pin, SemanticDomain::Topology | SemanticDomain::Layout);
        m_impl->TouchNode(graph, result->pin.node, SemanticDomain::Layout);
        for (const auto& link : result->links) m_impl->TouchLink(graph, link.id, SemanticDomain::Topology);
        for (const auto& link : result->links) {
            m_impl->Record({OperationKind::Connect, OperationAction::Erase, LinkOperation{graph, link}});
        }
        m_impl->Record(
            {OperationKind::RemoveDynamicPin, OperationAction::Erase,
             PinOperation{graph, result->pin.node, pin, result->index, before ? before->pins.SharedAt(pin) : nullptr}});
    }
    return result;
}

Result<void> GraphTransaction::RestoreDynamicPin(const GraphId graph, RemovedPin removed) {
    if (m_impl->enforce_protection) {
        return std::unexpected(
            MakeError(ErrorCode::CommandFailed, "Pin snapshots can only be restored while undoing history"));
    }
    const PinId pin_id = removed.pin.id;
    const NodeId node_id = removed.pin.node;
    const std::size_t index = removed.index;
    std::vector<LinkId> link_ids;
    link_ids.reserve(removed.links.size());
    for (const auto& link : removed.links) link_ids.push_back(link.id);
    const std::size_t operation_count = 1 + removed.links.size();
    if (auto budget = m_impl->ConsumeOperations(operation_count); !budget) return budget;
    auto result = m_impl->staged_document.RestoreDynamicPin(graph, std::move(removed));
    if (!result) m_impl->ReleaseOperations(operation_count);
    if (result) {
        m_impl->model_changed = true;
        const auto* restored = Document().FindPin(graph, pin_id);
        if (restored != nullptr) {
            m_impl->TouchPin(graph, restored->id, SemanticDomain::Topology | SemanticDomain::Layout);
            m_impl->TouchNode(graph, restored->node, SemanticDomain::Layout);
        }
        for (const LinkId link : link_ids) m_impl->TouchLink(graph, link, SemanticDomain::Topology);
        const auto* graph_value = Document().FindGraph(graph);
        m_impl->Record({OperationKind::AddDynamicPin, OperationAction::Set,
                        PinOperation{graph, node_id, pin_id, index, graph_value->pins.SharedAt(pin_id)}});
        for (const LinkId link : link_ids) {
            const auto handle = graph_value->links.SharedAt(link);
            if (handle) {
                m_impl->Record({OperationKind::Connect, OperationAction::Set, LinkOperation{graph, *handle}});
            }
        }
    }
    return result;
}

Result<void> GraphTransaction::UpdateDynamicPin(const GraphId graph, PinInstance pin) {
    const auto* current = Document().FindPin(graph, pin.id);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Pin does not exist"));
    }
    if (*current == pin) {
        return {};
    }
    const auto* target = Document().FindGraph(graph);
    const auto* node = Document().FindNode(graph, current->node);
    if (m_impl->enforce_protection && target != nullptr &&
        (m_impl->GraphReadOnly(graph) || m_impl->PinReadOnly(graph, current->id) ||
         (node != nullptr && m_impl->NodeReadOnly(graph, node->id)))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph, node, or pin is read-only"));
    }
    if (m_impl->enforce_protection && current->read_only != pin.read_only) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Pin protection must be changed explicitly"));
    }
    const PinId pin_id = pin.id;
    const NodeId node_id = pin.node;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.UpdateDynamicPin(graph, std::move(pin));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchPin(graph, pin_id, SemanticDomain::Topology | SemanticDomain::Layout);
        m_impl->Record({OperationKind::UpdateDynamicPin, OperationAction::Set,
                        PinOperation{graph, node_id, pin_id, PinOperation::NoIndex,
                                     Document().FindGraph(graph)->pins.SharedAt(pin_id)}});
    }
    return result;
}

Result<void> GraphTransaction::ReorderDynamicPins(const GraphId graph, const NodeId node, std::vector<PinId> order) {
    const auto* current = Document().FindNode(graph, node);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    if (current->pins == order) {
        return {};
    }
    const auto* target = Document().FindGraph(graph);
    if (m_impl->enforce_protection && target != nullptr &&
        (m_impl->GraphReadOnly(graph) || m_impl->NodeReadOnly(graph, node))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
    }
    if (m_impl->enforce_protection &&
        std::ranges::any_of(current->pins, [&](const PinId pin) { return m_impl->PinReadOnly(graph, pin); })) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Read-only pins cannot be reordered"));
    }
    std::shared_ptr<const std::vector<PinId>> value;
    if (m_impl->record_operations) value = std::make_shared<const std::vector<PinId>>(order);
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.ReorderDynamicPins(graph, node, std::move(order));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node, SemanticDomain::Layout);
        m_impl->Record({OperationKind::ReorderDynamicPins, OperationAction::Set,
                        ReorderPinsOperation{graph, node, std::move(value)}});
    }
    return result;
}

Result<void> GraphTransaction::SetDescriptorPins(const GraphId graph, const NodeId node, const std::span<const PinInstance> pins) {
    const auto* target = Document().FindGraph(graph);
    const auto* current = Document().FindNode(graph, node);
    if (current == nullptr || target == nullptr) {
        return std::unexpected(MakeError(current == nullptr ? ErrorCode::NodeNotFound : ErrorCode::GraphNotFound,
                                         current == nullptr ? "Node does not exist" : "Graph does not exist"));
    }
    if (m_impl->enforce_protection && (m_impl->GraphReadOnly(graph) || m_impl->NodeReadOnly(graph, node))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
    }

    std::unordered_set<std::string> projected_keys;
    for (const PinInstance& pin : pins)
        projected_keys.insert(pin.key);

    std::vector<PinId> dynamic_pins;
    std::unordered_map<PinId, PinInstance, IdHash> before_by_id;
    std::unordered_map<PinId, std::shared_ptr<const PinInstance>, IdHash> before_handles;
    for (const PinId id : current->pins) {
        const auto* pin = Document().FindPin(graph, id);
        if (pin == nullptr) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node references a missing pin"));
        }
        if (pin->storage == PinStorage::Dynamic) {
            dynamic_pins.push_back(id);
            continue;
        }
        before_by_id.emplace(id, *pin);
        before_handles.emplace(id, target->pins.SharedAt(id));
        if (projected_keys.contains(pin->key))
            continue;
        if (m_impl->enforce_protection && m_impl->PinReadOnly(graph, id)) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Removed descriptor pin is read-only"));
        }
        for (const LinkId link : Document().IncidentLinks(id)) {
            if (m_impl->enforce_protection) {
                if (auto removable = m_impl->CheckLinkRemoval(graph, link); !removable)
                    return removable;
            }
        }
        if (Document().IntergraphLinkForPin(id)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Descriptor pin intergraph links must be removed before reprojection"));
        }
    }

    std::vector<PinId> projected_order;
    projected_order.reserve(pins.size() + dynamic_pins.size());
    std::unordered_map<PinId, PinInstance, IdHash> after_by_id;
    for (const PinInstance& pin : pins) {
        after_by_id.emplace(pin.id, pin);
        if (const auto before = before_by_id.find(pin.id);
            m_impl->enforce_protection && before != before_by_id.end() &&
            ((before->second.read_only && before->second != pin) ||
             before->second.read_only != pin.read_only)) {
            return std::unexpected(MakeError(
                ErrorCode::ReadOnly,
                "Read-only descriptor pin cannot be changed by schema resolution"));
        }
    }
    std::size_t descriptor_index = 0;
    for (const PinId id : current->pins) {
        if (before_by_id.contains(id)) {
            if (descriptor_index < pins.size())
                projected_order.push_back(pins[descriptor_index++].id);
        } else {
            projected_order.push_back(id);
        }
    }
    while (descriptor_index < pins.size())
        projected_order.push_back(pins[descriptor_index++].id);

    std::size_t pin_changes = 0;
    for (const auto& [id, pin] : before_by_id) {
        const auto after = after_by_id.find(id);
        if (after == after_by_id.end() || after->second != pin)
            ++pin_changes;
    }
    for (const auto& [id, pin] : after_by_id) {
        (void)pin;
        if (!before_by_id.contains(id))
            ++pin_changes;
    }
    if (pin_changes == 0 && current->pins == projected_order)
        return {};

    std::unordered_map<PinId, SemanticDomain, IdHash> pin_domains;
    SemanticDomain node_domains = current->pins == projected_order
                                      ? SemanticDomain::None
                                      : SemanticDomain::Layout;
    for (const auto& [id, before] : before_by_id) {
        const auto after = after_by_id.find(id);
        SemanticDomain domains = SemanticDomain::None;
        if (after == after_by_id.end() || before.type != after->second.type ||
            before.direction != after->second.direction || before.kind != after->second.kind ||
            before.cardinality != after->second.cardinality) {
            domains |= SemanticDomain::Topology | SemanticDomain::Layout;
        } else {
            if (before.label != after->second.label)
                domains |= SemanticDomain::Layout;
            if (before.read_only != after->second.read_only)
                domains |= SemanticDomain::Value;
        }
        if (domains != SemanticDomain::None) {
            pin_domains.emplace(id, domains);
            node_domains |= domains;
        }
    }
    for (const auto& [id, after] : after_by_id) {
        (void)after;
        if (!before_by_id.contains(id)) {
            const SemanticDomain domains = SemanticDomain::Topology | SemanticDomain::Layout;
            pin_domains.emplace(id, domains);
            node_domains |= domains;
        }
    }

    if (auto budget = m_impl->ConsumeOperations(pin_changes + 1); !budget)
        return budget;
    auto result = m_impl->staged_document.SetDescriptorPins(graph, node, pins);
    if (!result) {
        m_impl->ReleaseOperations(pin_changes + 1);
        return result;
    }

    m_impl->model_changed = true;
    m_impl->TouchNode(graph, node, node_domains);
    const auto* after_graph = Document().FindGraph(graph);
    m_impl->Record({OperationKind::UpdateNode, OperationAction::Set, NodeOperation{graph, node, after_graph->nodes.SharedAt(node), {}}});
    for (const auto& [id, pin] : before_by_id) {
        const auto after = after_by_id.find(id);
        if (after != after_by_id.end() && after->second == pin)
            continue;
        m_impl->TouchPin(graph, id, pin_domains.at(id));
        if (after == after_by_id.end()) {
            m_impl->Record({OperationKind::RemovePin, OperationAction::Erase,
                            PinOperation{graph, node, id, PinOperation::NoIndex,
                                         before_handles.at(id)}});
        } else {
            m_impl->Record(
                {OperationKind::UpdatePin, OperationAction::Set, PinOperation{graph, node, id, PinOperation::NoIndex, after_graph->pins.SharedAt(id)}});
        }
    }
    for (const auto& [id, pin] : after_by_id) {
        (void)pin;
        if (before_by_id.contains(id))
            continue;
        m_impl->TouchPin(graph, id, pin_domains.at(id));
        m_impl->Record({OperationKind::AddPin, OperationAction::Set, PinOperation{graph, node, id, PinOperation::NoIndex, after_graph->pins.SharedAt(id)}});
    }
    return {};
}

Result<void> GraphTransaction::AddLink(const GraphId graph, Link link, std::optional<Link> replacing) {
    if (replacing) {
        auto authorization = AuthorizeConnection(ConnectionRequest{graph, link.output, link.input, replacing->id});
        if (!authorization) return std::unexpected(std::move(authorization.error()));
        if (authorization->status != ConnectionResult::Status::Allowed) {
            return std::unexpected(MakeError(ErrorCode::IncompatiblePins, authorization->reason));
        }
        const auto* current = Document().FindLink(graph, replacing->id);
        if (current == nullptr || *current != *replacing || link.id != replacing->id) {
            return std::unexpected(MakeError(ErrorCode::CommandFailed, "Reconnected link changed before commit"));
        }
        if (auto removed = RemovePlannedLink(graph, replacing->id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
        return AddPlannedLink(graph, std::move(link));
    }
    if (m_impl->enforce_protection) {
        const auto* output = Document().FindPin(graph, link.output);
        const auto* input = Document().FindPin(graph, link.input);
        if (m_impl->GraphReadOnly(graph) ||
            (output != nullptr &&
             (m_impl->PinReadOnly(graph, output->id) || m_impl->NodeReadOnly(graph, output->node))) ||
            (input != nullptr && (m_impl->PinReadOnly(graph, input->id) || m_impl->NodeReadOnly(graph, input->node)))) {
            return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or connection endpoint is read-only"));
        }
    }
    if (m_impl->enforce_protection) {
        const auto compatibility = Detail::ValidateConnection(
            Document(), Presentation(), ConnectionRequest{graph, link.output, link.input, {}}, m_impl->registry);
        if (compatibility.status != ConnectionResult::Status::Allowed) {
            return std::unexpected(MakeError(compatibility.error, compatibility.reason));
        }
    }
    const Link value = link;
    const LinkId link_id = link.id;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.AddLink(graph, std::move(link));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchLink(graph, link_id, SemanticDomain::Topology);
        m_impl->Record({OperationKind::Connect, OperationAction::Set, LinkOperation{graph, value}});
    }
    return result;
}

Result<Link> GraphTransaction::RemoveLink(const GraphId graph, const LinkId link) {
    if (m_impl->enforce_protection) {
        if (auto allowed = m_impl->CheckLinkRemoval(graph, link); !allowed) {
            return std::unexpected(std::move(allowed.error()));
        }
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    auto result = m_impl->staged_document.RemoveLink(graph, link);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchLink(graph, link, SemanticDomain::Topology);
        m_impl->Record({OperationKind::Connect, OperationAction::Erase, LinkOperation{graph, *result}});
    }
    return result;
}

Result<void> GraphTransaction::SetNodeProperty(const GraphId graph, const NodeId node, std::string key,
                                               std::optional<PropertyValue> value) {
    const auto* current = Document().FindNode(graph, node);
    const PropertyImpact impact =
        current != nullptr ? ResolvePropertyImpact(current->type, key) : PropertyImpact::Topology;
    if (current == nullptr || !m_impl->registry.PinSchemaDependsOn(current->type, key)) {
        return SetNodePropertyWithImpact(graph, node, std::move(key), std::move(value), impact);
    }
    const auto descriptor = m_impl->registry.Find(current->type);
    if (!descriptor || current->type_version != descriptor->version) {
        return std::unexpected(MakeError(
            ErrorCode::RevisionConflict,
            "Node pin schema requires the exact registered descriptor version"));
    }

    PropertyBag properties = current->properties;
    if (value)
        properties.insert_or_assign(key, *value);
    else
        properties.erase(key);
    auto resolved = m_impl->registry.ResolvePinSchema(current->type, properties);
    if (!resolved)
        return std::unexpected(std::move(resolved.error()));
    auto plan = CommandDetail::PlanDescriptorPins(
        Document(), graph, node, *resolved, [&] { return AllocatePinId(); });
    if (!plan)
        return std::unexpected(std::move(plan.error()));
    for (const PinId pin : plan->connection_changed) {
        if (!Document().IncidentLinks(pin).empty() || Document().IntergraphLinkForPin(pin)) {
            return std::unexpected(MakeError(
                ErrorCode::IncompatiblePins,
                "Property change requires explicit disconnection of incompatible pin links"));
        }
    }
    if (auto property = SetNodePropertyWithImpact(graph, node, std::move(key),
                                                  std::move(value), impact);
        !property) {
        return property;
    }
    return SetDescriptorPins(graph, node, plan->after);
}

PropertyImpact GraphTransaction::ResolvePropertyImpact(const TypeId& type, const std::string_view key) const noexcept {
    return m_impl->registry.Find(type)
        ? m_impl->registry.ResolvePropertyImpact(type, key)
        : PropertyImpact::Topology;
}

Result<void> GraphTransaction::SetNodePropertyWithImpact(const GraphId graph, const NodeId node, std::string key,
                                                         std::optional<PropertyValue> value,
                                                         const PropertyImpact impact) {
    if (key.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property key cannot be empty"));
    }
    const auto* current = Document().FindNode(graph, node);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    const auto found = current->properties.find(key);
    const std::optional<PropertyValue> before =
        found == current->properties.end() ? std::nullopt : std::optional<PropertyValue>{found->second};
    if (before == value) {
        return {};
    }
    const auto* target = Document().FindGraph(graph);
    if (m_impl->enforce_protection && target != nullptr &&
        (m_impl->GraphReadOnly(graph) || m_impl->NodeReadOnly(graph, node))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
    }
    std::shared_ptr<const PropertyChange> change;
    if (m_impl->record_operations) {
        change = std::make_shared<const PropertyChange>(PropertyChange{
            .key = key,
            .current = value,
            .previous = before,
        });
    }
    const OperationAction action = value ? OperationAction::Set : OperationAction::Erase;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.SetNodeProperty(graph, node, std::move(key), std::move(value));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node, ImpactDomain(impact));
        m_impl->Record({OperationKind::SetNodeProperty, action, PropertyOperation{graph, node, std::move(change)}});
    }
    return result;
}

Result<void> GraphTransaction::SetNodeDisplayName(const GraphId graph, const NodeId node, std::string name) {
    const auto* current = Document().FindNode(graph, node);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    if (current->display_name == name) {
        return {};
    }
    const auto* target = Document().FindGraph(graph);
    if (m_impl->enforce_protection && target != nullptr &&
        (m_impl->GraphReadOnly(graph) || m_impl->NodeReadOnly(graph, node))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
    }
    std::shared_ptr<const std::string> value;
    if (m_impl->record_operations) value = std::make_shared<const std::string>(name);
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.SetNodeDisplayName(graph, node, std::move(name));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node, SemanticDomain::Value | SemanticDomain::Layout);
        m_impl->Record({OperationKind::SetNodeDisplayName, OperationAction::Set,
                        NodeTextOperation{graph, node, std::move(value)}});
    }
    return result;
}

Result<void> GraphTransaction::SetNodeSubgraph(const GraphId graph, const NodeId node,
                                               std::optional<SubgraphReference> subgraph) {
    const auto* current = Document().FindNode(graph, node);
    if (current == nullptr) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    if (current->subgraph == subgraph) {
        return {};
    }
    const auto* target = Document().FindGraph(graph);
    if (m_impl->enforce_protection && target != nullptr &&
        (m_impl->GraphReadOnly(graph) || m_impl->NodeReadOnly(graph, node))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Graph or node is read-only"));
    }
    std::shared_ptr<const SubgraphChange> change;
    if (m_impl->record_operations) {
        change = std::make_shared<const SubgraphChange>(SubgraphChange{
            .current = subgraph,
            .previous = current->subgraph,
        });
    }
    const OperationAction action = subgraph ? OperationAction::Set : OperationAction::Erase;
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.SetNodeSubgraph(graph, node, std::move(subgraph));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->TouchNode(graph, node, SemanticDomain::Topology | SemanticDomain::Layout);
        m_impl->Record({OperationKind::SetNodeSubgraph, action, NodeSubgraphOperation{graph, node, std::move(change)}});
    }
    return result;
}

Result<void> GraphTransaction::AddIntergraphLink(IntergraphLink link) {
    const IntergraphLink value = link;
    const IntergraphLinkId link_id = link.id;
    if (m_impl->enforce_protection &&
        (m_impl->GraphReadOnly(link.source.graph) || m_impl->GraphReadOnly(link.destination.graph) ||
         m_impl->NodeReadOnly(link.source.graph, link.source.node) ||
         m_impl->NodeReadOnly(link.destination.graph, link.destination.node) ||
         m_impl->PinReadOnly(link.source.graph, link.source.pin) ||
         m_impl->PinReadOnly(link.destination.graph, link.destination.pin))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Intergraph link endpoint is read-only"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) return budget;
    auto result = m_impl->staged_document.AddIntergraphLink(std::move(link));
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->journal.intergraph_links[link_id] |= SemanticDomain::Topology;
        m_impl->Record({OperationKind::ConnectIntergraph, OperationAction::Set,
                        IntergraphOperation{std::make_shared<const IntergraphLink>(value)}});
    }
    return result;
}

Result<IntergraphLink> GraphTransaction::RemoveIntergraphLink(const IntergraphLinkId link) {
    const auto* current = Document().FindIntergraphLink(link);
    if (current != nullptr && m_impl->enforce_protection &&
        (current->read_only || m_impl->GraphReadOnly(current->source.graph) ||
         m_impl->GraphReadOnly(current->destination.graph) ||
         m_impl->NodeReadOnly(current->source.graph, current->source.node) ||
         m_impl->NodeReadOnly(current->destination.graph, current->destination.node))) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Intergraph link or endpoint is read-only"));
    }
    if (auto budget = m_impl->ConsumeOperations(); !budget) {
        return std::unexpected(std::move(budget.error()));
    }
    auto result = m_impl->staged_document.RemoveIntergraphLink(link);
    if (!result) m_impl->ReleaseOperations();
    if (result) {
        m_impl->model_changed = true;
        m_impl->journal.intergraph_links[link] |= SemanticDomain::Topology;
        m_impl->Record({OperationKind::DisconnectIntergraph, OperationAction::Erase,
                        IntergraphOperation{std::make_shared<const IntergraphLink>(*result)}});
    }
    return result;
}

#undef Record

} // namespace Uni::GUI::Nodes
