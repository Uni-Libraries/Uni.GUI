#include "nodes/commands/transaction_internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;
using namespace TransactionDetail;

Result<CommandResult> GraphTransaction::Commit() {
    const FinalizedJournal finalized = m_impl->FinalizeJournal();
    m_impl->model_changed = finalized.model_changed;
    m_impl->presentation_changed = finalized.presentation_changed;
    const SemanticRevisionSet document_changes{
        .serial = finalized.model_changed ? 1U : 0U,
        .topology = HasDomain(finalized.document_domains, SemanticDomain::Topology) ? 1U : 0U,
        .value = HasDomain(finalized.document_domains, SemanticDomain::Value) ? 1U : 0U,
        .layout = HasDomain(finalized.document_domains, SemanticDomain::Layout) ? 1U : 0U,
    };
    std::unordered_map<GraphId, SemanticRevisionSet, IdHash> graph_changes;
    graph_changes.reserve(finalized.graph_domains.size());
    for (const auto& [graph, domains] : finalized.graph_domains) {
        graph_changes.emplace(graph, SemanticRevisionSet{
                                         .serial = finalized.model_changed ? 1U : 0U,
                                         .topology = HasDomain(domains, SemanticDomain::Topology) ? 1U : 0U,
                                         .value = HasDomain(domains, SemanticDomain::Value) ? 1U : 0U,
                                         .layout = HasDomain(domains, SemanticDomain::Layout) ? 1U : 0U,
                                     });
    }
    for (const NodeId node : finalized.node_presentations) {
        const auto* state = Presentation().FindNode(node);
        if (state != nullptr && (!Document().FindNodeGraph(node) || !ValidNodePresentation(*state))) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Transaction would leave invalid or orphaned node presentation"));
        }
    }
    for (const LinkId link : finalized.link_presentations) {
        const auto* state = Presentation().FindLink(link);
        if (state != nullptr && !Document().FindLinkGraph(link)) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Transaction would leave orphaned link presentation"));
        }
        const auto impact = finalized.link_presentation_impacts.find(link);
        if (impact == finalized.link_presentation_impacts.end()) continue;
        const auto& touch = impact->second;
        if (state != nullptr && touch.full_route_validation && !ValidLinkPresentation(*state)) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Transaction would leave invalid link presentation"));
        }
        const bool validate_all_owners =
            touch.full_route_validation && HasImpact(touch.impact, LinkPresentationImpact::Lifecycle);
        if (state != nullptr && validate_all_owners) {
            for (const auto& point : state->Route()) {
                if (Presentation().RoutePointOwner(point.id) != link) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Transaction route-point ownership index is inconsistent"));
                }
            }
        } else if (state == nullptr && validate_all_owners) {
            if (const auto* before = m_impl->baseline_presentation.FindLink(link)) {
                for (const RoutePoint& point : before->Route()) {
                    if (Presentation().RoutePointOwner(point.id) == link) {
                        return std::unexpected(MakeError(
                            ErrorCode::InvalidGraph,
                            "Removed route point remains in the ownership index"));
                    }
                }
            }
        } else if (HasImpact(touch.impact, LinkPresentationImpact::Route)) {
            for (const RoutePointId point_id : touch.route_points) {
                const auto* point = state != nullptr ? state->Route().Find(point_id) : nullptr;
                if (point != nullptr) {
                    if (!point->id || !Finite(point->position) ||
                        Presentation().RoutePointOwner(point_id) != link) {
                        return std::unexpected(MakeError(
                            ErrorCode::InvalidGraph,
                            "Transaction route-point delta is invalid"));
                    }
                } else if (Presentation().RoutePointOwner(point_id) == link) {
                    return std::unexpected(MakeError(
                        ErrorCode::InvalidGraph,
                        "Removed route point remains in the ownership index"));
                }
            }
        }
    }
    for (const GroupId group : finalized.groups) {
        const auto* state = Presentation().FindGroup(group);
        if (state != nullptr && (state->id != group || !ValidGroupHeader(*state))) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Transaction would leave an invalid group presentation"));
        }
    }

    if (HasDomain(finalized.document_domains, SemanticDomain::Topology)) {
        std::unordered_set<GraphId, IdHash> ownership_targets;
        std::unordered_set<GraphId, IdHash> boundary_graphs;
        std::unordered_set<IntergraphLinkId, IdHash> intergraph_links;
        const auto collect_node_relations = [&](const GraphDocument& document, const GraphId graph, const NodeId node) {
            const auto* value = document.FindNode(graph, node);
            if (value == nullptr) return;
            if (const auto target = Detail::LocalSubgraph(value->subgraph)) ownership_targets.insert(*target);
            if (value->role == NodeRole::BoundaryInput || value->role == NodeRole::BoundaryOutput) {
                boundary_graphs.insert(graph);
            }
            for (const PinId pin : value->pins) {
                if (const IntergraphLinkId link = document.IntergraphLinkForPin(pin)) {
                    intergraph_links.insert(link);
                }
            }
        };
        for (const auto& entity : finalized.nodes) {
            collect_node_relations(m_impl->baseline_document, entity.graph, entity.id);
            collect_node_relations(m_impl->staged_document, entity.graph, entity.id);
            if (auto valid = m_impl->staged_document.ValidateNodeRelations(entity.graph, entity.id); !valid) {
                return std::unexpected(std::move(valid.error()));
            }
            if (Document().FindNode(entity.graph, entity.id) != nullptr) continue;
            if (Presentation().FindNode(entity.id) != nullptr || !Presentation().GroupsForNode(entity.id).empty()) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                 "Transaction would leave presentation state for a missing node"));
            }
        }
        for (const auto& entity : finalized.pins) {
            const auto before_owner = m_impl->baseline_document.FindPinOwner(entity.id);
            const auto after_owner = m_impl->staged_document.FindPinOwner(entity.id);
            if (before_owner)
                collect_node_relations(m_impl->baseline_document, before_owner->graph, before_owner->node);
            if (after_owner) {
                collect_node_relations(m_impl->staged_document, after_owner->graph, after_owner->node);
                if (auto valid = m_impl->staged_document.ValidateNodeRelations(after_owner->graph, after_owner->node);
                    !valid) {
                    return std::unexpected(std::move(valid.error()));
                }
            }
            if (const IntergraphLinkId link = m_impl->baseline_document.IntergraphLinkForPin(entity.id)) {
                intergraph_links.insert(link);
            }
            if (const IntergraphLinkId link = m_impl->staged_document.IntergraphLinkForPin(entity.id)) {
                intergraph_links.insert(link);
            }
        }
        for (const auto& entity : finalized.links) {
            if (Document().FindLink(entity.graph, entity.id) == nullptr &&
                Presentation().FindLink(entity.id) != nullptr) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                 "Transaction would leave presentation state for a missing link"));
            }
        }
        for (const GraphId graph : finalized.graphs) {
            const auto* after = Document().FindGraph(graph);
            if (after != nullptr && m_impl->baseline_document.FindGraph(graph) == nullptr) {
                const std::size_t owners = Document().OwnedSubgraphCallerCount(graph);
                if ((after->lifetime == GraphLifetime::Owned && owners != 1) ||
                    (after->lifetime == GraphLifetime::Reusable && owners != 0)) {
                    return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                     "Added graph lifetime does not match its final owner count"));
                }
                continue;
            }
            if (after != nullptr) continue;
            const auto* before = m_impl->baseline_document.FindGraph(graph);
            if (before != nullptr) {
                for (const auto& [node, value] : before->nodes) {
                    if (const auto target = Detail::LocalSubgraph(value.subgraph)) {
                        ownership_targets.insert(*target);
                    }
                    if (Presentation().FindNode(node) != nullptr || !Presentation().GroupsForNode(node).empty()) {
                        return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                         "Transaction would leave presentation "
                                                         "state for a removed graph node"));
                    }
                }
                for (const auto& [link, value] : before->links) {
                    (void)value;
                    if (Presentation().FindLink(link) != nullptr) {
                        return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                         "Transaction would leave presentation "
                                                         "state for a removed graph link"));
                    }
                }
            }
            if (!Presentation().GroupsForGraph(graph).empty()) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Transaction would leave groups for a removed graph"));
            }
        }
        if (finalized.has_replaced_graph) {
            for (const GraphId graph : m_impl->journal.replaced_graphs) {
                const auto baseline = m_impl->replacement_baselines.find(graph);
                const auto* before = baseline != m_impl->replacement_baselines.end() ? &baseline->second : nullptr;
                const auto* after = m_impl->staged_document.FindGraph(graph);
                if (before != nullptr && after != nullptr) {
                    before->nodes.ForEachDifference(after->nodes, [&](const NodeId node, const NodeInstance* old_value,
                                                                      const NodeInstance* new_value) {
                        (void)old_value;
                        (void)new_value;
                        collect_node_relations(m_impl->baseline_document, graph, node);
                        collect_node_relations(m_impl->staged_document, graph, node);
                    });
                    if (auto valid = m_impl->staged_document.ValidateReplacement(*before); !valid) {
                        return std::unexpected(std::move(valid.error()));
                    }
                    boundary_graphs.insert(graph);
                }
            }
        }
        for (const GraphId target : ownership_targets) {
            if (auto valid = m_impl->staged_document.ValidateTargetOwnership(target); !valid) {
                return std::unexpected(std::move(valid.error()));
            }
        }
        for (const GraphId graph : boundary_graphs) {
            if (auto valid = m_impl->staged_document.ValidateBoundaryRelations(graph); !valid) {
                return std::unexpected(std::move(valid.error()));
            }
        }
        intergraph_links.insert(finalized.intergraph_links.begin(), finalized.intergraph_links.end());
        for (const IntergraphLinkId link : intergraph_links) {
            if (auto valid = m_impl->staged_document.ValidateIntergraphLinkStructure(link); !valid) {
                return std::unexpected(std::move(valid.error()));
            }
        }
    }
    if (!m_impl->document->CanCommit(m_impl->document_identity, m_impl->expected.model,
                                     m_impl->document_allocation_epoch, document_changes, graph_changes) ||
        !m_impl->presentation->CanCommit(m_impl->presentation_identity, m_impl->expected.presentation,
                                         m_impl->presentation_allocation_epoch, m_impl->presentation_changed,
                                         finalized.presentation_geometry_changed)) {
        return std::unexpected(MakeError(ErrorCode::RevisionConflict,
                                         "Document or presentation changed while the transaction was "
                                         "active, or its revision is exhausted"));
    }
    m_impl->document->CommitFrom(std::move(m_impl->staged_document), document_changes, graph_changes);
    m_impl->presentation->CommitFrom(std::move(m_impl->staged_presentation), m_impl->presentation_changed,
                                     finalized.presentation_geometry_changed);
    Detail::RecordJournalEntries(finalized.entries);
    Detail::RecordIncrementalValidation(finalized.node_presentations.size() + finalized.link_presentations.size() +
                                        finalized.groups.size() + finalized.nodes.size() + finalized.links.size() +
                                        finalized.pins.size() + finalized.graphs.size() +
                                        finalized.intergraph_links.size());
    return CommandResult{
        .model_changed = m_impl->model_changed,
        .presentation_changed = m_impl->presentation_changed,
        .revisions = {m_impl->document->ModelRevision(), m_impl->presentation->PresentationRevision()},
    };
}

} // namespace Uni::GUI::Nodes
