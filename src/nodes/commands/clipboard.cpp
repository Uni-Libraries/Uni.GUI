#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {
namespace {

using CommandDetail::Deduplicate;
using CommandDetail::Finite;
using CommandDetail::MakeError;
using CommandDetail::OwnedClosure;
using CommandDetail::ValidGraphInterface;

[[nodiscard]] bool ValidProperties(const PropertyBag& properties) noexcept {
    return std::ranges::all_of(properties, [](const auto& entry) {
        if (entry.first.empty()) return false;
        return std::visit(
            [](const auto& value) {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, double>) {
                    return std::isfinite(value);
                } else if constexpr (std::same_as<Value, Vec2>) {
                    return Finite(value);
                } else if constexpr (std::same_as<Value, OpaqueJsonProperty>) {
                    return Detail::ValidOpaqueJsonProperty(value.canonical_json);
                } else {
                    return true;
                }
            },
            entry.second);
    });
}

[[nodiscard]] bool ValidPinMetadata(const PinInstance& pin) noexcept {
    const bool direction = pin.direction == PinDirection::Input || pin.direction == PinDirection::Output;
    const bool kind = pin.kind == PinKind::Data || pin.kind == PinKind::Execution;
    const bool cardinality = pin.cardinality == PinCardinality::Single || pin.cardinality == PinCardinality::Multiple;
    const bool storage = pin.storage == PinStorage::Static || pin.storage == PinStorage::Dynamic;
    return !pin.key.empty() && !pin.type.Empty() && direction && kind && cardinality && storage;
}

[[nodiscard]] bool ValidNodePresentation(const NodePresentation& value) noexcept {
    return Finite(value.position) && Finite(value.size) && value.size.x >= 0.0f && value.size.y >= 0.0f;
}

[[nodiscard]] bool ValidLinkPresentation(const LinkPresentation& value) noexcept {
    std::unordered_set<RoutePointId, IdHash> ids;
    return std::ranges::all_of(value.Route(), [&](const RoutePoint& point) {
        return point.id && Finite(point.position) && ids.insert(point.id).second;
    });
}

} // namespace

Result<GraphFragment> CaptureGraphFragment(const GraphDocument& document, const GraphPresentation& presentation,
                                           const GraphSelection& selection) {
    const auto* graph = document.FindGraph(selection.graph);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Selection graph does not exist"));
    }

    std::unordered_set<NodeId, IdHash> selected_nodes;
    for (const NodeId node : selection.nodes) {
        if (!graph->nodes.contains(node)) {
            return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Selected node does not exist"));
        }
        selected_nodes.insert(node);
    }

    std::vector<GroupId> selected_groups = selection.groups;
    Deduplicate(selected_groups);
    for (const GroupId group_id : selected_groups) {
        const auto* group = presentation.FindGroup(group_id);
        if (group == nullptr || group->graph != selection.graph) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Selected group does not exist in the graph"));
        }
        selected_nodes.insert(group->members.begin(), group->members.end());
    }

    if (selected_nodes.empty() && selected_groups.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Selection contains no copyable elements"));
    }

    GraphFragment fragment;
    std::vector<NodeId> ordered_nodes(selected_nodes.begin(), selected_nodes.end());
    std::ranges::sort(ordered_nodes, {}, &NodeId::Value);
    bool has_origin = false;
    auto include_origin = [&](const Vec2 position) {
        if (!has_origin) {
            fragment.origin = position;
            has_origin = true;
        } else {
            fragment.origin.x = std::min(fragment.origin.x, position.x);
            fragment.origin.y = std::min(fragment.origin.y, position.y);
        }
    };
    for (const NodeId node_id : ordered_nodes) {
        const auto& node = graph->nodes.at(node_id);
        GraphFragmentNode copy{
            .creation = NodeCreation{.node = node},
            .presentation =
                presentation.FindNode(node_id) != nullptr ? *presentation.FindNode(node_id) : NodePresentation{},
        };
        for (const PinId pin : node.pins) {
            copy.creation.pins.push_back(graph->pins.at(pin));
        }
        include_origin(copy.presentation.position);
        fragment.nodes.push_back(std::move(copy));
    }

    std::vector<LinkId> ordered_links;
    for (const auto& [link_id, link] : graph->links) {
        const auto output = graph->pins.find(link.output);
        const auto input = graph->pins.find(link.input);
        if (output != graph->pins.end() && input != graph->pins.end() && selected_nodes.contains(output->second.node) &&
            selected_nodes.contains(input->second.node)) {
            ordered_links.push_back(link_id);
        }
    }
    std::ranges::sort(ordered_links, {}, &LinkId::Value);
    for (const LinkId link_id : ordered_links) {
        fragment.links.push_back(GraphFragmentLink{
            .link = graph->links.at(link_id),
            .presentation = presentation.FindLink(link_id) != nullptr
                                ? std::optional<LinkPresentation>{*presentation.FindLink(link_id)}
                                : std::nullopt,
        });
    }

    std::ranges::sort(selected_groups, {}, &GroupId::Value);
    for (const GroupId group_id : selected_groups) {
        fragment.groups.push_back(*presentation.FindGroup(group_id));
        include_origin(fragment.groups.back().geometry.position);
    }

    std::unordered_set<GraphId, IdHash> owned_graph_ids;
    for (const NodeId node_id : ordered_nodes) {
        for (const GraphId owned : OwnedClosure(document, graph->nodes.at(node_id))) {
            if (!owned_graph_ids.insert(owned).second) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Selected owned subgraph closures overlap"));
            }
            const auto* child = document.FindGraph(owned);
            if (child == nullptr) {
                return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Owned subgraph does not exist"));
            }
            GraphFragment::OwnedGraph copy{.graph = *child};
            for (const auto& [child_node, value] : child->nodes) {
                (void)value;
                if (const auto* state = presentation.FindNode(child_node)) {
                    copy.nodes.emplace(child_node, *state);
                }
            }
            for (const auto& [child_link, value] : child->links) {
                (void)value;
                if (const auto* state = presentation.FindLink(child_link)) {
                    copy.links.emplace(child_link, *state);
                }
            }
            for (const auto& [group_id, state] : presentation.Groups()) {
                (void)group_id;
                if (state.graph == owned) {
                    copy.groups.push_back(state);
                }
            }
            fragment.owned_graphs.push_back(std::move(copy));
        }
    }
    for (const auto& [link_id, link] : document.IntergraphLinks()) {
        (void)link_id;
        if (owned_graph_ids.contains(link.source.graph) && owned_graph_ids.contains(link.destination.graph)) {
            fragment.intergraph_links.push_back(link);
        }
    }
    return fragment;
}

Result<PreparedGraphFragment> PrepareGraphFragmentPaste(GraphDocument& document, GraphPresentation& presentation,
                                                        const RegistryCatalog& registry, const GraphFragment& fragment,
                                                        const GraphId graph, const Vec2 position) {
    const RegistrySnapshot snapshot = registry.Snapshot();
    const auto* target_graph = document.FindGraph(graph);
    if (target_graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Paste target graph does not exist"));
    }
    if (target_graph->read_only) {
        return std::unexpected(MakeError(ErrorCode::ReadOnly, "Paste target graph is read-only"));
    }
    if ((fragment.nodes.empty() && fragment.groups.empty()) || !Finite(position)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Graph fragment or paste position is invalid"));
    }

    std::unordered_set<NodeId, IdHash> source_nodes;
    std::unordered_set<PinId, IdHash> source_pins;
    std::unordered_set<LinkId, IdHash> source_links;
    std::unordered_set<GroupId, IdHash> source_groups;
    std::unordered_set<RoutePointId, IdHash> source_route_points;
    std::unordered_set<GraphId, IdHash> source_owned_graphs;
    for (const auto& owned : fragment.owned_graphs) {
        if (!owned.graph.id || owned.graph.lifetime != GraphLifetime::Owned ||
            !source_owned_graphs.insert(owned.graph.id).second) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Graph fragment contains an invalid owned graph"));
        }
        std::unordered_set<PinId, IdHash> referenced_pins;
        for (const auto& [node_id, node] : owned.graph.nodes) {
            if (!node_id || node.id != node_id || node.type.Empty() || node.type_version == 0 ||
                !ValidProperties(node.properties) || !source_nodes.insert(node_id).second) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph contains an invalid node"));
            }
            if (node.subgraph && node.subgraph->ownership != SubgraphOwnership::Owned &&
                node.subgraph->ownership != SubgraphOwnership::Referenced) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph has invalid ownership"));
            }
            std::unordered_set<std::string> keys;
            for (const PinId pin_id : node.pins) {
                const auto pin = owned.graph.pins.find(pin_id);
                if (pin == owned.graph.pins.end() || pin->second.node != node_id ||
                    !referenced_pins.insert(pin_id).second || !keys.insert(pin->second.key).second) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Owned fragment graph has inconsistent node pins"));
                }
            }
        }
        for (const auto& [pin_id, pin] : owned.graph.pins) {
            if (!pin_id || pin.id != pin_id || !ValidPinMetadata(pin) || !owned.graph.nodes.contains(pin.node) ||
                !referenced_pins.contains(pin_id) || !source_pins.insert(pin_id).second) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph contains an invalid pin"));
            }
        }
        std::set<std::pair<PinId, PinId>> link_endpoints;
        std::unordered_map<PinId, std::size_t, IdHash> link_counts;
        for (const auto& [link_id, link] : owned.graph.links) {
            const auto output = owned.graph.pins.find(link.output);
            const auto input = owned.graph.pins.find(link.input);
            if (!link_id || link.id != link_id || !source_links.insert(link_id).second ||
                output == owned.graph.pins.end() || input == owned.graph.pins.end() ||
                output->second.direction != PinDirection::Output || input->second.direction != PinDirection::Input ||
                output->second.kind != input->second.kind ||
                snapshot.Check(output->second.type, input->second.type, output->second.kind).status !=
                    ConnectionResult::Status::Allowed ||
                !link_endpoints.insert({link.output, link.input}).second) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph contains an invalid link"));
            }
            ++link_counts[link.output];
            ++link_counts[link.input];
        }
        for (const auto& [pin_id, count] : link_counts) {
            if (owned.graph.pins.at(pin_id).cardinality == PinCardinality::Single && count > 1) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph exceeds pin cardinality"));
            }
        }
        for (const auto& group : owned.groups) {
            if (!group.id || group.graph != owned.graph.id || !group.style || !Finite(group.geometry.position) ||
                !Finite(group.geometry.size) || group.geometry.size.x < 0.0f || group.geometry.size.y < 0.0f ||
                (group.style->kind != GroupKind::Group && group.style->kind != GroupKind::Comment) ||
                !source_groups.insert(group.id).second ||
                !std::ranges::all_of(
                    group.members, [&](const NodeId member) { return owned.graph.nodes.contains(member); })) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph contains an invalid group"));
            }
        }
        for (const auto& [node_id, state] : owned.nodes) {
            if (!owned.graph.nodes.contains(node_id) || !ValidNodePresentation(state)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph contains invalid node presentation"));
            }
        }
        for (const auto& [link_id, state] : owned.links) {
            if (!owned.graph.links.contains(link_id) || !ValidLinkPresentation(state)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Owned fragment graph contains invalid link presentation"));
            }
            for (const auto& point : state.Route()) {
                if (!source_route_points.insert(point.id).second) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Fragment contains duplicate route point IDs"));
                }
            }
        }
    }
    std::unordered_map<PinId, const PinInstance*, IdHash> pins_by_id;
    for (const auto& source : fragment.nodes) {
        bool invalid_subgraph = false;
        if (source.creation.node.subgraph) {
            if (source.creation.node.subgraph->ownership != SubgraphOwnership::Owned &&
                source.creation.node.subgraph->ownership != SubgraphOwnership::Referenced) {
                invalid_subgraph = true;
            } else if (const auto* local = std::get_if<DocumentGraphTarget>(&source.creation.node.subgraph->target)) {
                invalid_subgraph = source.creation.node.subgraph->ownership == SubgraphOwnership::Owned
                                       ? !source_owned_graphs.contains(local->graph)
                                       : local->graph == graph || document.FindGraph(local->graph) == nullptr ||
                                             document.HasDependencyPath(local->graph, graph);
            } else {
                const auto& asset = std::get<GraphAssetTarget>(source.creation.node.subgraph->target);
                invalid_subgraph = source.creation.node.subgraph->ownership != SubgraphOwnership::Referenced ||
                                   asset.asset.Empty() || !ValidGraphInterface(asset.interface);
            }
        }
        if (!source.creation.node.id || source.creation.node.type.Empty() || source.creation.node.type_version == 0 ||
            !ValidProperties(source.creation.node.properties) || invalid_subgraph ||
            !Finite(source.presentation.position) || !ValidNodePresentation(source.presentation) ||
            !source_nodes.insert(source.creation.node.id).second ||
            source.creation.node.pins.size() != source.creation.pins.size()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph fragment contains an invalid node"));
        }
        std::unordered_set<std::string> semantic_keys;
        for (std::size_t index = 0; index < source.creation.pins.size(); ++index) {
            const auto& pin = source.creation.pins[index];
            if (!pin.id || pin.node != source.creation.node.id || source.creation.node.pins[index] != pin.id ||
                !ValidPinMetadata(pin) || !semantic_keys.insert(pin.key).second || !source_pins.insert(pin.id).second) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph fragment contains an invalid pin"));
            }
            pins_by_id.emplace(pin.id, &pin);
        }
        if (!Finite(source.presentation.position + (position - fragment.origin))) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pasted node position is out of range"));
        }
    }
    for (const auto& source : fragment.links) {
        const auto output = pins_by_id.find(source.link.output);
        const auto input = pins_by_id.find(source.link.input);
        if (!source.link.id || !source_links.insert(source.link.id).second || output == pins_by_id.end() ||
            input == pins_by_id.end() || output->second->direction != PinDirection::Output ||
            input->second->direction != PinDirection::Input || output->second->kind != input->second->kind ||
            snapshot.Check(output->second->type, input->second->type, output->second->kind).status !=
                ConnectionResult::Status::Allowed ||
            (source.presentation && !ValidLinkPresentation(*source.presentation))) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph fragment contains an invalid link"));
        }
        if (source.presentation) {
            for (const auto& point : source.presentation->Route()) {
                if (!source_route_points.insert(point.id).second ||
                    !Finite(point.position + (position - fragment.origin))) {
                    return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                     "Graph fragment contains a duplicate or invalid route point"));
                }
            }
        }
    }
    std::set<std::pair<PinId, PinId>> endpoints;
    std::unordered_map<PinId, std::size_t, IdHash> connection_counts;
    for (const auto& source : fragment.links) {
        const auto* output = pins_by_id.at(source.link.output);
        const auto* input = pins_by_id.at(source.link.input);
        if (!endpoints.insert({source.link.output, source.link.input}).second ||
            (output->cardinality == PinCardinality::Single && connection_counts[source.link.output] != 0) ||
            (input->cardinality == PinCardinality::Single && connection_counts[source.link.input] != 0)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph fragment contains duplicate "
                                                                      "links or exceeds pin cardinality"));
        }
        ++connection_counts[source.link.output];
        ++connection_counts[source.link.input];
    }
    for (const auto& source : fragment.groups) {
        std::unordered_set<NodeId, IdHash> members;
        if (!source.id || !source.graph || !source.style || !source_groups.insert(source.id).second ||
            !Finite(source.geometry.position) || !Finite(source.geometry.size) || source.geometry.size.x < 0.0f ||
            source.geometry.size.y < 0.0f ||
            (source.style->kind != GroupKind::Group && source.style->kind != GroupKind::Comment) ||
            !Finite(source.geometry.position + (position - fragment.origin)) ||
            !std::ranges::all_of(
                source.members,
                [&](const NodeId member) { return source_nodes.contains(member) && members.insert(member).second; })) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph fragment contains an invalid group"));
        }
    }
    std::unordered_set<IntergraphLinkId, IdHash> source_intergraph_links;
    for (const auto& link : fragment.intergraph_links) {
        if (!link.id || !source_intergraph_links.insert(link.id).second ||
            !source_owned_graphs.contains(link.source.graph) || !source_owned_graphs.contains(link.destination.graph) ||
            !source_nodes.contains(link.source.node) || !source_nodes.contains(link.destination.node) ||
            !source_pins.contains(link.source.pin) || !source_pins.contains(link.destination.pin)) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Graph fragment contains an invalid intergraph link"));
        }
    }

    {
        std::unordered_map<GraphId, std::size_t, IdHash> indegree;
        std::unordered_map<GraphId, std::vector<GraphId>, IdHash> edges;
        for (const auto& owned : fragment.owned_graphs)
            indegree.emplace(owned.graph.id, 0);
        for (const auto& owned : fragment.owned_graphs) {
            for (const auto& [node_id, node] : owned.graph.nodes) {
                (void)node_id;
                if (!node.subgraph) continue;
                if (const auto* local = std::get_if<DocumentGraphTarget>(&node.subgraph->target)) {
                    if (node.subgraph->ownership == SubgraphOwnership::Owned) {
                        if (!source_owned_graphs.contains(local->graph)) {
                            return std::unexpected(
                                MakeError(ErrorCode::InvalidGraph, "Owned fragment graph dependency is missing"));
                        }
                        edges[owned.graph.id].push_back(local->graph);
                        ++indegree[local->graph];
                    } else if (document.FindGraph(local->graph) == nullptr) {
                        return std::unexpected(
                            MakeError(ErrorCode::InvalidGraph, "Referenced fragment graph dependency does not exist"));
                    }
                } else {
                    const auto& asset = std::get<GraphAssetTarget>(node.subgraph->target);
                    if (node.subgraph->ownership != SubgraphOwnership::Referenced || asset.asset.Empty() ||
                        !ValidGraphInterface(asset.interface)) {
                        return std::unexpected(
                            MakeError(ErrorCode::InvalidGraph, "Owned fragment graph asset dependency is invalid"));
                    }
                }
            }
        }
        std::vector<GraphId> pending;
        for (const auto& [id, count] : indegree)
            if (count == 0) pending.push_back(id);
        std::size_t visited = 0;
        while (!pending.empty()) {
            const GraphId id = pending.back();
            pending.pop_back();
            ++visited;
            for (const GraphId child : edges[id])
                if (--indegree[child] == 0) pending.push_back(child);
        }
        if (visited != fragment.owned_graphs.size()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned fragment graph closure is recursive"));
        }
    }

    PreparedGraphFragment prepared{.graph = graph};
    const auto remember_descriptor = [&](const TypeId& type) {
        const NodeTypeDescriptorPtr descriptor = snapshot.Find(type);
        if (!descriptor ||
            std::ranges::any_of(prepared.prepared_descriptors,
                                [&](const NodeTypeDescriptorPtr& existing) { return existing->type == type; })) {
            return descriptor;
        }
        prepared.prepared_descriptors.push_back(descriptor);
        return descriptor;
    };
    const Vec2 offset = position - fragment.origin;
    for (const auto& owned : fragment.owned_graphs) {
        const GraphId mapped = document.AllocateGraphId();
        if (!mapped) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Graph IDs are exhausted"));
        }
        prepared.remap.graphs.emplace(owned.graph.id, mapped);
    }
    for (const auto& source : fragment.nodes) {
        const NodeId node = document.AllocateNodeId();
        if (!node) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node IDs are exhausted"));
        }
        prepared.remap.nodes.emplace(source.creation.node.id, node);
        for (const auto& pin : source.creation.pins) {
            const PinId mapped = document.AllocatePinId();
            if (!mapped) {
                return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pin IDs are exhausted"));
            }
            prepared.remap.pins.emplace(pin.id, mapped);
        }
    }
    for (const auto& source : fragment.links) {
        const LinkId link = document.AllocateLinkId();
        if (!link) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link IDs are exhausted"));
        }
        prepared.remap.links.emplace(source.link.id, link);
        if (source.presentation) {
            for (const auto& point : source.presentation->Route()) {
                const RoutePointId mapped = presentation.AllocateRoutePointId();
                if (!mapped) {
                    return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point IDs are exhausted"));
                }
                prepared.remap.route_points.emplace(point.id, mapped);
            }
        }
    }
    for (const auto& source : fragment.groups) {
        const GroupId group_id = presentation.AllocateGroupId();
        if (!group_id) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group IDs are exhausted"));
        }
        prepared.remap.groups.emplace(source.id, group_id);
    }
    for (const auto& owned : fragment.owned_graphs) {
        for (const auto& [node_id, node] : owned.graph.nodes) {
            (void)node;
            const NodeId mapped = document.AllocateNodeId();
            if (!mapped) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node IDs are exhausted"));
            prepared.remap.nodes.emplace(node_id, mapped);
        }
        for (const auto& [pin_id, pin] : owned.graph.pins) {
            (void)pin;
            const PinId mapped = document.AllocatePinId();
            if (!mapped) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pin IDs are exhausted"));
            prepared.remap.pins.emplace(pin_id, mapped);
        }
        for (const auto& [link_id, link] : owned.graph.links) {
            (void)link;
            const LinkId mapped = document.AllocateLinkId();
            if (!mapped) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link IDs are exhausted"));
            prepared.remap.links.emplace(link_id, mapped);
        }
        for (const auto& group : owned.groups) {
            const GroupId mapped = presentation.AllocateGroupId();
            if (!mapped) return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Group IDs are exhausted"));
            prepared.remap.groups.emplace(group.id, mapped);
        }
        for (const auto& [link_id, state] : owned.links) {
            (void)link_id;
            for (const auto& point : state.Route()) {
                const RoutePointId mapped = presentation.AllocateRoutePointId();
                if (!mapped) {
                    return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Route point IDs are exhausted"));
                }
                prepared.remap.route_points.emplace(point.id, mapped);
            }
        }
    }
    for (const auto& link : fragment.intergraph_links) {
        const IntergraphLinkId mapped = document.AllocateIntergraphLinkId();
        if (!mapped) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Intergraph link IDs are exhausted"));
        }
        prepared.remap.intergraph_links.emplace(link.id, mapped);
    }

    const auto remap_subgraph = [&](std::optional<SubgraphReference>& reference) -> Result<void> {
        if (!reference || reference->ownership != SubgraphOwnership::Owned) {
            return {};
        }
        auto* local = std::get_if<DocumentGraphTarget>(&reference->target);
        if (local == nullptr || !prepared.remap.graphs.contains(local->graph)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned subgraph is missing from the fragment"));
        }
        local->graph = prepared.remap.graphs.at(local->graph);
        return {};
    };

    for (const auto& source : fragment.nodes) {
        auto copy = source;
        copy.creation.prepared_descriptor = remember_descriptor(copy.creation.node.type);
        copy.creation.node.id = prepared.remap.nodes.at(source.creation.node.id);
        if (auto remapped = remap_subgraph(copy.creation.node.subgraph); !remapped)
            return std::unexpected(remapped.error());
        copy.creation.node.pins.clear();
        for (auto& pin : copy.creation.pins) {
            pin.id = prepared.remap.pins.at(pin.id);
            pin.node = copy.creation.node.id;
            copy.creation.node.pins.push_back(pin.id);
        }
        copy.presentation.position = copy.presentation.position + offset;
        prepared.fragment.nodes.push_back(std::move(copy));
    }
    for (const auto& source : fragment.links) {
        auto copy = source;
        copy.link.id = prepared.remap.links.at(source.link.id);
        copy.link.output = prepared.remap.pins.at(source.link.output);
        copy.link.input = prepared.remap.pins.at(source.link.input);
        if (copy.presentation) {
            auto route = copy.presentation->Route().ToVector();
            for (auto& point : route) {
                point.id = prepared.remap.route_points.at(point.id);
                point.position = point.position + offset;
            }
            copy.presentation =
                LinkPresentation{copy.presentation->Style(), PersistentRoutePointSequence{std::move(route)}};
        }
        prepared.fragment.links.push_back(std::move(copy));
    }
    for (const auto& source : fragment.groups) {
        auto copy = source;
        copy.id = prepared.remap.groups.at(source.id);
        copy.graph = graph;
        copy.geometry.position = copy.geometry.position + offset;
        GroupMemberSet remapped_members;
        for (const NodeId member : copy.members) {
            const auto mapped = prepared.remap.nodes.find(member);
            if (mapped == prepared.remap.nodes.end()) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Group member is missing from the fragment"));
            }
            remapped_members.Insert(mapped->second);
        }
        copy.members = std::move(remapped_members);
        prepared.fragment.groups.push_back(std::move(copy));
    }
    for (const auto& source : fragment.owned_graphs) {
        GraphFragment::OwnedGraph copy;
        copy.graph = source.graph;
        copy.graph.id = prepared.remap.graphs.at(source.graph.id);
        NodeMap nodes;
        PinMap pins;
        LinkMap links;
        for (const auto& [node_id, source_node] : source.graph.nodes) {
            auto node = source_node;
            (void)remember_descriptor(node.type);
            node.id = prepared.remap.nodes.at(node_id);
            if (auto remapped = remap_subgraph(node.subgraph); !remapped) return std::unexpected(remapped.error());
            for (auto& pin_id : node.pins)
                pin_id = prepared.remap.pins.at(pin_id);
            nodes.emplace(node.id, std::move(node));
        }
        for (const auto& [pin_id, source_pin] : source.graph.pins) {
            auto pin = source_pin;
            pin.id = prepared.remap.pins.at(pin_id);
            pin.node = prepared.remap.nodes.at(source_pin.node);
            pins.emplace(pin.id, std::move(pin));
        }
        for (const auto& [link_id, source_link] : source.graph.links) {
            auto link = source_link;
            link.id = prepared.remap.links.at(link_id);
            link.output = prepared.remap.pins.at(source_link.output);
            link.input = prepared.remap.pins.at(source_link.input);
            links.emplace(link.id, std::move(link));
        }
        copy.graph.nodes = std::move(nodes);
        copy.graph.pins = std::move(pins);
        copy.graph.links = std::move(links);
        for (const auto& [node_id, state] : source.nodes) {
            copy.nodes.emplace(prepared.remap.nodes.at(node_id), state);
        }
        for (const auto& [link_id, source_state] : source.links) {
            auto route = source_state.Route().ToVector();
            for (auto& point : route)
                point.id = prepared.remap.route_points.at(point.id);
            LinkPresentation state{source_state.Style(), PersistentRoutePointSequence{std::move(route)}};
            copy.links.emplace(prepared.remap.links.at(link_id), std::move(state));
        }
        for (const auto& source_group : source.groups) {
            auto group_copy = source_group;
            group_copy.id = prepared.remap.groups.at(source_group.id);
            group_copy.graph = copy.graph.id;
            GroupMemberSet remapped_members;
            for (const NodeId member : group_copy.members) {
                remapped_members.Insert(prepared.remap.nodes.at(member));
            }
            group_copy.members = std::move(remapped_members);
            copy.groups.push_back(std::move(group_copy));
        }
        prepared.fragment.owned_graphs.push_back(std::move(copy));
    }
    {
        std::unordered_map<GraphId, std::size_t, IdHash> indices;
        std::unordered_map<GraphId, std::size_t, IdHash> indegree;
        std::unordered_map<GraphId, std::vector<GraphId>, IdHash> edges;
        for (std::size_t index = 0; index < prepared.fragment.owned_graphs.size(); ++index) {
            const GraphId id = prepared.fragment.owned_graphs[index].graph.id;
            indices.emplace(id, index);
            indegree.emplace(id, 0);
        }
        for (const auto& owned : prepared.fragment.owned_graphs) {
            for (const auto& [node_id, node] : owned.graph.nodes) {
                (void)node_id;
                if (!node.subgraph || node.subgraph->ownership != SubgraphOwnership::Owned) continue;
                const auto child = Detail::LocalSubgraph(node.subgraph);
                if (!child || !indices.contains(*child)) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Owned graph closure contains a missing dependency"));
                }
                edges[owned.graph.id].push_back(*child);
                ++indegree[*child];
            }
        }
        std::vector<GraphId> pending;
        for (const auto& [id, count] : indegree) {
            if (count == 0) pending.push_back(id);
        }
        std::ranges::sort(pending, std::greater{}, &GraphId::Value);
        std::vector<std::size_t> order;
        while (!pending.empty()) {
            const GraphId id = pending.back();
            pending.pop_back();
            order.push_back(indices.at(id));
            for (const GraphId child : edges[id]) {
                if (--indegree[child] == 0) {
                    pending.push_back(child);
                    std::ranges::sort(pending, std::greater{}, &GraphId::Value);
                }
            }
        }
        if (order.size() != prepared.fragment.owned_graphs.size()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned graph closure is recursive"));
        }
        std::vector<GraphFragment::OwnedGraph> sorted;
        sorted.reserve(order.size());
        for (const std::size_t index : order) {
            sorted.push_back(std::move(prepared.fragment.owned_graphs[index]));
        }
        prepared.fragment.owned_graphs = std::move(sorted);
    }
    for (const auto& source : fragment.intergraph_links) {
        auto copy = source;
        copy.id = prepared.remap.intergraph_links.at(source.id);
        copy.source.graph = prepared.remap.graphs.at(source.source.graph);
        copy.source.node = prepared.remap.nodes.at(source.source.node);
        copy.source.pin = prepared.remap.pins.at(source.source.pin);
        copy.destination.graph = prepared.remap.graphs.at(source.destination.graph);
        copy.destination.node = prepared.remap.nodes.at(source.destination.node);
        copy.destination.pin = prepared.remap.pins.at(source.destination.pin);
        prepared.fragment.intergraph_links.push_back(std::move(copy));
    }
    prepared.fragment.origin = position;
    return prepared;
}

struct PasteGraphFragmentCommand::Impl final {
    PreparedGraphFragment prepared;
};

PasteGraphFragmentCommand::PasteGraphFragmentCommand(PreparedGraphFragment fragment)
    : m_impl(std::make_unique<Impl>(Impl{std::move(fragment)})) {}
PasteGraphFragmentCommand::~PasteGraphFragmentCommand() = default;
std::string_view PasteGraphFragmentCommand::Name() const noexcept {
    return "Paste graph fragment";
}
Result<void> PasteGraphFragmentCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) {
    if (m_impl->prepared.fragment.nodes.empty() && m_impl->prepared.fragment.groups.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Graph fragment is empty"));
    }
    for (const NodeTypeDescriptorPtr& prepared : m_impl->prepared.prepared_descriptors) {
        if (!prepared) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidArgument, "Prepared graph fragment contains an empty node descriptor"));
        }
        if (!registry.Find(prepared->type)) {
            return std::unexpected(MakeError(ErrorCode::TypeNotFound, "A node descriptor used to prepare this "
                                                                      "fragment is no longer registered"));
        }
    }
    for (const auto& node : m_impl->prepared.fragment.nodes) {
        if (auto valid = CommandDetail::ValidateNodeCreationSchema(registry, node.creation.node, node.creation.pins,
                                                                   node.creation.prepared_descriptor);
            !valid) {
            return valid;
        }
    }
    for (const auto& graph : m_impl->prepared.fragment.owned_graphs) {
        for (const auto& [node_id, node] : graph.graph.nodes) {
            (void)node_id;
            std::vector<PinInstance> pins;
            pins.reserve(node.pins.size());
            for (const PinId pin : node.pins)
                pins.push_back(graph.graph.pins.at(pin));
            const auto prepared = std::ranges::find_if(
                m_impl->prepared.prepared_descriptors,
                [&](const NodeTypeDescriptorPtr& descriptor) { return descriptor->type == node.type; });
            const NodeTypeDescriptorPtr expected =
                prepared != m_impl->prepared.prepared_descriptors.end() ? *prepared : NodeTypeDescriptorPtr{};
            if (auto valid = CommandDetail::ValidateNodeCreationSchema(registry, node, pins, expected); !valid) {
                return valid;
            }
        }
    }
    for (auto graph = m_impl->prepared.fragment.owned_graphs.rbegin();
         graph != m_impl->prepared.fragment.owned_graphs.rend(); ++graph) {
        auto semantic = graph->graph;
        semantic.read_only = false;
        std::vector<NodeId> node_ids;
        node_ids.reserve(semantic.nodes.size());
        for (const auto& [node_id, node] : semantic.nodes) {
            (void)node;
            node_ids.push_back(node_id);
        }
        for (const NodeId node_id : node_ids) {
            auto node = semantic.nodes.at(node_id);
            node.read_only = false;
            semantic.nodes.insert_or_assign(node_id, std::move(node));
        }
        std::vector<PinId> pin_ids;
        pin_ids.reserve(semantic.pins.size());
        for (const auto& [pin_id, pin] : semantic.pins) {
            (void)pin;
            pin_ids.push_back(pin_id);
        }
        for (const PinId pin_id : pin_ids) {
            auto pin = semantic.pins.at(pin_id);
            pin.read_only = false;
            semantic.pins.insert_or_assign(pin_id, std::move(pin));
        }
        std::vector<LinkId> link_ids;
        link_ids.reserve(semantic.links.size());
        for (const auto& [link_id, link] : semantic.links) {
            (void)link;
            link_ids.push_back(link_id);
        }
        for (const LinkId link_id : link_ids) {
            auto link = semantic.links.at(link_id);
            link.read_only = false;
            semantic.links.insert_or_assign(link_id, std::move(link));
        }
        if (auto added = transaction.AddGraph(std::move(semantic)); !added) {
            return std::unexpected(std::move(added.error()));
        }
        for (const auto& [node, state] : graph->nodes) {
            auto unlocked = state;
            unlocked.locked = false;
            if (auto result = transaction.SetNodePresentation(node, unlocked); !result) return result;
        }
        for (const auto& [link, state] : graph->links) {
            auto style = state.Style();
            style.locked = false;
            LinkPresentation unlocked{std::move(style), state.Route()};
            if (auto result = transaction.SetLinkPresentation(link, unlocked); !result) return result;
        }
        for (const auto& group : graph->groups) {
            auto unlocked = group;
            unlocked.protection.locked = false;
            if (auto result = transaction.AddGroup(std::move(unlocked)); !result) return result;
        }
    }
    for (const auto& node : m_impl->prepared.fragment.nodes) {
        auto creation = node.creation;
        creation.node.read_only = false;
        for (auto& pin : creation.pins)
            pin.read_only = false;
        auto presentation = node.presentation;
        presentation.locked = false;
        if (auto result = transaction.AddNode(m_impl->prepared.graph, std::move(creation.node), creation.pins);
            !result) {
            return result;
        }
        if (auto result = transaction.SetNodePresentation(node.creation.node.id, std::move(presentation)); !result) {
            return result;
        }
    }
    for (const auto& link : m_impl->prepared.fragment.links) {
        auto semantic = link.link;
        semantic.read_only = false;
        auto presentation = link.presentation;
        if (presentation) {
            auto style = presentation->Style();
            style.locked = false;
            presentation = LinkPresentation{std::move(style), presentation->Route()};
        }
        if (auto result = transaction.AddLink(m_impl->prepared.graph, std::move(semantic)); !result) {
            return result;
        }
        if (auto result = transaction.SetLinkPresentation(link.link.id, std::move(presentation)); !result) {
            return result;
        }
    }
    for (const auto& group : m_impl->prepared.fragment.groups) {
        if (auto result = transaction.AddGroup(group); !result) {
            return result;
        }
    }
    for (const auto& link : m_impl->prepared.fragment.intergraph_links) {
        if (auto result = transaction.AddIntergraphLink(link); !result) return result;
    }
    for (const auto& link : m_impl->prepared.fragment.links) {
        if (link.link.read_only) {
            if (auto result = transaction.SetLinkReadOnly(m_impl->prepared.graph, link.link.id, true); !result)
                return result;
        }
    }
    for (const auto& graph : m_impl->prepared.fragment.owned_graphs) {
        for (const auto& [link_id, link] : graph.graph.links) {
            if (link.read_only) {
                if (auto result = transaction.SetLinkReadOnly(graph.graph.id, link_id, true); !result) return result;
            }
        }
        for (const auto& [pin_id, pin] : graph.graph.pins) {
            if (pin.read_only) {
                if (auto result = transaction.SetPinReadOnly(graph.graph.id, pin_id, true); !result) return result;
            }
        }
        for (const auto& [node_id, node] : graph.graph.nodes) {
            if (node.read_only) {
                if (auto result = transaction.SetNodeReadOnly(graph.graph.id, node_id, true); !result) return result;
            }
        }
        for (const auto& [node, state] : graph.nodes) {
            if (state.locked) {
                if (auto result = transaction.SetNodeLocked(node, true); !result) return result;
            }
        }
        for (const auto& [link, state] : graph.links) {
            if (state.Style().locked) {
                if (auto result = transaction.SetLinkLocked(link, true); !result) return result;
            }
        }
        for (const auto& group : graph.groups) {
            if (group.protection.locked) {
                if (auto result = transaction.SetGroupLocked(group.id, true); !result) return result;
            }
        }
        if (graph.graph.read_only) {
            if (auto result = transaction.SetGraphReadOnly(graph.graph.id, true); !result) return result;
        }
    }
    for (const auto& node : m_impl->prepared.fragment.nodes) {
        for (const auto& pin : node.creation.pins) {
            if (pin.read_only) {
                if (auto result = transaction.SetPinReadOnly(m_impl->prepared.graph, pin.id, true); !result)
                    return result;
            }
        }
        if (node.creation.node.read_only) {
            if (auto result = transaction.SetNodeReadOnly(m_impl->prepared.graph, node.creation.node.id, true); !result)
                return result;
        }
        if (node.presentation.locked) {
            if (auto result = transaction.SetNodeLocked(node.creation.node.id, true); !result) return result;
        }
    }
    for (const auto& link : m_impl->prepared.fragment.links) {
        if (link.presentation && link.presentation->Style().locked) {
            if (auto result = transaction.SetLinkLocked(link.link.id, true); !result) return result;
        }
    }
    return {};
}
Result<void> PasteGraphFragmentCommand::Revert(GraphTransaction& transaction) {
    for (auto link = m_impl->prepared.fragment.intergraph_links.rbegin();
         link != m_impl->prepared.fragment.intergraph_links.rend(); ++link) {
        if (auto removed = transaction.RemoveIntergraphLink(link->id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (auto group = m_impl->prepared.fragment.groups.rbegin(); group != m_impl->prepared.fragment.groups.rend();
         ++group) {
        if (auto removed = transaction.RemoveGroup(group->id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (auto link = m_impl->prepared.fragment.links.rbegin(); link != m_impl->prepared.fragment.links.rend(); ++link) {
        if (auto result = transaction.SetLinkPresentation(link->link.id, std::nullopt); !result) {
            return result;
        }
        if (auto removed = transaction.RemoveLink(m_impl->prepared.graph, link->link.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (auto node = m_impl->prepared.fragment.nodes.rbegin(); node != m_impl->prepared.fragment.nodes.rend(); ++node) {
        if (auto result = transaction.SetNodePresentation(node->creation.node.id, std::nullopt); !result) {
            return result;
        }
        if (auto removed = transaction.RemoveNode(m_impl->prepared.graph, node->creation.node.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    for (const auto& graph : m_impl->prepared.fragment.owned_graphs) {
        for (auto group = graph.groups.rbegin(); group != graph.groups.rend(); ++group) {
            if (auto removed = transaction.RemoveGroup(group->id); !removed) {
                return std::unexpected(std::move(removed.error()));
            }
        }
        for (const auto& [link, state] : graph.links) {
            (void)state;
            if (auto result = transaction.SetLinkPresentation(link, std::nullopt); !result) return result;
        }
        for (const auto& [node, state] : graph.nodes) {
            (void)state;
            if (auto result = transaction.SetNodePresentation(node, std::nullopt); !result) return result;
        }
        if (auto removed = transaction.RemoveGraph(graph.graph.id); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
    }
    return {};
}

} // namespace Uni::GUI::Nodes
