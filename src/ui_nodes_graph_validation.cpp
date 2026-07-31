#include <uni/gui/nodes/graph.h>

#include "ui_nodes_internal.h"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) { return Error{code, std::move(message)}; }

[[nodiscard]] bool ValidDirection(const PinDirection direction) noexcept {
    return direction == PinDirection::Input || direction == PinDirection::Output;
}

[[nodiscard]] bool ValidKind(const PinKind kind) noexcept {
    return kind == PinKind::Data || kind == PinKind::Execution;
}

[[nodiscard]] bool ValidCardinality(const PinCardinality cardinality) noexcept {
    return cardinality == PinCardinality::Single || cardinality == PinCardinality::Multiple;
}

[[nodiscard]] bool ValidStorage(const PinStorage storage) noexcept {
    return storage == PinStorage::Static || storage == PinStorage::Dynamic;
}

[[nodiscard]] bool ValidRole(const NodeRole role) noexcept {
    return role == NodeRole::Regular || role == NodeRole::Subgraph || role == NodeRole::BoundaryInput ||
           role == NodeRole::BoundaryOutput || role == NodeRole::IntergraphInput || role == NodeRole::IntergraphOutput;
}

[[nodiscard]] bool ValidLifetime(const GraphLifetime lifetime) noexcept {
    return lifetime == GraphLifetime::Reusable || lifetime == GraphLifetime::Owned;
}

[[nodiscard]] bool ValidOwnership(const SubgraphOwnership ownership) noexcept {
    return ownership == SubgraphOwnership::Referenced || ownership == SubgraphOwnership::Owned;
}

[[nodiscard]] bool ValidInterface(const GraphInterface& interface) noexcept {
    if (interface.version == 0) return false;
    std::unordered_set<std::string> keys;
    return std::ranges::all_of(interface.pins, [&](const GraphInterfacePin& pin) {
        return !pin.key.empty() && !pin.type.Empty() && ValidDirection(pin.direction) && ValidKind(pin.kind) &&
               ValidCardinality(pin.caller_cardinality) && ValidCardinality(pin.boundary_cardinality) &&
               keys.insert(pin.key).second;
    });
}

[[nodiscard]] const GraphInterface* SubgraphInterface(const GraphDocument& document,
                                                      const SubgraphReference& reference) noexcept {
    if (const auto* local = std::get_if<DocumentGraphTarget>(&reference.target)) {
        const auto* graph = document.FindGraph(local->graph);
        return graph != nullptr ? &graph->interface : nullptr;
    }
    return &std::get<GraphAssetTarget>(reference.target).interface;
}

[[nodiscard]] bool ProjectionMatches(const Graph& graph, const NodeInstance& node, const GraphInterface& interface,
                                     const NodeRole role) {
    std::vector<const GraphInterfacePin*> expected;
    expected.reserve(interface.pins.size());
    for (const auto& pin : interface.pins) {
        if (role == NodeRole::Subgraph || (role == NodeRole::BoundaryInput && pin.direction == PinDirection::Input) ||
            (role == NodeRole::BoundaryOutput && pin.direction == PinDirection::Output)) {
            expected.push_back(&pin);
        }
    }

    std::vector<const PinInstance*> actual;
    for (const PinId pin_id : node.pins) {
        const auto found = graph.pins.find(pin_id);
        if (found == graph.pins.end()) return false;
        if (found->second.storage == PinStorage::Dynamic) actual.push_back(&found->second);
    }
    if (actual.size() != expected.size()) return false;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto& source = *expected[index];
        const auto& projected = *actual[index];
        const PinDirection direction = role == NodeRole::BoundaryInput    ? PinDirection::Output
                                       : role == NodeRole::BoundaryOutput ? PinDirection::Input
                                                                          : source.direction;
        const PinCardinality cardinality =
            role == NodeRole::Subgraph ? source.caller_cardinality : source.boundary_cardinality;
        if (projected.key != source.key || projected.label != source.label || projected.type != source.type ||
            projected.direction != direction || projected.kind != source.kind || projected.cardinality != cardinality) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidPropertyValue(const PropertyValue& value) noexcept {
    return std::visit(
        [](const auto& property) noexcept {
            using Value = std::decay_t<decltype(property)>;
            if constexpr (std::is_same_v<Value, double>) {
                return std::isfinite(property);
            } else if constexpr (std::is_same_v<Value, Vec2>) {
                return std::isfinite(property.x) && std::isfinite(property.y);
            } else if constexpr (std::is_same_v<Value, OpaqueJsonProperty>) {
                return Detail::ValidOpaqueJsonProperty(property.canonical_json);
            } else {
                return true;
            }
        },
        value);
}

[[nodiscard]] bool ValidProperties(const PropertyBag& properties) noexcept {
    return std::ranges::all_of(
        properties, [](const auto& entry) { return !entry.first.empty() && ValidPropertyValue(entry.second); });
}

} // namespace

Result<void> GraphDocument::ValidateIntergraphLinkStructure(const IntergraphLinkId id) const {
    const auto* link = FindIntergraphLink(id);
    if (link == nullptr) return {};
    const auto* source_node = FindNode(link->source.graph, link->source.node);
    const auto* source_pin = FindPin(link->source.graph, link->source.pin);
    const auto* destination_node = FindNode(link->destination.graph, link->destination.node);
    const auto* destination_pin = FindPin(link->destination.graph, link->destination.pin);
    if (link->id != id || source_node == nullptr || source_pin == nullptr || destination_node == nullptr ||
        destination_pin == nullptr || link->source.graph == link->destination.graph ||
        source_node->role != NodeRole::IntergraphOutput || destination_node->role != NodeRole::IntergraphInput ||
        source_pin->node != source_node->id || destination_pin->node != destination_node->id ||
        source_pin->direction != PinDirection::Input || destination_pin->direction != PinDirection::Output ||
        source_pin->type != destination_pin->type || source_pin->kind != destination_pin->kind ||
        IntergraphLinkForPin(source_pin->id) != id || IntergraphLinkForPin(destination_pin->id) != id) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Intergraph link endpoint is invalid"));
    }
    return {};
}

Result<void> GraphDocument::ValidateLocalLinkStructure(const GraphId graph_id, const LinkId id) const {
    const auto* link = FindLink(graph_id, id);
    if (link == nullptr) return {};
    const auto* output = FindPin(graph_id, link->output);
    const auto* input = FindPin(graph_id, link->input);
    if (!id || link->id != id || FindLinkGraph(id) != graph_id || output == nullptr || input == nullptr ||
        output->direction != PinDirection::Output || input->direction != PinDirection::Input ||
        output->kind != input->kind) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Local link endpoint is invalid"));
    }
    if (FindLinkBetween(output->id, input->id) != link->id ||
        (output->cardinality == PinCardinality::Single && IncidentLinks(output->id).size() > 1) ||
        (input->cardinality == PinCardinality::Single && IncidentLinks(input->id).size() > 1)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Local link cardinality is invalid"));
    }
    return {};
}

Result<void> GraphDocument::ValidateTargetOwnership(const GraphId id) const {
    const auto* graph = FindGraph(id);
    if (graph == nullptr) return {};
    const std::size_t owners = OwnedSubgraphCallerCount(id);
    if ((graph->lifetime == GraphLifetime::Owned && owners != 1) ||
        (graph->lifetime == GraphLifetime::Reusable && owners != 0)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned graphs require exactly one owner "
                                                                  "and reusable graphs cannot have one"));
    }
    return {};
}

Result<void> GraphDocument::ValidateNodeRelations(const GraphId graph_id, const NodeId node_id) const {
    const auto* graph = FindGraph(graph_id);
    const auto* node = FindNode(graph_id, node_id);
    if (graph == nullptr || node == nullptr) return {};
    if (!node_id || node->id != node_id || FindNodeGraph(node_id) != graph_id || node->type.Empty() ||
        node->type_version == 0 || !ValidRole(node->role) || !ValidProperties(node->properties) ||
        ((node->role == NodeRole::Subgraph) != node->subgraph.has_value())) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node metadata or role is invalid"));
    }
    std::unordered_set<PinId, IdHash> pins;
    std::unordered_set<std::string> keys;
    for (const PinId pin_id : node->pins) {
        const auto* pin = FindPin(graph_id, pin_id);
        if (pin == nullptr || pin->node != node_id || !pins.insert(pin_id).second || !keys.insert(pin->key).second) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node pin ownership is invalid"));
        }
        if (const IntergraphLinkId link = IntergraphLinkForPin(pin_id)) {
            if (auto valid = ValidateIntergraphLinkStructure(link); !valid) return valid;
        }
    }
    if (node->role == NodeRole::BoundaryInput || node->role == NodeRole::BoundaryOutput) {
        if (node->subgraph || !ProjectionMatches(*graph, *node, graph->interface, node->role)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Boundary projection is invalid"));
        }
    }
    if (!node->subgraph) return {};
    if (!ValidOwnership(node->subgraph->ownership)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph ownership is invalid"));
    }
    const GraphInterface* interface = SubgraphInterface(*this, *node->subgraph);
    if (interface == nullptr || !ValidInterface(*interface) ||
        !ProjectionMatches(*graph, *node, *interface, NodeRole::Subgraph)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph projection is invalid"));
    }
    if (const auto* local = std::get_if<DocumentGraphTarget>(&node->subgraph->target)) {
        const auto* target = FindGraph(local->graph);
        const bool owned = node->subgraph->ownership == SubgraphOwnership::Owned;
        if (target == nullptr || target == graph || (owned && target->lifetime != GraphLifetime::Owned) ||
            (!owned && target->lifetime != GraphLifetime::Reusable)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph target is invalid"));
        }
        return ValidateTargetOwnership(local->graph);
    }
    const auto& asset = std::get<GraphAssetTarget>(node->subgraph->target);
    if (node->subgraph->ownership != SubgraphOwnership::Referenced || asset.asset.Empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset reference is invalid"));
    }
    return {};
}

Result<void> GraphDocument::ValidateBoundaryRelations(const GraphId graph_id) const {
    const auto* graph = FindGraph(graph_id);
    if (graph == nullptr) return {};
    const auto& inputs = BoundaryNodes(graph_id, NodeRole::BoundaryInput);
    const auto& outputs = BoundaryNodes(graph_id, NodeRole::BoundaryOutput);
    if ((!graph->interface.pins.empty() || !inputs.empty() || !outputs.empty()) &&
        (inputs.size() != 1 || outputs.size() != 1)) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Graph interface requires exactly one input and output boundary"));
    }
    for (const NodeId node : inputs) {
        if (auto valid = ValidateNodeRelations(graph_id, node); !valid) return valid;
    }
    for (const NodeId node : outputs) {
        if (auto valid = ValidateNodeRelations(graph_id, node); !valid) return valid;
    }
    return {};
}

Result<void> GraphDocument::ValidateReplacement(const Graph& before) const {
    const Graph* graph = FindGraph(before.id);
    if (graph == nullptr || graph->id != before.id || !ValidLifetime(graph->lifetime) ||
        !ValidInterface(graph->interface)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Replacement graph metadata is invalid"));
    }

    const auto validate_intergraph = [&](const IntergraphLink& link) {
        const auto* source_node = FindNode(link.source.graph, link.source.node);
        const auto* source_pin = FindPin(link.source.graph, link.source.pin);
        const auto* destination_node = FindNode(link.destination.graph, link.destination.node);
        const auto* destination_pin = FindPin(link.destination.graph, link.destination.pin);
        return source_node != nullptr && source_pin != nullptr && destination_node != nullptr &&
               destination_pin != nullptr && link.source.graph != link.destination.graph &&
               source_node->role == NodeRole::IntergraphOutput && destination_node->role == NodeRole::IntergraphInput &&
               source_pin->node == source_node->id && destination_pin->node == destination_node->id &&
               source_pin->direction == PinDirection::Input && destination_pin->direction == PinDirection::Output &&
               source_pin->type == destination_pin->type && source_pin->kind == destination_pin->kind;
    };

    std::optional<Error> error;
    before.nodes.ForEachDifference(
        graph->nodes, [&](const NodeId id, const NodeInstance* old_node, const NodeInstance* node) {
            if (error) return;
            if (old_node != nullptr) {
                for (const PinId pin_id : old_node->pins) {
                    const auto* retained_pin = FindPin(graph->id, pin_id);
                    if (retained_pin != nullptr) {
                        const auto* owner = FindNode(graph->id, retained_pin->node);
                        if (owner == nullptr || !std::ranges::contains(owner->pins, pin_id)) {
                            error = MakeError(ErrorCode::InvalidGraph, "Replacement leaves a detached pin");
                            return;
                        }
                        for (const LinkId link : IncidentLinks(pin_id)) {
                            if (auto valid = ValidateLocalLinkStructure(graph->id, link); !valid) {
                                error = std::move(valid.error());
                                return;
                            }
                        }
                    }
                }
            }
            if (node == nullptr) {
                if (FindNodeGraph(id)) error = MakeError(ErrorCode::InvalidGraph, "Removed node remains indexed");
                return;
            }
            if (!id || node->id != id || FindNodeGraph(id) != graph->id || node->type.Empty() ||
                node->type_version == 0 || !ValidRole(node->role) || !ValidProperties(node->properties) ||
                ((node->role == NodeRole::Subgraph) != node->subgraph.has_value())) {
                error = MakeError(ErrorCode::InvalidGraph, "Replacement node metadata is invalid");
                return;
            }
            std::unordered_set<PinId, IdHash> pins;
            std::unordered_set<std::string> keys;
            for (const PinId pin_id : node->pins) {
                const auto* pin = FindPin(graph->id, pin_id);
                if (pin == nullptr || pin->node != id || !pins.insert(pin_id).second || !keys.insert(pin->key).second) {
                    error = MakeError(ErrorCode::InvalidGraph, "Replacement node pin ownership is invalid");
                    return;
                }
                if (const IntergraphLinkId channel = IntergraphLinkForPin(pin_id)) {
                    const auto* link = FindIntergraphLink(channel);
                    if (link == nullptr || !validate_intergraph(*link)) {
                        error = MakeError(ErrorCode::InvalidGraph, "Replacement invalidates an intergraph endpoint");
                        return;
                    }
                }
                for (const LinkId link : IncidentLinks(pin_id)) {
                    if (auto valid = ValidateLocalLinkStructure(graph->id, link); !valid) {
                        error = std::move(valid.error());
                        return;
                    }
                }
            }
            if (node->role == NodeRole::BoundaryInput || node->role == NodeRole::BoundaryOutput) {
                if (node->subgraph || !ProjectionMatches(*graph, *node, graph->interface, node->role)) {
                    error = MakeError(ErrorCode::InvalidGraph, "Replacement boundary projection is invalid");
                    return;
                }
            }
            if (!node->subgraph) return;
            if (!ValidOwnership(node->subgraph->ownership)) {
                error = MakeError(ErrorCode::InvalidGraph, "Replacement subgraph ownership is invalid");
                return;
            }
            const GraphInterface* interface = SubgraphInterface(*this, *node->subgraph);
            if (interface == nullptr || !ValidInterface(*interface) ||
                !ProjectionMatches(*graph, *node, *interface, NodeRole::Subgraph)) {
                error = MakeError(ErrorCode::InvalidGraph, "Replacement subgraph projection is invalid");
                return;
            }
            if (const auto* local = std::get_if<DocumentGraphTarget>(&node->subgraph->target)) {
                const auto* target = FindGraph(local->graph);
                const bool owned = node->subgraph->ownership == SubgraphOwnership::Owned;
                if (target == nullptr || target == graph || (owned && target->lifetime != GraphLifetime::Owned) ||
                    (!owned && target->lifetime != GraphLifetime::Reusable)) {
                    error = MakeError(ErrorCode::InvalidGraph, "Replacement subgraph target is invalid");
                    return;
                }
                if (auto valid = ValidateTargetOwnership(local->graph); !valid) {
                    error = std::move(valid.error());
                }
            } else if (node->subgraph->ownership != SubgraphOwnership::Referenced ||
                       std::get<GraphAssetTarget>(node->subgraph->target).asset.Empty()) {
                error = MakeError(ErrorCode::InvalidGraph, "Replacement graph asset reference is invalid");
            }
        });
    if (error) return std::unexpected(std::move(*error));

    before.pins.ForEachDifference(graph->pins, [&](const PinId id, const PinInstance* old_pin, const PinInstance* pin) {
        if (error) return;
        if (pin == nullptr) {
            if (FindPinOwner(id)) error = MakeError(ErrorCode::InvalidGraph, "Removed pin remains indexed");
        } else {
            const auto owner = FindPinOwner(id);
            const auto* node = owner ? FindNode(owner->graph, owner->node) : nullptr;
            if (!id || pin->id != id || !owner || owner->graph != graph->id || owner->node != pin->node ||
                node == nullptr || !std::ranges::contains(node->pins, id) || pin->key.empty() || pin->type.Empty() ||
                !ValidDirection(pin->direction) || !ValidKind(pin->kind) || !ValidCardinality(pin->cardinality) ||
                !ValidStorage(pin->storage)) {
                error = MakeError(ErrorCode::InvalidGraph, "Replacement pin metadata is invalid");
            }
        }
        const NodeId owner_node = pin != nullptr ? pin->node : old_pin != nullptr ? old_pin->node : NodeId{};
        if (!error && owner_node) {
            if (auto valid = ValidateNodeRelations(graph->id, owner_node); !valid) {
                error = std::move(valid.error());
            }
        }
        if (!error) {
            for (const LinkId link : IncidentLinks(id)) {
                if (auto valid = ValidateLocalLinkStructure(graph->id, link); !valid) {
                    error = std::move(valid.error());
                    return;
                }
            }
        }
    });
    if (error) return std::unexpected(std::move(*error));

    before.links.ForEachDifference(graph->links, [&](const LinkId id, const Link*, const Link* link) {
        if (error) return;
        if (link == nullptr) {
            if (FindLinkGraph(id)) error = MakeError(ErrorCode::InvalidGraph, "Removed link remains indexed");
            return;
        }
        const auto* output = FindPin(graph->id, link->output);
        const auto* input = FindPin(graph->id, link->input);
        if (!id || link->id != id || FindLinkGraph(id) != graph->id || output == nullptr || input == nullptr ||
            output->direction != PinDirection::Output || input->direction != PinDirection::Input ||
            output->kind != input->kind) {
            error = MakeError(ErrorCode::InvalidGraph, "Replacement link endpoints are invalid");
            return;
        }
        if (FindLinkBetween(link->output, link->input) != link->id ||
            (output->cardinality == PinCardinality::Single && IncidentLinks(output->id).size() > 1) ||
            (input->cardinality == PinCardinality::Single && IncidentLinks(input->id).size() > 1)) {
            error = MakeError(ErrorCode::InvalidGraph, "Replacement link cardinality is invalid");
        }
    });
    if (error) return std::unexpected(std::move(*error));

    if (before.interface != graph->interface) {
        const auto& inputs = BoundaryNodes(graph->id, NodeRole::BoundaryInput);
        const auto& outputs = BoundaryNodes(graph->id, NodeRole::BoundaryOutput);
        if ((!graph->interface.pins.empty() || !inputs.empty() || !outputs.empty()) &&
            (inputs.size() != 1 || outputs.size() != 1)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Replacement graph interface requires "
                                                                      "exactly one input and output boundary"));
        }
        for (const SubgraphCallSite& caller : SubgraphCallers(graph->id)) {
            const auto* caller_graph = FindGraph(caller.graph);
            const auto* caller_node = FindNode(caller.graph, caller.node);
            if (caller_graph == nullptr || caller_node == nullptr ||
                !ProjectionMatches(*caller_graph, *caller_node, graph->interface, NodeRole::Subgraph)) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Replacement graph interface is not "
                                                                          "synchronized with every call site"));
            }
        }
    }
    if (before.lifetime != graph->lifetime) {
        const std::size_t owners = OwnedSubgraphCallerCount(graph->id);
        if ((graph->lifetime == GraphLifetime::Owned && owners != 1) ||
            (graph->lifetime == GraphLifetime::Reusable && owners != 0)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Replacement graph owner count is invalid"));
        }
    }
    return {};
}

} // namespace Uni::GUI::Nodes
