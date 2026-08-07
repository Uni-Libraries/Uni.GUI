#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::Deduplicate;
using CommandDetail::Finite;
using CommandDetail::MakeError;
using CommandDetail::OwnedClosure;
using CommandDetail::PlanDescriptorPins;

struct AddNodeCommand::Impl final {
    GraphId graph;
    NodeCreation creation;
    NodePresentation presentation;
};

AddNodeCommand::AddNodeCommand(const GraphId graph, NodeCreation creation, NodePresentation presentation)
    : m_impl(std::make_unique<Impl>(Impl{graph, std::move(creation), std::move(presentation)})) {}
AddNodeCommand::~AddNodeCommand() = default;
std::string_view AddNodeCommand::Name() const noexcept {
    return "Add node";
}
Result<void> AddNodeCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) {
    if (auto valid = CommandDetail::ValidateNodeCreationSchema(registry, m_impl->creation.node, m_impl->creation.pins,
                                                               m_impl->creation.prepared_descriptor);
        !valid) {
        return valid;
    }
    if (auto result = transaction.AddNode(m_impl->graph, m_impl->creation.node, m_impl->creation.pins); !result) {
        return result;
    }
    return transaction.SetNodePresentation(m_impl->creation.node.id, m_impl->presentation);
}
Result<void> AddNodeCommand::Revert(GraphTransaction& transaction) {
    auto removed = transaction.RemoveNode(m_impl->graph, m_impl->creation.node.id);
    if (!removed) {
        return std::unexpected(std::move(removed.error()));
    }
    for (const auto& link : removed->links) {
        if (auto result = transaction.SetLinkPresentation(link.id, std::nullopt); !result) return result;
    }
    return transaction.SetNodePresentation(m_impl->creation.node.id, std::nullopt);
}

struct DeleteElementsCommand::Impl final {
    struct NodeState final {
        RemovedNode removed;
        std::optional<NodePresentation> presentation;
    };
    struct LinkState final {
        Link link;
        std::optional<LinkPresentation> presentation;
    };
    struct GraphState final {
        Graph graph;
        std::vector<std::pair<NodeId, NodePresentation>> nodes;
        std::vector<std::pair<LinkId, LinkPresentation>> links;
        std::vector<GroupPresentation> groups;
    };

    GraphId graph;
    std::vector<NodeId> requested_nodes;
    std::vector<LinkId> requested_links;
    std::vector<NodeState> nodes;
    std::vector<LinkState> links;
    std::vector<GroupPresentation> groups;
    std::vector<GraphState> owned_graphs;
    std::vector<IntergraphLink> intergraph_links;
    bool captured{false};
};

DeleteElementsCommand::DeleteElementsCommand(const GraphId graph, std::vector<NodeId> nodes, std::vector<LinkId> links)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->graph = graph;
    m_impl->requested_nodes = std::move(nodes);
    m_impl->requested_links = std::move(links);
    Deduplicate(m_impl->requested_nodes);
    Deduplicate(m_impl->requested_links);
}
DeleteElementsCommand::~DeleteElementsCommand() = default;
std::string_view DeleteElementsCommand::Name() const noexcept {
    return "Delete elements";
}
Result<void> DeleteElementsCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* graph = transaction.Document().FindGraph(m_impl->graph);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (m_impl->requested_nodes.empty() && m_impl->requested_links.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "No elements were selected for deletion"));
    }
    if (!m_impl->captured) {
        std::unordered_set<LinkId, IdHash> links(m_impl->requested_links.begin(), m_impl->requested_links.end());
        std::unordered_set<PinId, IdHash> pins;
        for (const auto node : m_impl->requested_nodes) {
            const auto found = graph->nodes.find(node);
            if (found == graph->nodes.end()) {
                return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Selected node does not exist"));
            }
            pins.insert(found->second.pins.begin(), found->second.pins.end());
            for (const GraphId owned : OwnedClosure(transaction.Document(), found->second)) {
                if (std::ranges::any_of(m_impl->owned_graphs,
                                        [owned](const Impl::GraphState& state) { return state.graph.id == owned; })) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Selected owned subgraph closures overlap"));
                }
                const auto* owned_graph = transaction.Document().FindGraph(owned);
                if (owned_graph == nullptr) {
                    return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Owned subgraph does not exist"));
                }
                Impl::GraphState state{.graph = *owned_graph};
                for (const auto& [owned_node, value] : owned_graph->nodes) {
                    (void)value;
                    if (const auto* presentation = transaction.Presentation().FindNode(owned_node)) {
                        state.nodes.emplace_back(owned_node, *presentation);
                    }
                }
                for (const auto& [owned_link, value] : owned_graph->links) {
                    (void)value;
                    if (const auto* presentation = transaction.Presentation().FindLink(owned_link)) {
                        state.links.emplace_back(owned_link, *presentation);
                    }
                }
                for (const GroupId group : transaction.Presentation().GroupsForGraph(owned)) {
                    if (const auto* value = transaction.Presentation().FindGroup(group)) {
                        state.groups.push_back(*value);
                    }
                }
                m_impl->owned_graphs.push_back(std::move(state));
            }
        }
        std::unordered_set<GraphId, IdHash> removed_graphs;
        for (const auto& state : m_impl->owned_graphs) {
            removed_graphs.insert(state.graph.id);
        }
        std::unordered_set<IntergraphLinkId, IdHash> intergraph_links;
        for (const GraphId graph_id : removed_graphs) {
            const auto incident = transaction.Document().IntergraphLinksForGraph(graph_id);
            intergraph_links.insert(incident.begin(), incident.end());
        }
        for (const NodeId node_id : m_impl->requested_nodes) {
            const auto* node = transaction.Document().FindNode(m_impl->graph, node_id);
            if (node == nullptr) continue;
            for (const PinId pin : node->pins) {
                if (const IntergraphLinkId link = transaction.Document().IntergraphLinkForPin(pin)) {
                    intergraph_links.insert(link);
                }
            }
        }
        for (const IntergraphLinkId link : intergraph_links) {
            if (const auto* value = transaction.Document().FindIntergraphLink(link)) {
                m_impl->intergraph_links.push_back(*value);
            }
        }
        for (const PinId pin : pins) {
            const auto incident = transaction.Document().IncidentLinks(pin);
            links.insert(incident.begin(), incident.end());
        }
        for (const auto link : links) {
            const auto found = graph->links.find(link);
            if (found == graph->links.end()) {
                return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Selected link does not exist"));
            }
            const auto* state = transaction.Presentation().FindLink(link);
            m_impl->links.push_back({found->second, state ? std::optional<LinkPresentation>{*state} : std::nullopt});
        }
        for (const auto node : m_impl->requested_nodes) {
            const auto* state = transaction.Presentation().FindNode(node);
            m_impl->nodes.push_back({
                .removed = RemovedNode{.node = graph->nodes.at(node)},
                .presentation = state ? std::optional<NodePresentation>{*state} : std::nullopt,
            });
            for (const auto pin : graph->nodes.at(node).pins) {
                m_impl->nodes.back().removed.pins.push_back(graph->pins.at(pin));
            }
        }
        std::unordered_set<GroupId, IdHash> affected_groups;
        for (const NodeId node : m_impl->requested_nodes) {
            const auto groups = transaction.Presentation().GroupsForNode(node);
            affected_groups.insert(groups.begin(), groups.end());
        }
        for (const GroupId group : affected_groups) {
            const auto* state = transaction.Presentation().FindGroup(group);
            if (state != nullptr && state->graph == m_impl->graph) m_impl->groups.push_back(*state);
        }
        m_impl->captured = true;
    }

    for (const auto& link : m_impl->intergraph_links) {
        if (auto removed = transaction.RemoveIntergraphLink(link.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }

    for (const auto& previous : m_impl->groups) {
        if (auto result = transaction.RemoveGroupMembers(previous.id, m_impl->requested_nodes); !result) return result;
    }
    for (const auto& state : m_impl->links) {
        if (auto removed = transaction.RemoveLink(m_impl->graph, state.link.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (const auto& state : m_impl->nodes) {
        if (auto removed = transaction.RemoveNode(m_impl->graph, state.removed.node.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (const auto& state : m_impl->owned_graphs) {
        for (const auto& group : state.groups) {
            if (auto removed = transaction.RemoveGroup(group.id); !removed) {
                return std::unexpected(std::move(removed.error()));
            }
        }
        for (const auto& [link, presentation] : state.links) {
            (void)presentation;
            if (auto result = transaction.SetLinkPresentation(link, std::nullopt); !result) return result;
        }
        for (const auto& [node, presentation] : state.nodes) {
            (void)presentation;
            if (auto result = transaction.SetNodePresentation(node, std::nullopt); !result) return result;
        }
        if (auto removed = transaction.RemoveGraph(state.graph.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (const auto& state : m_impl->links) {
        if (auto result = transaction.SetLinkPresentation(state.link.id, std::nullopt); !result) return result;
    }
    for (const auto& state : m_impl->nodes) {
        if (auto result = transaction.SetNodePresentation(state.removed.node.id, std::nullopt); !result) return result;
    }
    return {};
}
Result<void> DeleteElementsCommand::Revert(GraphTransaction& transaction) {
    if (!m_impl->captured) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Delete command was not executed"));
    }
    for (auto state = m_impl->owned_graphs.rbegin(); state != m_impl->owned_graphs.rend(); ++state) {
        if (auto result = transaction.RestoreGraph(state->graph); !result) return result;
        for (const auto& [node, presentation] : state->nodes) {
            if (auto result = transaction.SetNodePresentation(node, presentation); !result) return result;
        }
        for (const auto& [link, presentation] : state->links) {
            if (auto result = transaction.SetLinkPresentation(link, presentation); !result) return result;
        }
        for (const auto& group : state->groups) {
            if (auto result = transaction.AddGroup(group); !result) return result;
        }
    }
    for (const auto& state : m_impl->nodes) {
        if (auto result = transaction.RestoreNode(m_impl->graph, state.removed); !result) return result;
    }
    for (const auto& state : m_impl->links) {
        if (auto result = transaction.AddLink(m_impl->graph, state.link); !result) return result;
    }
    for (const auto& state : m_impl->nodes) {
        if (auto result = transaction.SetNodePresentation(state.removed.node.id, state.presentation); !result)
            return result;
    }
    for (const auto& state : m_impl->links) {
        if (auto result = transaction.SetLinkPresentation(state.link.id, state.presentation); !result) return result;
    }
    for (const auto& state : m_impl->groups) {
        if (auto result = transaction.SetGroupMembers(state.id, state.members); !result) return result;
    }
    for (const auto& link : m_impl->intergraph_links) {
        if (auto result = transaction.AddIntergraphLink(link); !result) return result;
    }
    return {};
}

struct MoveNodesCommand::Impl final {
    GraphId graph;
    Positions before;
    Positions after;
};

MoveNodesCommand::MoveNodesCommand(const GraphId graph, Positions before, Positions after)
    : m_impl(std::make_unique<Impl>(Impl{graph, std::move(before), std::move(after)})) {}
MoveNodesCommand::~MoveNodesCommand() = default;
std::string_view MoveNodesCommand::Name() const noexcept {
    return "Move nodes";
}
Result<void> MoveNodesCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (m_impl->before.size() != m_impl->after.size() ||
        std::ranges::any_of(m_impl->before, [&](const auto& value) { return !m_impl->after.contains(value.first); })) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidArgument, "Move command requires identical before and after node sets"));
    }
    for (const auto& [node, before] : m_impl->before) {
        const auto* state = transaction.Presentation().FindNode(node);
        if (transaction.Document().FindNode(m_impl->graph, node) == nullptr) {
            return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Moved node does not exist"));
        }
        if (state == nullptr || state->position != before) {
            return std::unexpected(MakeError(ErrorCode::CommandFailed, "Node position changed before move commit"));
        }
        if (!Finite(before) || !Finite(m_impl->after.at(node))) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node positions must be finite"));
        }
    }
    for (const auto& [node, position] : m_impl->after) {
        auto state = *transaction.Presentation().FindNode(node);
        state.position = position;
        if (auto result = transaction.SetNodePresentation(node, std::move(state)); !result) return result;
    }
    return {};
}
Result<void> MoveNodesCommand::Revert(GraphTransaction& transaction) {
    for (const auto& [node, position] : m_impl->before) {
        const auto* current = transaction.Presentation().FindNode(node);
        if (current == nullptr) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Moved node has no presentation state"));
        }
        auto state = *current;
        state.position = position;
        if (auto result = transaction.SetNodePresentation(node, std::move(state)); !result) return result;
    }
    return {};
}

struct SetNodePropertyCommand::Impl final {
    struct LinkState final {
        Link link;
        std::optional<LinkPresentation> presentation;
    };

    GraphId graph;
    NodeId node;
    std::string key;
    std::optional<PropertyValue> value;
    std::optional<PropertyValue> previous;
    std::optional<PropertyImpact> impact;
    std::vector<PinInstance> previous_pins;
    std::vector<PinInstance> projected_pins;
    std::vector<LinkState> removed_links;
    std::vector<IntergraphLink> removed_intergraph_links;
    NodeTypeDescriptorPtr descriptor;
    Edit edit;
    InvalidConnectionPolicy invalid_connections{InvalidConnectionPolicy::Disconnect};
    bool reproject_descriptor_pins{false};
    bool merge_open{false};
    bool captured{false};
};

SetNodePropertyCommand::SetNodePropertyCommand(const GraphId graph, const NodeId node, std::string key,
                                               std::optional<PropertyValue> value)
    : SetNodePropertyCommand(graph, node, std::move(key), std::move(value), Edit{}) {}
SetNodePropertyCommand::SetNodePropertyCommand(const GraphId graph, const NodeId node, std::string key,
                                               std::optional<PropertyValue> value,
                                               const InvalidConnectionPolicy invalid_connections)
    : SetNodePropertyCommand(graph, node, std::move(key), std::move(value), Edit{}, invalid_connections) {}
SetNodePropertyCommand::SetNodePropertyCommand(const GraphId graph, const NodeId node, std::string key,
                                               std::optional<PropertyValue> value, const Edit edit)
    : SetNodePropertyCommand(graph, node, std::move(key), std::move(value), edit,
                             InvalidConnectionPolicy::Disconnect) {}
SetNodePropertyCommand::SetNodePropertyCommand(const GraphId graph, const NodeId node, std::string key,
                                               std::optional<PropertyValue> value, const Edit edit,
                                               const InvalidConnectionPolicy invalid_connections)
    : m_impl(std::make_unique<Impl>(Impl{
          .graph = graph,
          .node = node,
          .key = std::move(key),
          .value = std::move(value),
          .edit = edit,
          .invalid_connections = invalid_connections,
          .merge_open = edit.merge_key != 0 && !edit.final,
      })) {}
SetNodePropertyCommand::~SetNodePropertyCommand() = default;
std::string_view SetNodePropertyCommand::Name() const noexcept {
    return "Set node property";
}
Result<void> SetNodePropertyCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) {
    if (m_impl->captured && m_impl->descriptor && registry.Find(m_impl->descriptor->type) != m_impl->descriptor) {
        return std::unexpected(MakeError(
            ErrorCode::RevisionConflict,
            "Node pin schema descriptor changed after the property command was prepared"));
    }
    if (!m_impl->captured) {
        const auto* node = transaction.Document().FindNode(m_impl->graph, m_impl->node);
        if (node == nullptr) {
            return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
        }
        if (const auto found = node->properties.find(m_impl->key); found != node->properties.end()) {
            m_impl->previous = found->second;
        }
        m_impl->impact = transaction.ResolvePropertyImpact(node->type, m_impl->key);
        m_impl->reproject_descriptor_pins = registry.PinSchemaDependsOn(node->type, m_impl->key);
        if (m_impl->reproject_descriptor_pins) {
            m_impl->descriptor = registry.Find(node->type);
            if (!m_impl->descriptor || node->type_version != m_impl->descriptor->version) {
                return std::unexpected(MakeError(
                    ErrorCode::RevisionConflict,
                    "Node pin schema requires the exact registered descriptor version"));
            }
            PropertyBag properties = node->properties;
            if (m_impl->value) {
                properties.insert_or_assign(m_impl->key, *m_impl->value);
            } else {
                properties.erase(m_impl->key);
            }
            auto projected = registry.ResolvePinSchema(node->type, properties);
            if (!projected)
                return std::unexpected(std::move(projected.error()));

            auto plan = PlanDescriptorPins(
                transaction.Document(), m_impl->graph, m_impl->node, *projected,
                [&] { return transaction.AllocatePinId(); });
            if (!plan)
                return std::unexpected(std::move(plan.error()));
            m_impl->previous_pins = std::move(plan->before);
            m_impl->projected_pins = std::move(plan->after);

            std::unordered_set<LinkId, IdHash> incident_links;
            std::unordered_set<IntergraphLinkId, IdHash> intergraph_links;
            std::unordered_map<PinId, const PinInstance*, IdHash> projected_by_id;
            std::unordered_set<PinId, IdHash> previous_descriptor_ids;
            for (const PinInstance& pin : m_impl->previous_pins)
                previous_descriptor_ids.insert(pin.id);
            for (const PinInstance& pin : m_impl->projected_pins)
                projected_by_id.emplace(pin.id, &pin);
            const auto resolved_pin = [&](const PinId id) -> const PinInstance* {
                const auto projected_pin = projected_by_id.find(id);
                if (projected_pin != projected_by_id.end())
                    return projected_pin->second;
                return previous_descriptor_ids.contains(id)
                           ? nullptr
                           : transaction.Document().FindPin(m_impl->graph, id);
            };
            const auto local_link_valid = [&](const Link& link) {
                const auto* output = resolved_pin(link.output);
                const auto* input = resolved_pin(link.input);
                if (output == nullptr || input == nullptr ||
                    output->direction != PinDirection::Output ||
                    input->direction != PinDirection::Input || output->kind != input->kind) {
                    return false;
                }
                if ((output->cardinality == PinCardinality::Single &&
                     transaction.Document().IncidentLinks(output->id).size() > 1) ||
                    (input->cardinality == PinCardinality::Single &&
                     transaction.Document().IncidentLinks(input->id).size() > 1)) {
                    return false;
                }
                return registry.Check(output->type, input->type, output->kind).status ==
                       ConnectionResult::Status::Allowed;
            };
            for (const PinId pin : plan->connection_changed) {
                for (const LinkId link_id : transaction.Document().IncidentLinks(pin)) {
                    const auto* link = transaction.Document().FindLink(m_impl->graph, link_id);
                    if (link == nullptr || !local_link_valid(*link))
                        incident_links.insert(link_id);
                }
                if (const IntergraphLinkId link_id = transaction.Document().IntergraphLinkForPin(pin)) {
                    const auto* link = transaction.Document().FindIntergraphLink(link_id);
                    if (link == nullptr) {
                        intergraph_links.insert(link_id);
                    } else {
                        const auto* source = link->source.graph == m_impl->graph
                                                 ? resolved_pin(link->source.pin)
                                                 : transaction.Document().FindPin(link->source.graph, link->source.pin);
                        const auto* destination = link->destination.graph == m_impl->graph
                                                      ? resolved_pin(link->destination.pin)
                                                      : transaction.Document().FindPin(link->destination.graph,
                                                                                       link->destination.pin);
                        if (source == nullptr || destination == nullptr ||
                            source->direction != PinDirection::Input ||
                            destination->direction != PinDirection::Output ||
                            source->type != destination->type || source->kind != destination->kind) {
                            intergraph_links.insert(link_id);
                        }
                    }
                }
            }
            std::vector<LinkId> ordered_links(incident_links.begin(), incident_links.end());
            std::ranges::sort(ordered_links);
            for (const LinkId id : ordered_links) {
                const auto* link = transaction.Document().FindLink(m_impl->graph, id);
                if (link == nullptr) {
                    return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Incident link does not exist"));
                }
                const auto* presentation = transaction.Presentation().FindLink(id);
                m_impl->removed_links.push_back(Impl::LinkState{
                    .link = *link,
                    .presentation = presentation ? std::optional<LinkPresentation>{*presentation} : std::nullopt,
                });
            }
            std::vector<IntergraphLinkId> ordered_intergraph(intergraph_links.begin(), intergraph_links.end());
            std::ranges::sort(ordered_intergraph);
            for (const IntergraphLinkId id : ordered_intergraph) {
                const auto* link = transaction.Document().FindIntergraphLink(id);
                if (link == nullptr) {
                    return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Incident intergraph link does not exist"));
                }
                m_impl->removed_intergraph_links.push_back(*link);
            }
            if (m_impl->invalid_connections == InvalidConnectionPolicy::Reject &&
                (!m_impl->removed_links.empty() || !m_impl->removed_intergraph_links.empty())) {
                return std::unexpected(MakeError(
                    ErrorCode::IncompatiblePins,
                    "Resolved pin schema would invalidate existing connections"));
            }
        }
        m_impl->captured = true;
    }

    for (const IntergraphLink& link : m_impl->removed_intergraph_links) {
        if (auto removed = transaction.RemoveIntergraphLink(link.id); !removed)
            return std::unexpected(std::move(removed.error()));
    }
    for (const Impl::LinkState& state : m_impl->removed_links) {
        if (auto presentation = transaction.SetLinkPresentation(state.link.id, std::nullopt); !presentation)
            return presentation;
        if (auto removed = transaction.RemoveLink(m_impl->graph, state.link.id); !removed)
            return std::unexpected(std::move(removed.error()));
    }
    if (auto property = transaction.SetNodePropertyWithImpact(m_impl->graph, m_impl->node, m_impl->key, m_impl->value, *m_impl->impact); !property) {
        return property;
    }
    if (m_impl->reproject_descriptor_pins)
        return transaction.SetDescriptorPins(m_impl->graph, m_impl->node, m_impl->projected_pins);
    return {};
}
Result<void> SetNodePropertyCommand::Revert(GraphTransaction& transaction) {
    if (m_impl->descriptor && transaction.Registry().Find(m_impl->descriptor->type) != m_impl->descriptor) {
        return std::unexpected(MakeError(
            ErrorCode::RevisionConflict,
            "Node pin schema descriptor changed after the property command was prepared"));
    }
    if (auto property = transaction.SetNodePropertyWithImpact(m_impl->graph, m_impl->node, m_impl->key, m_impl->previous, *m_impl->impact); !property) {
        return property;
    }
    if (m_impl->reproject_descriptor_pins) {
        if (auto pins = transaction.SetDescriptorPins(m_impl->graph, m_impl->node, m_impl->previous_pins); !pins)
            return pins;
    }
    for (const Impl::LinkState& state : m_impl->removed_links) {
        if (auto link = transaction.AddLink(m_impl->graph, state.link); !link)
            return link;
        if (auto presentation = transaction.SetLinkPresentation(state.link.id, state.presentation); !presentation)
            return presentation;
    }
    for (const IntergraphLink& link : m_impl->removed_intergraph_links) {
        if (auto restored = transaction.AddIntergraphLink(link); !restored)
            return restored;
    }
    return {};
}
bool SetNodePropertyCommand::TryMerge(const Command& newer) {
    const auto* property = dynamic_cast<const SetNodePropertyCommand*>(&newer);
    if (property == nullptr || !m_impl->merge_open || m_impl->edit.merge_key == 0 || m_impl->edit.merge_key != property->m_impl->edit.merge_key ||
        m_impl->graph != property->m_impl->graph || m_impl->node != property->m_impl->node || m_impl->key != property->m_impl->key ||
        m_impl->invalid_connections != property->m_impl->invalid_connections || m_impl->reproject_descriptor_pins ||
        property->m_impl->reproject_descriptor_pins) {
        return false;
    }
    if (property->m_impl->edit.begin) {
        m_impl->merge_open = false;
        return false;
    }
    m_impl->value = property->m_impl->value;
    if (property->m_impl->impact && (!m_impl->impact || static_cast<std::uint8_t>(*property->m_impl->impact) >
                                                            static_cast<std::uint8_t>(*m_impl->impact))) {
        m_impl->impact = property->m_impl->impact;
    }
    m_impl->merge_open = !property->m_impl->edit.final;
    return true;
}

struct SetNodeDisplayNameCommand::Impl final {
    GraphId graph;
    NodeId node;
    std::string value;
    std::string previous;
    bool captured{false};
};

SetNodeDisplayNameCommand::SetNodeDisplayNameCommand(const GraphId graph, const NodeId node, std::string name)
    : m_impl(std::make_unique<Impl>(Impl{.graph = graph, .node = node, .value = std::move(name)})) {}
SetNodeDisplayNameCommand::~SetNodeDisplayNameCommand() = default;
std::string_view SetNodeDisplayNameCommand::Name() const noexcept {
    return "Set node display name";
}
Result<void> SetNodeDisplayNameCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        const auto* node = transaction.Document().FindNode(m_impl->graph, m_impl->node);
        if (node == nullptr) return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
        m_impl->previous = node->display_name;
        m_impl->captured = true;
    }
    return transaction.SetNodeDisplayName(m_impl->graph, m_impl->node, m_impl->value);
}
Result<void> SetNodeDisplayNameCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetNodeDisplayName(m_impl->graph, m_impl->node, m_impl->previous);
}

struct SetNodePresentationCommand::Impl final {
    NodeId node;
    NodePresentation value;
    std::optional<NodePresentation> previous;
    bool captured{false};
};

SetNodePresentationCommand::SetNodePresentationCommand(const NodeId node, NodePresentation presentation)
    : m_impl(std::make_unique<Impl>(Impl{.node = node, .value = std::move(presentation)})) {}
SetNodePresentationCommand::~SetNodePresentationCommand() = default;
std::string_view SetNodePresentationCommand::Name() const noexcept {
    return "Set node presentation";
}
Result<void> SetNodePresentationCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (!m_impl->captured) {
        if (const auto* value = transaction.Presentation().FindNode(m_impl->node)) m_impl->previous = *value;
        m_impl->captured = true;
    }
    return transaction.SetNodePresentation(m_impl->node, m_impl->value);
}
Result<void> SetNodePresentationCommand::Revert(GraphTransaction& transaction) {
    return transaction.SetNodePresentation(m_impl->node, m_impl->previous);
}

struct SetNodeZOrderCommand::Impl final {
    Orders values;
    Orders previous;
    bool captured{false};
};

SetNodeZOrderCommand::SetNodeZOrderCommand(Orders orders) : m_impl(std::make_unique<Impl>()) {
    m_impl->values = std::move(orders);
}
SetNodeZOrderCommand::SetNodeZOrderCommand(const NodeId node, const std::uint64_t z_order)
    : SetNodeZOrderCommand(Orders{{node, z_order}}) {}
SetNodeZOrderCommand::~SetNodeZOrderCommand() = default;
std::string_view SetNodeZOrderCommand::Name() const noexcept {
    return "Set node Z-order";
}
Result<void> SetNodeZOrderCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    if (m_impl->values.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Z-order command cannot be empty"));
    }
    if (!m_impl->captured) {
        for (const auto& [node, value] : m_impl->values) {
            (void)value;
            const auto* state = transaction.Presentation().FindNode(node);
            if (state == nullptr)
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node has no presentation state"));
            m_impl->previous.emplace(node, state->z_order);
        }
        m_impl->captured = true;
    }
    for (const auto& [node, value] : m_impl->values) {
        const auto* current = transaction.Presentation().FindNode(node);
        if (current == nullptr)
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node has no presentation state"));
        auto state = *current;
        state.z_order = value;
        if (auto result = transaction.SetNodePresentation(node, std::move(state)); !result) return result;
    }
    return {};
}
Result<void> SetNodeZOrderCommand::Revert(GraphTransaction& transaction) {
    for (const auto& [node, value] : m_impl->previous) {
        const auto* current = transaction.Presentation().FindNode(node);
        if (current == nullptr)
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node has no presentation state"));
        auto state = *current;
        state.z_order = value;
        if (auto result = transaction.SetNodePresentation(node, std::move(state)); !result) return result;
    }
    return {};
}

struct ResizeNodeCommand::Impl final {
    NodeId node;
    Vec2 value;
    Vec2 previous;
    bool captured{false};
};

ResizeNodeCommand::ResizeNodeCommand(const NodeId node, const Vec2 size)
    : m_impl(std::make_unique<Impl>(Impl{.node = node, .value = size})) {}
ResizeNodeCommand::~ResizeNodeCommand() = default;
std::string_view ResizeNodeCommand::Name() const noexcept {
    return "Resize node";
}
Result<void> ResizeNodeCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Presentation().FindNode(m_impl->node);
    if (current == nullptr)
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node has no presentation state"));
    if (!m_impl->captured) {
        m_impl->previous = current->size;
        m_impl->captured = true;
    }
    auto state = *current;
    state.size = m_impl->value;
    return transaction.SetNodePresentation(m_impl->node, std::move(state));
}
Result<void> ResizeNodeCommand::Revert(GraphTransaction& transaction) {
    const auto* current = transaction.Presentation().FindNode(m_impl->node);
    if (current == nullptr)
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node has no presentation state"));
    auto state = *current;
    state.size = m_impl->previous;
    return transaction.SetNodePresentation(m_impl->node, std::move(state));
}

struct SetNodeCollapsedCommand::Impl final {
    NodeId node;
    bool value;
    bool previous{false};
    bool captured{false};
};

SetNodeCollapsedCommand::SetNodeCollapsedCommand(const NodeId node, const bool collapsed)
    : m_impl(std::make_unique<Impl>(Impl{.node = node, .value = collapsed})) {}
SetNodeCollapsedCommand::~SetNodeCollapsedCommand() = default;
std::string_view SetNodeCollapsedCommand::Name() const noexcept {
    return "Set node collapsed";
}
Result<void> SetNodeCollapsedCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot&) {
    const auto* current = transaction.Presentation().FindNode(m_impl->node);
    if (current == nullptr)
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node has no presentation state"));
    if (!m_impl->captured) {
        m_impl->previous = current->collapsed;
        m_impl->captured = true;
    }
    auto state = *current;
    state.collapsed = m_impl->value;
    return transaction.SetNodePresentation(m_impl->node, std::move(state));
}
Result<void> SetNodeCollapsedCommand::Revert(GraphTransaction& transaction) {
    const auto* current = transaction.Presentation().FindNode(m_impl->node);
    if (current == nullptr)
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node has no presentation state"));
    auto state = *current;
    state.collapsed = m_impl->previous;
    return transaction.SetNodePresentation(m_impl->node, std::move(state));
}

} // namespace Uni::GUI::Nodes
