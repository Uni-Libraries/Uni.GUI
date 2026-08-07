#include "nodes/commands/transaction_internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;
using namespace TransactionDetail;

#define Record(...) StoreOperationLazy([&]() -> OperationIntent { return OperationIntent __VA_ARGS__; })

Result<void> GraphTransaction::Impl::ConsumeOperations(const std::size_t count) {
    if (!record_operations || count == 0) return {};
    if (operation_limit_exceeded || count > max_operations - std::min(max_operations, reserved_operations)) {
        operation_limit_exceeded = true;
        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded,
                                         "Graph policy mutation batch exceeds the configured operation limit"));
    }
    reserved_operations += count;
    return {};
}

void GraphTransaction::Impl::ReleaseOperations(const std::size_t count) noexcept {
    if (record_operations && !operation_limit_exceeded) {
        reserved_operations -= std::min(reserved_operations, count);
    }
}

std::size_t GraphTransaction::Impl::AvailableOperations() const noexcept {
    return record_operations ? max_operations - std::min(max_operations, reserved_operations) : 0;
}

std::shared_ptr<const GraphMetadata> GraphTransaction::Impl::Metadata(const Graph& graph) {
    return std::make_shared<const GraphMetadata>(GraphMetadata{
        .display_name = graph.display_name,
        .lifetime = graph.lifetime,
        .interface = graph.interface,
        .read_only = graph.read_only,
    });
}

void GraphTransaction::Impl::RecordGraphAdded(const Graph& graph) {
    if (!record_operations) return;
    operations.reserve(operations.size() + 1 + graph.nodes.size() + graph.pins.size() + graph.links.size());
    Record({OperationKind::AddGraph, OperationAction::Set, GraphOperation{graph.id, Metadata(graph)}});
    for (const auto& [node_id, node] : graph.nodes) {
        (void)node;
        Record({OperationKind::AddNode, OperationAction::Set,
                NodeOperation{graph.id, node_id, graph.nodes.SharedAt(node_id), {}}});
    }
    for (const auto& [pin_id, pin] : graph.pins) {
        Record({OperationKind::AddPin, OperationAction::Set,
                PinOperation{graph.id, pin.node, pin_id, PinOperation::NoIndex, graph.pins.SharedAt(pin_id)}});
    }
    for (const auto& [link_id, link] : graph.links) {
        (void)link_id;
        Record({OperationKind::Connect, OperationAction::Set, LinkOperation{graph.id, link}});
    }
}

void GraphTransaction::Impl::RecordGraphRemoved(const Graph& graph) {
    if (!record_operations) return;
    operations.reserve(operations.size() + 1 + graph.nodes.size() + graph.pins.size() + graph.links.size());
    for (const auto& [link_id, link] : graph.links) {
        (void)link_id;
        Record({OperationKind::Connect, OperationAction::Erase, LinkOperation{graph.id, link}});
    }
    for (const auto& [pin_id, pin] : graph.pins) {
        Record({OperationKind::RemovePin, OperationAction::Erase,
                PinOperation{graph.id, pin.node, pin_id, PinOperation::NoIndex, graph.pins.SharedAt(pin_id)}});
    }
    for (const auto& [node_id, node] : graph.nodes) {
        (void)node;
        Record({OperationKind::DeleteElements, OperationAction::Erase,
                NodeOperation{graph.id, node_id, graph.nodes.SharedAt(node_id), {}}});
    }
    Record({OperationKind::RemoveGraph, OperationAction::Erase, GraphOperation{graph.id, Metadata(graph)}});
}

void GraphTransaction::Impl::RecordGraphReplaced(const Graph& before, const Graph& after) {
    if (!record_operations) return;
    if (before.display_name != after.display_name || before.lifetime != after.lifetime) {
        Record({OperationKind::UpdateGraph, OperationAction::Set, GraphOperation{after.id, Metadata(after)}});
    }
    if (before.interface != after.interface) {
        Record({OperationKind::SetGraphInterface, OperationAction::Set,
                GraphInterfaceOperation{after.id, std::make_shared<const GraphInterface>(after.interface)}});
    }
    if (before.read_only != after.read_only) {
        Record({OperationKind::SetProtection, OperationAction::Set,
                ProtectionOperation{GraphProtectionTarget{after.id}, ProtectionKind::ReadOnly, after.read_only}});
    }
    before.nodes.ForEachDifference(after.nodes, [&](const NodeId node_id, const NodeInstance* node,
                                                    const NodeInstance* replacement) {
        if (replacement == nullptr) {
            Record({OperationKind::DeleteElements, OperationAction::Erase,
                    NodeOperation{before.id, node_id, before.nodes.SharedAt(node_id), {}}});
            return;
        }
        if (node == nullptr) {
            Record({OperationKind::AddNode, OperationAction::Set,
                    NodeOperation{after.id, node_id, after.nodes.SharedAt(node_id), {}}});
            return;
        }
        Record({OperationKind::UpdateNode, OperationAction::Set,
                NodeOperation{after.id, node_id, after.nodes.SharedAt(node_id), {}}});
        if (node->display_name != replacement->display_name) {
            Record(
                {OperationKind::SetNodeDisplayName, OperationAction::Set,
                 NodeTextOperation{after.id, node_id, std::make_shared<const std::string>(replacement->display_name)}});
        }
        if (node->subgraph != replacement->subgraph) {
            Record({OperationKind::SetNodeSubgraph,
                    replacement->subgraph ? OperationAction::Set : OperationAction::Erase,
                    NodeSubgraphOperation{after.id, node_id,
                                          std::make_shared<const SubgraphChange>(SubgraphChange{
                                              .current = replacement->subgraph,
                                              .previous = node->subgraph,
                                          })}});
        }
        if (node->read_only != replacement->read_only) {
            Record({OperationKind::SetProtection, OperationAction::Set,
                    ProtectionOperation{NodeProtectionTarget{after.id, node_id}, ProtectionKind::ReadOnly,
                                        replacement->read_only}});
        }
        for (const auto& [key, property] : node->properties) {
            const auto value = replacement->properties.find(key);
            if (value != replacement->properties.end() && value->second == property) continue;
            Record({OperationKind::SetNodeProperty,
                    value != replacement->properties.end() ? OperationAction::Set : OperationAction::Erase,
                    PropertyOperation{after.id, node_id,
                                      std::make_shared<const PropertyChange>(PropertyChange{
                                          .key = key,
                                          .current = value != replacement->properties.end()
                                                         ? std::optional<PropertyValue>{value->second}
                                                         : std::nullopt,
                                          .previous = property,
                                      })}});
        }
        for (const auto& [key, property] : replacement->properties) {
            if (node->properties.contains(key)) continue;
            Record({OperationKind::SetNodeProperty, OperationAction::Set,
                    PropertyOperation{after.id, node_id,
                                      std::make_shared<const PropertyChange>(PropertyChange{
                                          .key = key,
                                          .current = property,
                                      })}});
        }
    });
    before.pins.ForEachDifference(after.pins, [&](const PinId pin_id, const PinInstance* pin,
                                                  const PinInstance* replacement) {
        if (replacement == nullptr) {
            Record({OperationKind::RemovePin, OperationAction::Erase,
                    PinOperation{before.id, pin->node, pin_id, PinOperation::NoIndex, before.pins.SharedAt(pin_id)}});
        } else if (pin == nullptr) {
            Record({OperationKind::AddPin, OperationAction::Set,
                    PinOperation{after.id, replacement->node, pin_id, PinOperation::NoIndex,
                                 after.pins.SharedAt(pin_id)}});
        } else {
            Record({OperationKind::UpdatePin, OperationAction::Set,
                    PinOperation{after.id, replacement->node, pin_id, PinOperation::NoIndex,
                                 after.pins.SharedAt(pin_id)}});
        }
    });
    before.links.ForEachDifference(after.links, [&](const LinkId, const Link* link, const Link* replacement) {
        if (replacement == nullptr) {
            Record({OperationKind::Connect, OperationAction::Erase, LinkOperation{before.id, *link}});
        } else {
            Record({OperationKind::Connect, OperationAction::Set, LinkOperation{after.id, *replacement}});
        }
    });
}

std::size_t GraphTransaction::Impl::GraphReplacementOperationCount(const Graph& before, const Graph& after,
                                                                   const std::size_t limit) {
    std::size_t count = 0;
    const auto increment = [&](const std::size_t amount = 1) {
        if (amount > limit - std::min(limit, count)) {
            count = limit == std::numeric_limits<std::size_t>::max() ? limit : limit + 1;
            return false;
        }
        count += amount;
        return true;
    };
    if ((before.display_name != after.display_name || before.lifetime != after.lifetime) && !increment()) return count;
    if (before.interface != after.interface && !increment()) return count;
    if (before.read_only != after.read_only && !increment()) return count;
    if (!before.nodes.ForEachDifferenceWhile(
            after.nodes, [&](const NodeId, const NodeInstance* node, const NodeInstance* replacement) {
                if (!increment()) return false;
                if (node == nullptr || replacement == nullptr) return true;
                if (node->display_name != replacement->display_name && !increment()) return false;
                if (node->subgraph != replacement->subgraph && !increment()) return false;
                if (node->read_only != replacement->read_only && !increment()) return false;
                for (const auto& [key, property] : node->properties) {
                    const auto value = replacement->properties.find(key);
                    if ((value == replacement->properties.end() || value->second != property) && !increment())
                        return false;
                }
                for (const auto& [key, property] : replacement->properties) {
                    (void)property;
                    if (!node->properties.contains(key) && !increment()) return false;
                }
                return true;
            }))
        return count;
    if (!before.pins.ForEachDifferenceWhile(
            after.pins, [&](const PinId, const PinInstance*, const PinInstance*) { return increment(); }))
        return count;
    (void)before.links.ForEachDifferenceWhile(after.links,
                                              [&](const LinkId, const Link*, const Link*) { return increment(); });
    return count;
}

GroupGeometryOperation GraphTransaction::Impl::GroupGeometry(const GroupPresentation& group) {
    return GroupGeometryOperation{
        .graph = group.graph,
        .group = group.id,
        .value = group.geometry,
    };
}

std::shared_ptr<const GroupMembershipChange>
GraphTransaction::Impl::GroupMemberDifference(const GroupMemberSet& before, const GroupMemberSet& after) {
    auto change = std::make_shared<GroupMembershipChange>();
    for (const NodeId member : before) {
        if (!after.contains(member)) change->removed.push_back(member);
    }
    for (const NodeId member : after) {
        if (!before.contains(member)) change->added.push_back(member);
    }
    return change;
}

void GraphTransaction::Impl::TouchGraph(const GraphId graph, const SemanticDomain domain) {
    journal.graphs[graph] |= domain;
}

void GraphTransaction::Impl::TouchNode(const GraphId graph, const NodeId node, const SemanticDomain domain) {
    journal.nodes[EntityTouch<NodeId>{graph, node}] |= domain;
}

void GraphTransaction::Impl::TouchPin(const GraphId graph, const PinId pin, const SemanticDomain domain) {
    journal.pins[EntityTouch<PinId>{graph, pin}] |= domain;
}

void GraphTransaction::Impl::TouchLink(const GraphId graph, const LinkId link, const SemanticDomain domain) {
    journal.links[EntityTouch<LinkId>{graph, link}] |= domain;
}

void GraphTransaction::Impl::TouchLinkPresentation(
    const LinkId link,
    const LinkPresentationImpact impact,
    const bool full_route_validation,
    const std::span<const RoutePointId> route_points) {
    auto& touch = journal.link_presentations[link];
    touch.impact |= impact;
    touch.full_route_validation |= full_route_validation;
    touch.route_points.insert(route_points.begin(), route_points.end());
}

FinalizedJournal GraphTransaction::Impl::FinalizeJournal() const {
    FinalizedJournal result;
    const auto changed = [&](const GraphId graph, const SemanticDomain domain) {
        result.model_changed = true;
        result.document_domains |= domain;
        result.graph_domains[graph] |= domain;
        ++result.entries;
    };

    if (journal.schema && baseline_document.SchemaVersion() != staged_document.SchemaVersion()) {
        result.model_changed = true;
        result.document_domains |= SemanticDomain::Value;
        ++result.entries;
    }
    if (journal.root && baseline_document.RootGraph() != staged_document.RootGraph()) {
        result.model_changed = true;
        result.document_domains |= SemanticDomain::Topology;
        ++result.entries;
    }
    for (const auto& [graph, domain] : journal.graphs) {
        if (DifferentGraphState(baseline_document.FindGraph(graph), staged_document.FindGraph(graph))) {
            changed(graph, domain);
            result.graphs.push_back(graph);
            result.has_replaced_graph |= journal.replaced_graphs.contains(graph);
        }
    }
    for (const auto& [entity, domain] : journal.nodes) {
        if (Different(baseline_document.FindNode(entity.graph, entity.id),
                      staged_document.FindNode(entity.graph, entity.id))) {
            changed(entity.graph, domain);
            result.nodes.push_back(entity);
        }
    }
    for (const auto& [entity, domain] : journal.pins) {
        if (Different(baseline_document.FindPin(entity.graph, entity.id),
                      staged_document.FindPin(entity.graph, entity.id))) {
            changed(entity.graph, domain);
            result.pins.push_back(entity);
        }
    }
    for (const auto& [entity, domain] : journal.links) {
        if (Different(baseline_document.FindLink(entity.graph, entity.id),
                      staged_document.FindLink(entity.graph, entity.id))) {
            changed(entity.graph, domain);
            result.links.push_back(entity);
        }
    }
    for (const auto& [link, domain] : journal.intergraph_links) {
        const auto* before = baseline_document.FindIntergraphLink(link);
        const auto* after = staged_document.FindIntergraphLink(link);
        if (!Different(before, after)) continue;
        result.model_changed = true;
        result.document_domains |= domain;
        if (before != nullptr) {
            result.graph_domains[before->source.graph] |= domain;
            result.graph_domains[before->destination.graph] |= domain;
        }
        if (after != nullptr) {
            result.graph_domains[after->source.graph] |= domain;
            result.graph_domains[after->destination.graph] |= domain;
        }
        ++result.entries;
        result.intergraph_links.push_back(link);
    }
    for (const NodeId node : journal.node_presentations) {
        const auto* before = baseline_presentation.FindNode(node);
        const auto* after = staged_presentation.FindNode(node);
        if (!Different(before, after)) continue;
        result.presentation_changed = true;
        result.presentation_geometry_changed |= NodePresentationGeometryChanged(before, after);
        result.node_presentations.push_back(node);
        ++result.entries;
    }
    for (const auto& [link, touch] : journal.link_presentations) {
        const auto* before = baseline_presentation.FindLink(link);
        const auto* after = staged_presentation.FindLink(link);
        if (!Different(before, after)) continue;
        const LinkStyle empty_style;
        const PersistentRoutePointSequence empty_route;
        const LinkStyle& before_style = before != nullptr ? before->Style() : empty_style;
        const LinkStyle& after_style = after != nullptr ? after->Style() : empty_style;
        const PersistentRoutePointSequence& before_route = before != nullptr ? before->Route() : empty_route;
        const PersistentRoutePointSequence& after_route = after != nullptr ? after->Route() : empty_route;
        result.presentation_changed = true;
        result.presentation_geometry_changed |= before_style.router != after_style.router ||
            before_route != after_route;
        result.link_presentations.push_back(link);
        result.link_presentation_impacts.emplace(link, touch);
        ++result.entries;
    }
    for (const auto& [group, impact] : journal.groups) {
        const auto* before = baseline_presentation.FindGroup(group);
        const auto* after = staged_presentation.FindGroup(group);
        if (!Different(before, after)) continue;
        result.presentation_changed = true;
        result.presentation_geometry_changed |= HasImpact(impact, GroupImpact::Lifecycle) ||
                                                HasImpact(impact, GroupImpact::Geometry) ||
                                                HasImpact(impact, GroupImpact::Membership);
        result.groups.push_back(group);
        result.group_impacts.emplace(group, impact);
        ++result.entries;
    }
    return result;
}

void GraphTransaction::PushCommandScope(const std::string_view name, const std::size_t child) {
    if (!m_impl->record_operations) return;
    auto entries = std::make_shared<CommandPath::Storage>();
    if (!m_impl->command_paths.empty()) {
        const auto& current = m_impl->command_paths.back();
        entries->reserve(current.size() + 1);
        entries->insert(entries->end(), current.begin(), current.end());
    }
    entries->push_back(CommandPathEntry{child, std::string{name}});
    m_impl->command_paths.emplace_back(std::move(entries));
    Detail::RecordCommandPath();
}

void GraphTransaction::PopCommandScope() noexcept {
    if (!m_impl->record_operations) return;
    if (!m_impl->command_paths.empty()) m_impl->command_paths.pop_back();
}

#undef Record

} // namespace Uni::GUI::Nodes
