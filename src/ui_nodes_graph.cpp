#include <uni/gui/nodes/graph.h>

#include "ui_nodes_internal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

std::atomic<std::uint64_t> NextDocumentIdentity{1};

struct AtomicCopyMetrics final {
    std::atomic<std::uint64_t> root_clones{0};
    std::atomic<std::uint64_t> directory_clones{0};
    std::atomic<std::uint64_t> shard_clones{0};
    std::atomic<std::uint64_t> page_clones{0};
    std::atomic<std::uint64_t> value_clones{0};
    std::atomic<std::uint64_t> copied_handles{0};
    std::atomic<std::uint64_t> logical_bytes{0};
};

AtomicCopyMetrics GraphCopies;
AtomicCopyMetrics GraphRevisionCopies;
AtomicCopyMetrics NodeMapCopies;
AtomicCopyMetrics PinMapCopies;
AtomicCopyMetrics LinkMapCopies;
AtomicCopyMetrics IntergraphLinkCopies;
AtomicCopyMetrics NodePresentationCopies;
AtomicCopyMetrics LinkPresentationCopies;
AtomicCopyMetrics GroupCopies;
AtomicCopyMetrics GroupStyleCopies;
AtomicCopyMetrics GroupMembershipCopies;
AtomicCopyMetrics RoutePointSequenceCopies;
AtomicCopyMetrics SemanticIndexCopies;
AtomicCopyMetrics PresentationIndexCopies;
std::atomic<std::uint64_t> JournalEntries{0};
std::atomic<std::uint64_t> OperationIntents{0};
std::atomic<std::uint64_t> CommandPaths{0};
std::atomic<std::uint64_t> IncrementalRecordsValidated{0};
std::atomic<std::uint64_t> FullStructureValidations{0};
std::atomic<std::uint64_t> OwnershipSummaryLookups{0};
std::atomic<std::uint64_t> DependencySearches{0};
std::atomic<std::uint64_t> DependencyVerticesVisited{0};
std::atomic<std::uint64_t> RouteChunkMerges{0};
std::atomic<std::uint64_t> RoutePointsReindexed{0};

[[nodiscard]] std::uint64_t AllocateIdentity() noexcept {
    std::uint64_t identity = NextDocumentIdentity.fetch_add(1, std::memory_order_relaxed);
    if (identity == 0) {
        identity = NextDocumentIdentity.fetch_add(1, std::memory_order_relaxed);
    }
    return identity;
}

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

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
    if (interface.version == 0) {
        return false;
    }
    std::unordered_set<std::string> keys;
    return std::ranges::all_of(interface.pins, [&](const GraphInterfacePin& pin) {
        return !pin.key.empty() && !pin.type.Empty() && ValidDirection(pin.direction) && ValidKind(pin.kind) &&
               ValidCardinality(pin.caller_cardinality) && ValidCardinality(pin.boundary_cardinality) &&
               keys.insert(pin.key).second;
    });
}

[[nodiscard]] const GraphInterface* SubgraphInterface(const GraphMap& graphs,
                                                      const SubgraphReference& reference) noexcept {
    if (const auto* local = std::get_if<DocumentGraphTarget>(&reference.target)) {
        const auto found = graphs.find(local->graph);
        return found != graphs.end() ? &found->second.interface : nullptr;
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
        if (found == graph.pins.end()) {
            return false;
        }
        if (found->second.storage == PinStorage::Dynamic) {
            actual.push_back(&found->second);
        }
    }
    if (actual.size() != expected.size()) {
        return false;
    }
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

[[nodiscard]] bool ProjectionMatchesCreation(const NodeInstance& node, const std::span<const PinInstance> pins,
                                             const GraphInterface& interface) {
    std::vector<const GraphInterfacePin*> expected;
    expected.reserve(interface.pins.size());
    for (const auto& pin : interface.pins)
        expected.push_back(&pin);

    std::vector<const PinInstance*> actual;
    actual.reserve(pins.size());
    for (const PinId pin_id : node.pins) {
        const auto found = std::ranges::find(pins, pin_id, &PinInstance::id);
        if (found == pins.end()) return false;
        if (found->storage == PinStorage::Dynamic) actual.push_back(&*found);
    }
    if (actual.size() != expected.size()) return false;
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto& source = *expected[index];
        const auto& projected = *actual[index];
        if (projected.key != source.key || projected.label != source.label || projected.type != source.type ||
            projected.direction != source.direction || projected.kind != source.kind ||
            projected.cardinality != source.caller_cardinality) {
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

template<typename Id, typename Value>
[[nodiscard]] bool ContainsId(const GraphMap& graphs, const Id id, const Value Graph::* member) {
    return std::ranges::any_of(graphs, [id, member](const auto& entry) { return (entry.second.*member).contains(id); });
}

} // namespace

TransactionMetrics GetTransactionMetrics() noexcept {
    const auto load = [](const AtomicCopyMetrics& value) {
        return CopyDomainMetrics{
            .root_clones = value.root_clones.load(std::memory_order_relaxed),
            .directory_clones = value.directory_clones.load(std::memory_order_relaxed),
            .shard_clones = value.shard_clones.load(std::memory_order_relaxed),
            .page_clones = value.page_clones.load(std::memory_order_relaxed),
            .value_clones = value.value_clones.load(std::memory_order_relaxed),
            .copied_handles = value.copied_handles.load(std::memory_order_relaxed),
            .logical_bytes = value.logical_bytes.load(std::memory_order_relaxed),
        };
    };
    TransactionMetrics result{
        .graphs = load(GraphCopies),
        .graph_revisions = load(GraphRevisionCopies),
        .node_maps = load(NodeMapCopies),
        .pin_maps = load(PinMapCopies),
        .link_maps = load(LinkMapCopies),
        .intergraph_links = load(IntergraphLinkCopies),
        .node_presentations = load(NodePresentationCopies),
        .link_presentations = load(LinkPresentationCopies),
        .groups = load(GroupCopies),
        .group_styles = load(GroupStyleCopies),
        .group_memberships = load(GroupMembershipCopies),
        .route_point_sequences = load(RoutePointSequenceCopies),
        .semantic_indexes = load(SemanticIndexCopies),
        .presentation_indexes = load(PresentationIndexCopies),
        .journal_entries = JournalEntries.load(std::memory_order_relaxed),
        .operation_intents = OperationIntents.load(std::memory_order_relaxed),
        .command_paths = CommandPaths.load(std::memory_order_relaxed),
        .incremental_records_validated = IncrementalRecordsValidated.load(std::memory_order_relaxed),
        .full_structure_validations = FullStructureValidations.load(std::memory_order_relaxed),
        .ownership_summary_lookups = OwnershipSummaryLookups.load(std::memory_order_relaxed),
        .dependency_searches = DependencySearches.load(std::memory_order_relaxed),
        .dependency_vertices_visited = DependencyVerticesVisited.load(std::memory_order_relaxed),
        .route_chunk_merges = RouteChunkMerges.load(std::memory_order_relaxed),
        .route_points_reindexed = RoutePointsReindexed.load(std::memory_order_relaxed),
    };
    result.copied_logical_bytes = result.graphs.logical_bytes + result.graph_revisions.logical_bytes +
                                  result.node_maps.logical_bytes + result.pin_maps.logical_bytes +
                                  result.link_maps.logical_bytes + result.intergraph_links.logical_bytes +
                                  result.node_presentations.logical_bytes + result.link_presentations.logical_bytes +
                                  result.groups.logical_bytes + result.group_styles.logical_bytes +
                                  result.group_memberships.logical_bytes + result.route_point_sequences.logical_bytes +
                                  result.semantic_indexes.logical_bytes + result.presentation_indexes.logical_bytes;
    return result;
}

void ResetTransactionMetrics() noexcept {
    const auto reset = [](AtomicCopyMetrics& value) {
        value.root_clones.store(0, std::memory_order_relaxed);
        value.directory_clones.store(0, std::memory_order_relaxed);
        value.shard_clones.store(0, std::memory_order_relaxed);
        value.page_clones.store(0, std::memory_order_relaxed);
        value.value_clones.store(0, std::memory_order_relaxed);
        value.copied_handles.store(0, std::memory_order_relaxed);
        value.logical_bytes.store(0, std::memory_order_relaxed);
    };
    reset(GraphCopies);
    reset(GraphRevisionCopies);
    reset(NodeMapCopies);
    reset(PinMapCopies);
    reset(LinkMapCopies);
    reset(IntergraphLinkCopies);
    reset(NodePresentationCopies);
    reset(LinkPresentationCopies);
    reset(GroupCopies);
    reset(GroupStyleCopies);
    reset(GroupMembershipCopies);
    reset(RoutePointSequenceCopies);
    reset(SemanticIndexCopies);
    reset(PresentationIndexCopies);
    JournalEntries.store(0, std::memory_order_relaxed);
    OperationIntents.store(0, std::memory_order_relaxed);
    CommandPaths.store(0, std::memory_order_relaxed);
    IncrementalRecordsValidated.store(0, std::memory_order_relaxed);
    FullStructureValidations.store(0, std::memory_order_relaxed);
    OwnershipSummaryLookups.store(0, std::memory_order_relaxed);
    DependencySearches.store(0, std::memory_order_relaxed);
    DependencyVerticesVisited.store(0, std::memory_order_relaxed);
    RouteChunkMerges.store(0, std::memory_order_relaxed);
    RoutePointsReindexed.store(0, std::memory_order_relaxed);
}

namespace {

void RecordClone(AtomicCopyMetrics& metrics, const Detail::CowCloneKind kind, const std::uint64_t copied_handles,
                 const std::uint64_t logical_bytes) noexcept {
    switch (kind) {
    case Detail::CowCloneKind::Root:
        metrics.root_clones.fetch_add(1, std::memory_order_relaxed);
        break;
    case Detail::CowCloneKind::Directory:
        metrics.directory_clones.fetch_add(1, std::memory_order_relaxed);
        break;
    case Detail::CowCloneKind::Shard:
        metrics.shard_clones.fetch_add(1, std::memory_order_relaxed);
        break;
    case Detail::CowCloneKind::Value:
        metrics.value_clones.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    metrics.copied_handles.fetch_add(copied_handles, std::memory_order_relaxed);
    metrics.logical_bytes.fetch_add(logical_bytes, std::memory_order_relaxed);
}

} // namespace

void Detail::RecordCowClone(const CowCopyDomain domain, const CowCloneKind kind, const std::uint64_t copied_handles,
                            const std::uint64_t logical_bytes) noexcept {
    switch (domain) {
    case CowCopyDomain::None:
        break;
    case CowCopyDomain::Graphs:
        RecordClone(GraphCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::GraphRevisions:
        RecordClone(GraphRevisionCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::Nodes:
        RecordClone(NodeMapCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::Pins:
        RecordClone(PinMapCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::Links:
        RecordClone(LinkMapCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::IntergraphLinks:
        RecordClone(IntergraphLinkCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::NodePresentations:
        RecordClone(NodePresentationCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::LinkPresentations:
        RecordClone(LinkPresentationCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::Groups:
        RecordClone(GroupCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::GroupStyles:
        RecordClone(GroupStyleCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::GroupMemberships:
        RecordClone(GroupMembershipCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::RoutePointSequences:
        RecordClone(RoutePointSequenceCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::PresentationIndexes:
        RecordClone(PresentationIndexCopies, kind, copied_handles, logical_bytes);
        break;
    case CowCopyDomain::SemanticIndexes:
        RecordClone(SemanticIndexCopies, kind, copied_handles, logical_bytes);
        break;
    }
}

void Detail::RecordJournalEntries(const std::uint64_t entries) noexcept {
    JournalEntries.fetch_add(entries, std::memory_order_relaxed);
}

void Detail::RecordOperationIntent() noexcept {
    OperationIntents.fetch_add(1, std::memory_order_relaxed);
}

void Detail::RecordCommandPath() noexcept {
    CommandPaths.fetch_add(1, std::memory_order_relaxed);
}

void Detail::RecordIncrementalValidation(const std::uint64_t records) noexcept {
    IncrementalRecordsValidated.fetch_add(records, std::memory_order_relaxed);
}

void Detail::RecordRouteCompaction(const std::uint64_t chunk_merges, const std::uint64_t points_reindexed) noexcept {
    RouteChunkMerges.fetch_add(chunk_merges, std::memory_order_relaxed);
    RoutePointsReindexed.fetch_add(points_reindexed, std::memory_order_relaxed);
}

void Detail::RecordFullStructureValidation() noexcept {
    FullStructureValidations.fetch_add(1, std::memory_order_relaxed);
}

using GraphTable = GraphMap;

namespace {

template<typename Tag> [[nodiscard]] StrongId<Tag> AdjacencyKey(const StrongId<Tag> value) noexcept {
    return value;
}

[[nodiscard]] NodeId AdjacencyKey(const SubgraphCallSite& value) noexcept {
    return value.node;
}

template<typename Index, typename Key, typename Value>
void AddIndexValues(Index& index, const Key key, const std::span<const Value> additions) {
    typename Index::mapped_type values;
    if (const auto found = index.find(key); found != index.end()) values = found->second;
    for (const auto& addition : additions) {
        values.insert_or_assign(AdjacencyKey(addition), addition);
    }
    index.insert_or_assign(key, std::move(values));
}

template<typename Index, typename Key, typename Value>
void AddIndexValue(Index& index, const Key key, const Value value) {
    AddIndexValues(index, key, std::span<const Value>{&value, 1});
}

template<typename Index, typename Key, typename Value>
void RemoveIndexValue(Index& index, const Key key, const Value value) {
    const auto found = index.find(key);
    if (found == index.end()) return;
    typename Index::mapped_type values = found->second;
    values.erase(AdjacencyKey(value));
    if (values.empty())
        index.erase(key);
    else
        index.insert_or_assign(key, std::move(values));
}

} // namespace

struct GraphDocument::Impl final {
    struct Indexes final {
        struct DependencyTarget final {
            GraphId graph;
            std::size_t references{0};
            bool operator==(const DependencyTarget&) const = default;
        };

        using NodeOwners = CowEntityMap<NodeId, GraphId, Detail::CowCopyDomain::SemanticIndexes>;
        using PinOwners = CowEntityMap<PinId, PinOwner, Detail::CowCopyDomain::SemanticIndexes>;
        using LinkOwners = CowEntityMap<LinkId, GraphId, Detail::CowCopyDomain::SemanticIndexes>;
        using PinIncidence = CowEntityMap<PinId, IncidentLinkRange, Detail::CowCopyDomain::SemanticIndexes>;
        using InputConnections = CowAdjacencyMap<PinId, LinkId>;
        using LocalConnections = CowEntityMap<PinId, InputConnections, Detail::CowCopyDomain::SemanticIndexes>;
        struct CallerBucket final {
            SubgraphCallerRange callers;
            std::size_t owned_count{0};
            std::optional<SubgraphCallSite> owned_owner;

            bool operator==(const CallerBucket&) const = default;
        };
        using Callers = CowEntityMap<GraphId, CallerBucket, Detail::CowCopyDomain::SemanticIndexes>;
        using DependencyTargets = CowAdjacencyMap<GraphId, DependencyTarget>;
        using Dependencies = CowEntityMap<GraphId, DependencyTargets, Detail::CowCopyDomain::SemanticIndexes>;
        using IntergraphEndpoints = CowEntityMap<PinId, IntergraphLinkId, Detail::CowCopyDomain::SemanticIndexes>;
        using GraphIntergraphLinks = CowEntityMap<GraphId, IntergraphLinkRange, Detail::CowCopyDomain::SemanticIndexes>;
        using BoundaryNodes = CowEntityMap<GraphId, BoundaryNodeRange, Detail::CowCopyDomain::SemanticIndexes>;

        NodeOwners node_owners;
        PinOwners pin_owners;
        LinkOwners link_owners;
        PinIncidence pin_incidence;
        LocalConnections local_connections;
        Callers callers;
        Dependencies dependencies;
        IntergraphEndpoints intergraph_endpoints;
        GraphIntergraphLinks graph_intergraph_links;
        BoundaryNodes boundary_inputs;
        BoundaryNodes boundary_outputs;

        void AddCaller(const GraphId target, const SubgraphCallSite caller) {
            CallerBucket bucket;
            if (const auto found = callers.find(target); found != callers.end()) bucket = found->second;
            if (bucket.callers.insert_or_assign(caller.node, caller) && caller.ownership == SubgraphOwnership::Owned) {
                ++bucket.owned_count;
                bucket.owned_owner = caller;
            }
            callers.insert_or_assign(target, std::move(bucket));
        }

        void RemoveCaller(const GraphId target, const SubgraphCallSite caller) {
            const auto found = callers.find(target);
            if (found == callers.end()) return;
            CallerBucket bucket = found->second;
            const auto* current = bucket.callers.Find(caller.node);
            if (current != nullptr && current->ownership == SubgraphOwnership::Owned) {
                --bucket.owned_count;
                if (bucket.owned_count == 0) bucket.owned_owner.reset();
            }
            bucket.callers.erase(caller.node);
            if (bucket.callers.empty())
                callers.erase(target);
            else
                callers.insert_or_assign(target, std::move(bucket));
        }

        [[nodiscard]] std::size_t OwnedCallerCount(const GraphId target) const noexcept {
            OwnershipSummaryLookups.fetch_add(1, std::memory_order_relaxed);
            const auto found = callers.find(target);
            return found != callers.end() ? found->second.owned_count : 0;
        }

        [[nodiscard]] const SubgraphCallSite* OwnedCaller(const GraphId target) const noexcept {
            OwnershipSummaryLookups.fetch_add(1, std::memory_order_relaxed);
            const auto found = callers.find(target);
            return found != callers.end() && found->second.owned_owner ? &*found->second.owned_owner : nullptr;
        }

        void AddDependency(const GraphId source, const GraphId target) {
            DependencyTargets values;
            if (const auto found = dependencies.find(source); found != dependencies.end()) {
                values = found->second;
            }
            const auto* current = values.Find(target);
            values.insert_or_assign(target, DependencyTarget{
                                                .graph = target,
                                                .references = current != nullptr ? current->references + 1 : 1,
                                            });
            dependencies.insert_or_assign(source, std::move(values));
        }

        void RemoveDependency(const GraphId source, const GraphId target) {
            const auto found = dependencies.find(source);
            if (found == dependencies.end()) return;
            DependencyTargets values = found->second;
            const auto* current = values.Find(target);
            if (current == nullptr) return;
            if (current->references == 1)
                values.erase(target);
            else
                values.insert_or_assign(target, DependencyTarget{target, current->references - 1});
            if (values.empty())
                dependencies.erase(source);
            else
                dependencies.insert_or_assign(source, std::move(values));
        }

        [[nodiscard]] bool HasDependencyPath(const GraphId from, const GraphId target) const {
            if (!from || !target) return false;
            if (from == target) return true;
            DependencySearches.fetch_add(1, std::memory_order_relaxed);
            std::vector<GraphId> pending{from};
            std::unordered_set<GraphId, IdHash> visited;
            while (!pending.empty()) {
                const GraphId graph = pending.back();
                pending.pop_back();
                if (!visited.insert(graph).second) continue;
                DependencyVerticesVisited.fetch_add(1, std::memory_order_relaxed);
                const auto outgoing = dependencies.find(graph);
                if (outgoing == dependencies.end()) continue;
                for (const DependencyTarget& destination : outgoing->second) {
                    if (destination.graph == target) {
                        DependencyVerticesVisited.fetch_add(1, std::memory_order_relaxed);
                        return true;
                    }
                    pending.push_back(destination.graph);
                }
            }
            return false;
        }

        [[nodiscard]] bool HasDependencyEdge(const GraphId source, const GraphId target) const noexcept {
            const auto found = dependencies.find(source);
            return found != dependencies.end() && found->second.contains(target);
        }

        void AddNode(const GraphId graph, const NodeInstance& node) {
            node_owners.insert_or_assign(node.id, graph);
            if (node.role == NodeRole::BoundaryInput) AddIndexValue(boundary_inputs, graph, node.id);
            if (node.role == NodeRole::BoundaryOutput) AddIndexValue(boundary_outputs, graph, node.id);
            if (const auto target = Detail::LocalSubgraph(node.subgraph)) {
                AddCaller(*target, SubgraphCallSite{graph, node.id, node.subgraph->ownership});
                AddDependency(graph, *target);
            }
        }

        void RemoveNode(const GraphId graph, const NodeInstance& node) {
            if (node.role == NodeRole::BoundaryInput) RemoveIndexValue(boundary_inputs, graph, node.id);
            if (node.role == NodeRole::BoundaryOutput) RemoveIndexValue(boundary_outputs, graph, node.id);
            if (const auto target = Detail::LocalSubgraph(node.subgraph)) {
                RemoveCaller(*target, SubgraphCallSite{graph, node.id, node.subgraph->ownership});
                RemoveDependency(graph, *target);
            }
            node_owners.erase(node.id);
        }

        void AddPin(const GraphId graph, const PinInstance& pin) {
            pin_owners.insert_or_assign(pin.id, PinOwner{graph, pin.node});
        }

        void RemovePin(const PinId pin) {
            pin_incidence.erase(pin);
            pin_owners.erase(pin);
        }

        [[nodiscard]] LinkId FindLinkBetween(const PinId output, const PinId input) const noexcept {
            const auto found = local_connections.find(output);
            if (found == local_connections.end()) return {};
            const auto* link = found->second.Find(input);
            return link != nullptr ? *link : LinkId{};
        }

        void AddLink(const GraphId graph, const Link& link) {
            link_owners.insert_or_assign(link.id, graph);
            AddIndexValue(pin_incidence, link.output, link.id);
            AddIndexValue(pin_incidence, link.input, link.id);
            InputConnections connections;
            if (const auto found = local_connections.find(link.output); found != local_connections.end()) {
                connections = found->second;
            }
            connections.insert_or_assign(link.input, link.id);
            local_connections.insert_or_assign(link.output, std::move(connections));
        }

        void RemoveLink(const Link& link) {
            RemoveIndexValue(pin_incidence, link.output, link.id);
            RemoveIndexValue(pin_incidence, link.input, link.id);
            const auto found = local_connections.find(link.output);
            if (found != local_connections.end()) {
                InputConnections connections = found->second;
                connections.erase(link.input);
                if (connections.empty())
                    local_connections.erase(link.output);
                else
                    local_connections.insert_or_assign(link.output, std::move(connections));
            }
            link_owners.erase(link.id);
        }

        void AddGraph(const Graph& graph) {
            std::unordered_map<PinId, std::vector<LinkId>, IdHash> incidence;
            std::unordered_map<PinId, std::vector<std::pair<PinId, LinkId>>, IdHash> connections;
            std::unordered_map<GraphId, std::vector<SubgraphCallSite>, IdHash> added_callers;
            std::vector<GraphId> added_dependencies;
            std::vector<NodeId> inputs;
            std::vector<NodeId> outputs;
            for (const auto& [node_id, node] : graph.nodes) {
                (void)node_id;
                node_owners.insert_or_assign(node.id, graph.id);
                if (node.role == NodeRole::BoundaryInput) inputs.push_back(node.id);
                if (node.role == NodeRole::BoundaryOutput) outputs.push_back(node.id);
                if (const auto target = Detail::LocalSubgraph(node.subgraph)) {
                    added_callers[*target].push_back(SubgraphCallSite{graph.id, node.id, node.subgraph->ownership});
                    added_dependencies.push_back(*target);
                }
            }
            for (const auto& [pin_id, pin] : graph.pins) {
                (void)pin_id;
                pin_owners.insert_or_assign(pin.id, PinOwner{graph.id, pin.node});
            }
            for (const auto& [link_id, link] : graph.links) {
                (void)link_id;
                link_owners.insert_or_assign(link.id, graph.id);
                incidence[link.output].push_back(link.id);
                incidence[link.input].push_back(link.id);
                connections[link.output].emplace_back(link.input, link.id);
            }
            for (const auto& [pin, links] : incidence) {
                AddIndexValues(pin_incidence, pin, std::span<const LinkId>{links});
            }
            for (const auto& [output, values] : connections) {
                InputConnections indexed;
                for (const auto& [input, link] : values)
                    indexed.insert_or_assign(input, link);
                local_connections.emplace(output, std::move(indexed));
            }
            for (const auto& [target, values] : added_callers) {
                for (const auto& caller : values)
                    AddCaller(target, caller);
            }
            for (const GraphId target : added_dependencies)
                AddDependency(graph.id, target);
            if (!inputs.empty()) AddIndexValues(boundary_inputs, graph.id, std::span<const NodeId>{inputs});
            if (!outputs.empty()) AddIndexValues(boundary_outputs, graph.id, std::span<const NodeId>{outputs});
        }

        void RemoveGraph(const Graph& graph) {
            for (const auto& [link_id, link] : graph.links) {
                (void)link;
                link_owners.erase(link_id);
            }
            for (const auto& [pin_id, pin] : graph.pins) {
                (void)pin;
                pin_incidence.erase(pin_id);
                local_connections.erase(pin_id);
                pin_owners.erase(pin_id);
            }
            for (const auto& [node_id, node] : graph.nodes) {
                (void)node_id;
                RemoveNode(graph.id, node);
            }
            dependencies.erase(graph.id);
            boundary_inputs.erase(graph.id);
            boundary_outputs.erase(graph.id);
        }

        void AddIntergraph(const IntergraphLink& link) {
            intergraph_endpoints.insert_or_assign(link.source.pin, link.id);
            intergraph_endpoints.insert_or_assign(link.destination.pin, link.id);
            AddIndexValue(graph_intergraph_links, link.source.graph, link.id);
            AddIndexValue(graph_intergraph_links, link.destination.graph, link.id);
            AddDependency(link.source.graph, link.destination.graph);
        }

        void RemoveIntergraph(const IntergraphLink& link) {
            intergraph_endpoints.erase(link.source.pin);
            intergraph_endpoints.erase(link.destination.pin);
            RemoveIndexValue(graph_intergraph_links, link.source.graph, link.id);
            RemoveIndexValue(graph_intergraph_links, link.destination.graph, link.id);
            RemoveDependency(link.source.graph, link.destination.graph);
        }

        [[nodiscard]] static Indexes Build(const GraphTable& graphs, const IntergraphLinkMap& intergraph_links) {
            Indexes result;
            std::unordered_map<PinId, std::vector<LinkId>, IdHash> incidence;
            std::unordered_map<PinId, std::vector<std::pair<PinId, LinkId>>, IdHash> connections;
            std::unordered_map<GraphId, std::vector<SubgraphCallSite>, IdHash> caller_values;
            std::unordered_map<GraphId, std::vector<IntergraphLinkId>, IdHash> graph_channels;
            std::unordered_map<GraphId, std::vector<NodeId>, IdHash> input_values;
            std::unordered_map<GraphId, std::vector<NodeId>, IdHash> output_values;
            for (const auto& [graph_id, graph] : graphs) {
                for (const auto& [node_id, node] : graph.nodes) {
                    result.node_owners.emplace(node_id, graph_id);
                    if (node.role == NodeRole::BoundaryInput) input_values[graph_id].push_back(node_id);
                    if (node.role == NodeRole::BoundaryOutput) output_values[graph_id].push_back(node_id);
                    if (const auto target = Detail::LocalSubgraph(node.subgraph)) {
                        caller_values[*target].push_back(SubgraphCallSite{graph_id, node_id, node.subgraph->ownership});
                    }
                }
                for (const auto& [pin_id, pin] : graph.pins) {
                    result.pin_owners.emplace(pin_id, PinOwner{graph_id, pin.node});
                }
                for (const auto& [link_id, link] : graph.links) {
                    result.link_owners.emplace(link_id, graph_id);
                    incidence[link.output].push_back(link_id);
                    incidence[link.input].push_back(link_id);
                    connections[link.output].emplace_back(link.input, link_id);
                }
            }
            for (const auto& [link_id, link] : intergraph_links) {
                result.intergraph_endpoints.emplace(link.source.pin, link_id);
                result.intergraph_endpoints.emplace(link.destination.pin, link_id);
                graph_channels[link.source.graph].push_back(link_id);
                graph_channels[link.destination.graph].push_back(link_id);
            }
            for (const auto& [key, values] : incidence) {
                AddIndexValues(result.pin_incidence, key, std::span<const LinkId>{values});
            }
            for (const auto& [output, values] : connections) {
                InputConnections indexed;
                for (const auto& [input, link] : values)
                    indexed.insert_or_assign(input, link);
                result.local_connections.emplace(output, std::move(indexed));
            }
            for (const auto& [key, values] : caller_values) {
                for (const auto& caller : values)
                    result.AddCaller(key, caller);
            }
            for (auto& [key, values] : graph_channels) {
                AddIndexValues(result.graph_intergraph_links, key, std::span<const IntergraphLinkId>{values});
            }
            for (const auto& [key, values] : input_values) {
                AddIndexValues(result.boundary_inputs, key, std::span<const NodeId>{values});
            }
            for (const auto& [key, values] : output_values) {
                AddIndexValues(result.boundary_outputs, key, std::span<const NodeId>{values});
            }
            for (const auto& [graph_id, graph] : graphs) {
                for (const auto& [node_id, node] : graph.nodes) {
                    (void)node_id;
                    if (const auto target = Detail::LocalSubgraph(node.subgraph)) {
                        result.AddDependency(graph_id, *target);
                    }
                }
            }
            for (const auto& [link_id, link] : intergraph_links) {
                (void)link_id;
                result.AddDependency(link.source.graph, link.destination.graph);
            }
            return result;
        }

        [[nodiscard]] bool Equivalent(const Indexes& other) const {
            return node_owners == other.node_owners && pin_owners == other.pin_owners &&
                   link_owners == other.link_owners && pin_incidence == other.pin_incidence &&
                   local_connections == other.local_connections && callers == other.callers &&
                   dependencies == other.dependencies && intergraph_endpoints == other.intergraph_endpoints &&
                   graph_intergraph_links == other.graph_intergraph_links && boundary_inputs == other.boundary_inputs &&
                   boundary_outputs == other.boundary_outputs;
        }
    };

    std::uint32_t schema_version{1};
    GraphTable graphs;
    GraphRevisionMap graph_revisions;
    GraphId root_graph;
    IdGenerator graph_ids;
    IdGenerator node_ids;
    IdGenerator pin_ids;
    IdGenerator link_ids;
    IdGenerator intergraph_link_ids;
    IntergraphLinkMap intergraph_links;
    Indexes indexes;
    std::uint64_t model_revision{0};
    std::uint64_t topology_revision{0};
    std::uint64_t value_revision{0};
    std::uint64_t layout_revision{0};
    std::uint64_t allocation_epoch{0};
    std::uint64_t identity{AllocateIdentity()};
};

GraphDocument::GraphDocument() : m_impl(std::make_unique<Impl>()) {
    const auto root = AddGraph();
    if (root) {
        m_impl->root_graph = *root;
    }
}

GraphDocument::GraphDocument(SnapshotTag) : m_impl(std::make_unique<Impl>()) {}

GraphDocument::~GraphDocument() = default;

GraphDocument::GraphDocument(GraphDocument&& other) : m_impl(std::move(other.m_impl)) {
    other.m_impl = std::make_unique<Impl>();
    const auto root = other.AddGraph();
    if (root) {
        other.m_impl->root_graph = *root;
    }
}

GraphDocument& GraphDocument::operator=(GraphDocument&& other) {
    if (this == &other) {
        return *this;
    }
    auto replacement = std::make_unique<Impl>();
    m_impl = std::move(other.m_impl);
    other.m_impl = std::move(replacement);
    const auto root = other.AddGraph();
    if (root) {
        other.m_impl->root_graph = *root;
    }
    return *this;
}

void GraphDocument::Swap(GraphDocument& other) noexcept {
    m_impl.swap(other.m_impl);
}

std::uint32_t GraphDocument::SchemaVersion() const noexcept {
    return m_impl->schema_version;
}

void GraphDocument::SetSchemaVersion(const std::uint32_t version) noexcept {
    m_impl->schema_version = version == 0 ? 1 : version;
}

GraphId GraphDocument::RootGraph() const noexcept {
    return m_impl->root_graph;
}

std::uint64_t GraphDocument::ModelRevision() const noexcept {
    return m_impl->model_revision;
}

SemanticRevisionSet GraphDocument::SemanticRevisions() const noexcept {
    return {
        .serial = m_impl->model_revision,
        .topology = m_impl->topology_revision,
        .value = m_impl->value_revision,
        .layout = m_impl->layout_revision,
    };
}

SemanticRevisionSet GraphDocument::GraphRevisions(const GraphId graph) const noexcept {
    const auto state = m_impl->graph_revisions.find(graph);
    return state != m_impl->graph_revisions.end() ? state->second : SemanticRevisionSet{};
}

Result<void> GraphDocument::SetRootGraph(const GraphId graph) {
    const auto found = m_impl->graphs.find(graph);
    if (found == m_impl->graphs.end()) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Root graph does not exist"));
    }
    if (found->second.lifetime != GraphLifetime::Reusable) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "An owned graph cannot be the document root"));
    }
    m_impl->root_graph = graph;
    return {};
}

GraphId GraphDocument::AllocateGraphId() noexcept {
    if (m_impl->allocation_epoch == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const GraphId id = m_impl->graph_ids.Next<GraphId>();
    if (id) {
        ++m_impl->allocation_epoch;
    }
    return id;
}

NodeId GraphDocument::AllocateNodeId() noexcept {
    if (m_impl->allocation_epoch == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const NodeId id = m_impl->node_ids.Next<NodeId>();
    if (id) {
        ++m_impl->allocation_epoch;
    }
    return id;
}

PinId GraphDocument::AllocatePinId() noexcept {
    if (m_impl->allocation_epoch == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const PinId id = m_impl->pin_ids.Next<PinId>();
    if (id) {
        ++m_impl->allocation_epoch;
    }
    return id;
}

LinkId GraphDocument::AllocateLinkId() noexcept {
    if (m_impl->allocation_epoch == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const LinkId id = m_impl->link_ids.Next<LinkId>();
    if (id) {
        ++m_impl->allocation_epoch;
    }
    return id;
}

IntergraphLinkId GraphDocument::AllocateIntergraphLinkId() noexcept {
    if (m_impl->allocation_epoch == std::numeric_limits<std::uint64_t>::max()) {
        return {};
    }
    const IntergraphLinkId id = m_impl->intergraph_link_ids.Next<IntergraphLinkId>();
    if (id) {
        ++m_impl->allocation_epoch;
    }
    return id;
}

Result<GraphId> GraphDocument::AddGraph(GraphId id) {
    if (!id) {
        id = AllocateGraphId();
    }
    if (!id) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Graph ID space is exhausted"));
    }
    if (m_impl->graphs.contains(id)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Graph ID already exists"));
    }

    m_impl->graph_ids.Observe(id);
    m_impl->graphs.emplace(id, Graph{.id = id});
    m_impl->graph_revisions.emplace(id, SemanticRevisionSet{});
    return id;
}

Result<Graph> GraphDocument::RemoveGraph(const GraphId id) {
    const auto found = m_impl->graphs.find(id);
    if (found == m_impl->graphs.end()) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (id == m_impl->root_graph) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "The root graph cannot be removed"));
    }
    if (!SubgraphCallers(id).empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph is referenced by a subgraph node"));
    }
    if (!IntergraphLinksForGraph(id).empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph is referenced by an intergraph link"));
    }

    Graph removed = found->second;
    m_impl->indexes.RemoveGraph(removed);
    m_impl->graphs.erase(id);
    m_impl->graph_revisions.erase(id);
    return removed;
}

Result<void> GraphDocument::RestoreGraph(Graph graph) {
    if (!graph.id || !ValidLifetime(graph.lifetime) || !ValidInterface(graph.interface)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Graph ID cannot be zero"));
    }
    if (m_impl->graphs.contains(graph.id)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Graph ID already exists"));
    }

    std::unordered_set<PinId, IdHash> referenced_pins;
    std::unordered_map<GraphId, std::size_t, IdHash> candidate_owned_targets;
    std::size_t boundary_inputs = 0;
    std::size_t boundary_outputs = 0;
    for (const auto& [node_id, node] : graph.nodes) {
        if (!node_id || node.id != node_id || node.type.Empty() || node.type_version == 0 || !ValidRole(node.role) ||
            !ValidProperties(node.properties) || m_impl->indexes.node_owners.contains(node_id)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph contains an invalid or duplicate node"));
        }
        const auto local_subgraph = Detail::LocalSubgraph(node.subgraph);
        if (local_subgraph && (*local_subgraph == graph.id || !m_impl->graphs.contains(*local_subgraph))) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Node creates an invalid or cyclic subgraph reference"));
        }
        std::unordered_set<PinId, IdHash> node_pins;
        std::unordered_set<std::string> pin_keys;
        for (const auto pin_id : node.pins) {
            const auto pin = graph.pins.find(pin_id);
            if (!node_pins.insert(pin_id).second || pin == graph.pins.end() || pin->second.node != node_id ||
                !pin_keys.insert(pin->second.key).second) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node pin references are inconsistent"));
            }
            referenced_pins.insert(pin_id);
        }
        if ((node.role == NodeRole::Subgraph) != node.subgraph.has_value() ||
            (node.subgraph && !ValidOwnership(node.subgraph->ownership))) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Graph contains an invalid subgraph node role or ownership"));
        }
        if (node.role == NodeRole::BoundaryInput) {
            ++boundary_inputs;
            if (node.subgraph || !ProjectionMatches(graph, node, graph.interface, node.role)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Boundary input does not match the graph interface"));
            }
        } else if (node.role == NodeRole::BoundaryOutput) {
            ++boundary_outputs;
            if (node.subgraph || !ProjectionMatches(graph, node, graph.interface, node.role)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Boundary output does not match the graph interface"));
            }
        }
        if (!node.subgraph) continue;
        const GraphInterface* target_interface = SubgraphInterface(m_impl->graphs, *node.subgraph);
        if (target_interface == nullptr || !ValidInterface(*target_interface) ||
            !ProjectionMatches(graph, node, *target_interface, NodeRole::Subgraph)) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Subgraph node does not match the target interface"));
        }
        if (const auto* local = std::get_if<DocumentGraphTarget>(&node.subgraph->target)) {
            const auto* target = FindGraph(local->graph);
            const bool owned = node.subgraph->ownership == SubgraphOwnership::Owned;
            if (target == nullptr || (owned && target->lifetime != GraphLifetime::Owned) ||
                (!owned && target->lifetime != GraphLifetime::Reusable)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Subgraph ownership does not match target lifetime"));
            }
            if (owned) ++candidate_owned_targets[local->graph];
        } else {
            const auto& asset = std::get<GraphAssetTarget>(node.subgraph->target);
            if (node.subgraph->ownership != SubgraphOwnership::Referenced || asset.asset.Empty()) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset reference is invalid"));
            }
        }
    }
    const bool has_boundaries = boundary_inputs != 0 || boundary_outputs != 0;
    if ((has_boundaries || !graph.interface.pins.empty()) && (boundary_inputs != 1 || boundary_outputs != 1)) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Graph interface requires exactly one input and output boundary"));
    }
    for (const auto& [target, owners] : candidate_owned_targets) {
        const std::size_t existing_owners = m_impl->indexes.OwnedCallerCount(target);
        if (owners + existing_owners != 1) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned graph must have exactly one owner"));
        }
    }
    for (const auto& [pin_id, pin] : graph.pins) {
        if (!pin_id || pin.id != pin_id || !pin.node || pin.key.empty() || pin.type.Empty() ||
            !ValidDirection(pin.direction) || !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality) ||
            !ValidStorage(pin.storage) || !graph.nodes.contains(pin.node) || !referenced_pins.contains(pin_id) ||
            m_impl->indexes.pin_owners.contains(pin_id)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph contains an invalid or duplicate pin"));
        }
    }

    std::unordered_map<PinId, std::size_t, IdHash> connection_counts;
    std::set<std::pair<PinId, PinId>> connected_pairs;
    for (const auto& [link_id, link] : graph.links) {
        if (!link_id || link.id != link_id || m_impl->indexes.link_owners.contains(link_id)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph contains an invalid or duplicate link"));
        }
        const auto output = graph.pins.find(link.output);
        const auto input = graph.pins.find(link.input);
        if (output == graph.pins.end() || input == graph.pins.end() ||
            output->second.direction != PinDirection::Output || input->second.direction != PinDirection::Input ||
            output->second.kind != input->second.kind) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph link endpoints are invalid"));
        }
        if (!connected_pairs.insert({link.output, link.input}).second) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph contains duplicate links"));
        }
        ++connection_counts[link.output];
        ++connection_counts[link.input];
    }
    for (const auto& [pin_id, count] : connection_counts) {
        if (graph.pins.at(pin_id).cardinality == PinCardinality::Single && count > 1) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph exceeds pin cardinality"));
        }
    }

    const GraphId id = graph.id;
    m_impl->graph_ids.Observe(id);
    for (const auto& [node_id, node] : graph.nodes) {
        (void)node;
        m_impl->node_ids.Observe(node_id);
    }
    for (const auto& [pin_id, pin] : graph.pins) {
        (void)pin;
        m_impl->pin_ids.Observe(pin_id);
    }
    for (const auto& [link_id, link] : graph.links) {
        (void)link;
        m_impl->link_ids.Observe(link_id);
    }
    m_impl->indexes.AddGraph(graph);
    m_impl->graphs.emplace(id, std::move(graph));
    m_impl->graph_revisions.emplace(id, SemanticRevisionSet{});
    return {};
}

Result<void> GraphDocument::ReplaceGraph(Graph graph) {
    const auto found = m_impl->graphs.find(graph.id);
    if (!graph.id || found == m_impl->graphs.end()) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (!ValidLifetime(graph.lifetime) || !ValidInterface(graph.interface)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph metadata is invalid"));
    }
    auto candidate_indexes = m_impl->indexes;
    const Graph& before = found->second;
    std::vector<GraphId> added_dependency_targets;
    before.links.ForEachDifference(graph.links, [&](const LinkId, const Link* old_value, const Link*) {
        if (old_value != nullptr) candidate_indexes.RemoveLink(*old_value);
    });
    before.pins.ForEachDifference(graph.pins, [&](const PinId, const PinInstance* old_value, const PinInstance*) {
        if (old_value == nullptr) return;
        candidate_indexes.pin_owners.erase(old_value->id);
    });
    before.nodes.ForEachDifference(graph.nodes, [&](const NodeId, const NodeInstance* old_value, const NodeInstance*) {
        if (old_value != nullptr) candidate_indexes.RemoveNode(graph.id, *old_value);
    });

    bool duplicate_identity = false;
    bool duplicate_connection = false;
    before.nodes.ForEachDifference(
        graph.nodes, [&](const NodeId id, const NodeInstance* old_value, const NodeInstance* value) {
            if (value == nullptr) return;
            const auto owner = candidate_indexes.node_owners.find(id);
            duplicate_identity |= owner != candidate_indexes.node_owners.end() && owner->second != graph.id;
            const auto old_target = old_value != nullptr ? Detail::LocalSubgraph(old_value->subgraph) : std::nullopt;
            const auto new_target = Detail::LocalSubgraph(value->subgraph);
            if (new_target && new_target != old_target && !candidate_indexes.HasDependencyEdge(graph.id, *new_target)) {
                added_dependency_targets.push_back(*new_target);
            }
            if (!duplicate_identity) candidate_indexes.AddNode(graph.id, *value);
        });
    before.pins.ForEachDifference(graph.pins, [&](const PinId id, const PinInstance*, const PinInstance* value) {
        if (value == nullptr) return;
        const auto owner = candidate_indexes.pin_owners.find(id);
        duplicate_identity |= owner != candidate_indexes.pin_owners.end() && owner->second.graph != graph.id;
        if (!duplicate_identity) candidate_indexes.AddPin(graph.id, *value);
    });
    before.links.ForEachDifference(graph.links, [&](const LinkId id, const Link*, const Link* value) {
        if (value == nullptr) return;
        const auto owner = candidate_indexes.link_owners.find(id);
        duplicate_identity |= owner != candidate_indexes.link_owners.end() && owner->second != graph.id;
        const LinkId connected = candidate_indexes.FindLinkBetween(value->output, value->input);
        duplicate_connection |= connected && connected != id;
        if (!duplicate_identity && !duplicate_connection) candidate_indexes.AddLink(graph.id, *value);
    });
    if (duplicate_identity) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Replacement graph contains a global ID collision"));
    }
    if (duplicate_connection) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Replacement graph contains duplicate pin endpoints"));
    }
    if (std::ranges::any_of(added_dependency_targets, [&](const GraphId target) {
            return candidate_indexes.HasDependencyPath(target, graph.id);
        })) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph dependencies contain a cycle"));
    }
    before.nodes.ForEachDifference(graph.nodes, [&](const NodeId id, const NodeInstance*, const NodeInstance* value) {
        if (value != nullptr) m_impl->node_ids.Observe(id);
    });
    before.pins.ForEachDifference(graph.pins, [&](const PinId id, const PinInstance*, const PinInstance* value) {
        if (value != nullptr) m_impl->pin_ids.Observe(id);
    });
    before.links.ForEachDifference(graph.links, [&](const LinkId id, const Link*, const Link* value) {
        if (value != nullptr) m_impl->link_ids.Observe(id);
    });
    m_impl->graphs.insert_or_assign(graph.id, std::move(graph));
    m_impl->indexes = std::move(candidate_indexes);
    return {};
}

Result<void> GraphDocument::AddNode(const GraphId graph_id, NodeInstance node,
                                    const std::span<const PinInstance> pins) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (!node.id || node.type.Empty() || node.type_version == 0 || !ValidRole(node.role) ||
        !ValidProperties(node.properties) || ((node.role == NodeRole::Subgraph) != node.subgraph.has_value())) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node ID, type, and version are required"));
    }
    const auto local_subgraph = Detail::LocalSubgraph(node.subgraph);
    if (local_subgraph && (*local_subgraph == graph_id || !m_impl->graphs.contains(*local_subgraph) ||
                           (!m_impl->indexes.HasDependencyEdge(graph_id, *local_subgraph) &&
                            m_impl->indexes.HasDependencyPath(*local_subgraph, graph_id)))) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Node creates an invalid or cyclic subgraph reference"));
    }
    if (m_impl->indexes.node_owners.contains(node.id)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Node ID already exists"));
    }

    std::unordered_set<PinId, IdHash> supplied_ids;
    std::unordered_set<std::string> supplied_keys;
    std::vector<PinId> ordered_pins;
    ordered_pins.reserve(pins.size());
    for (const auto& pin : pins) {
        if (!pin.id || pin.node != node.id || pin.key.empty() || pin.type.Empty() || !ValidDirection(pin.direction) ||
            !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality) || !ValidStorage(pin.storage)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Node contains an invalid pin"));
        }
        if (!supplied_ids.insert(pin.id).second || m_impl->indexes.pin_owners.contains(pin.id)) {
            return std::unexpected(MakeError(ErrorCode::DuplicateId, "Pin ID already exists"));
        }
        if (!supplied_keys.insert(pin.key).second) {
            return std::unexpected(MakeError(ErrorCode::DuplicateId, "Pin semantic key already exists on node"));
        }
        ordered_pins.push_back(pin.id);
    }

    if (!node.pins.empty() && node.pins != ordered_pins) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Node pin order does not match the supplied pin instances"));
    }
    node.pins = std::move(ordered_pins);

    if (node.subgraph) {
        if (!ValidOwnership(node.subgraph->ownership)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph ownership is invalid"));
        }
        const GraphInterface* interface = SubgraphInterface(m_impl->graphs, *node.subgraph);
        if (interface == nullptr || !ValidInterface(*interface) || !ProjectionMatchesCreation(node, pins, *interface)) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Subgraph node pins do not match the target interface"));
        }
        if (const auto* local = std::get_if<DocumentGraphTarget>(&node.subgraph->target)) {
            const auto* target = FindGraph(local->graph);
            const bool owned = node.subgraph->ownership == SubgraphOwnership::Owned;
            if (target == nullptr || (owned && target->lifetime != GraphLifetime::Owned) ||
                (!owned && target->lifetime != GraphLifetime::Reusable)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Subgraph ownership does not match target graph lifetime"));
            }
            if (owned && m_impl->indexes.OwnedCallerCount(local->graph) != 0) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned graph already has an owner"));
            }
        } else if (node.subgraph->ownership != SubgraphOwnership::Referenced ||
                   std::get<GraphAssetTarget>(node.subgraph->target).asset.Empty()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph assets cannot be owned"));
        }
    }

    m_impl->node_ids.Observe(node.id);
    for (const auto& pin : pins) {
        m_impl->pin_ids.Observe(pin.id);
        graph->pins.emplace(pin.id, pin);
        m_impl->indexes.AddPin(graph_id, pin);
    }
    m_impl->indexes.AddNode(graph_id, node);
    graph->nodes.emplace(node.id, std::move(node));
    return {};
}

Result<RemovedNode> GraphDocument::RemoveNode(const GraphId graph_id, const NodeId node_id) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto found = graph->nodes.find(node_id);
    if (found == graph->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    for (const PinId pin : found->second.pins) {
        if (IntergraphLinkForPin(pin)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Intergraph links must be disconnected "
                                                                      "before removing their endpoint node"));
        }
    }

    RemovedNode removed;
    removed.node = std::move(found->second);
    std::unordered_set<PinId, IdHash> pin_ids(removed.node.pins.begin(), removed.node.pins.end());
    removed.pins.reserve(pin_ids.size());
    for (const auto pin_id : removed.node.pins) {
        const auto pin = graph->pins.find(pin_id);
        if (pin != graph->pins.end()) {
            removed.pins.push_back(std::move(pin->second));
            graph->pins.erase(pin);
        }
    }
    std::unordered_set<LinkId, IdHash> incident_links;
    for (const PinId pin : pin_ids) {
        const auto incidents = IncidentLinks(pin);
        incident_links.insert(incidents.begin(), incidents.end());
    }
    removed.links.reserve(incident_links.size());
    for (const LinkId link_id : incident_links) {
        const auto link = graph->links.find(link_id);
        if (link == graph->links.end()) continue;
        removed.links.push_back(link->second);
        m_impl->indexes.RemoveLink(link->second);
        graph->links.erase(link);
    }
    for (const auto& pin : removed.pins)
        m_impl->indexes.RemovePin(pin.id);
    m_impl->indexes.RemoveNode(graph_id, removed.node);
    graph->nodes.erase(found);
    return removed;
}

Result<void> GraphDocument::RestoreNode(const GraphId graph_id, RemovedNode removed) {
    const NodeId node_id = removed.node.id;
    if (auto added = AddNode(graph_id, std::move(removed.node), removed.pins); !added) {
        return added;
    }
    for (const auto& link : removed.links) {
        if (auto added = AddLink(graph_id, link); !added) {
            (void)RemoveNode(graph_id, node_id);
            return added;
        }
    }
    return {};
}

Result<void> GraphDocument::AddDynamicPin(const GraphId graph_id, PinInstance pin, const std::size_t index) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    auto node = graph->nodes.find(pin.node);
    if (node == graph->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Pin owner does not exist"));
    }
    if (!pin.id || pin.key.empty() || pin.type.Empty() || pin.storage != PinStorage::Dynamic ||
        !ValidDirection(pin.direction) || !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality) ||
        index > node->second.pins.size()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pin metadata or insertion index is invalid"));
    }
    if (m_impl->indexes.pin_owners.contains(pin.id)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Pin ID already exists"));
    }
    for (const auto pin_id : node->second.pins) {
        if (const auto found = graph->pins.find(pin_id); found != graph->pins.end() && found->second.key == pin.key) {
            return std::unexpected(MakeError(ErrorCode::DuplicateId, "Pin semantic key already exists on node"));
        }
    }

    m_impl->pin_ids.Observe(pin.id);
    const PinId id = pin.id;
    graph->pins.emplace(id, std::move(pin));
    m_impl->indexes.AddPin(graph_id, graph->pins.at(id));
    NodeInstance mutable_node = node->second;
    mutable_node.pins.insert(mutable_node.pins.begin() + static_cast<std::ptrdiff_t>(index), id);
    graph->nodes.insert_or_assign(node->first, std::move(mutable_node));
    return {};
}

Result<RemovedPin> GraphDocument::RemoveDynamicPin(const GraphId graph_id, const PinId pin_id) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto pin = graph->pins.find(pin_id);
    if (pin == graph->pins.end()) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Pin does not exist"));
    }
    if (pin->second.storage != PinStorage::Dynamic) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Static pins cannot be removed"));
    }
    if (IntergraphLinkForPin(pin_id)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Intergraph links must be disconnected "
                                                                  "before removing their endpoint pin"));
    }
    auto node = graph->nodes.find(pin->second.node);
    if (node == graph->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Pin owner does not exist"));
    }
    const auto position = std::ranges::find(node->second.pins, pin_id);
    if (position == node->second.pins.end()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Pin is not referenced by its owner"));
    }

    RemovedPin removed{
        .pin = std::move(pin->second),
        .index = static_cast<std::size_t>(position - node->second.pins.begin()),
    };
    NodeInstance mutable_node = node->second;
    mutable_node.pins.erase(mutable_node.pins.begin() + static_cast<std::ptrdiff_t>(removed.index));
    graph->nodes.insert_or_assign(node->first, std::move(mutable_node));
    const std::vector<LinkId> incident_links(IncidentLinks(pin_id).begin(), IncidentLinks(pin_id).end());
    graph->pins.erase(pin);
    for (const LinkId link_id : incident_links) {
        const auto link = graph->links.find(link_id);
        if (link == graph->links.end()) continue;
        removed.links.push_back(link->second);
        m_impl->indexes.RemoveLink(link->second);
        graph->links.erase(link);
    }
    m_impl->indexes.RemovePin(pin_id);
    return removed;
}

Result<void> GraphDocument::RestoreDynamicPin(const GraphId graph_id, RemovedPin removed) {
    const PinId pin_id = removed.pin.id;
    if (auto result = AddDynamicPin(graph_id, std::move(removed.pin), removed.index); !result) {
        return result;
    }
    for (const auto& link : removed.links) {
        if (auto result = AddLink(graph_id, link); !result) {
            (void)RemoveDynamicPin(graph_id, pin_id);
            return result;
        }
    }
    return {};
}

Result<void> GraphDocument::UpdateDynamicPin(const GraphId graph_id, PinInstance pin) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto found = graph->pins.find(pin.id);
    if (found == graph->pins.end()) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Pin does not exist"));
    }
    const auto& current = found->second;
    if (current.storage != PinStorage::Dynamic || pin.storage != PinStorage::Dynamic) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Static pins cannot be updated"));
    }
    if (pin.node != current.node || pin.key != current.key || pin.id != current.id || pin.type.Empty() ||
        !ValidDirection(pin.direction) || !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality)) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidArgument, "Dynamic pin ID, owner, semantic key, and storage are immutable"));
    }
    const bool structural_change = pin.type != current.type || pin.direction != current.direction ||
                                   pin.kind != current.kind || pin.cardinality != current.cardinality;
    if (structural_change && (!IncidentLinks(pin.id).empty() || IntergraphLinkForPin(pin.id))) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Connected dynamic pins cannot change "
                                                                  "type, direction, kind, or cardinality"));
    }
    graph->pins.insert_or_assign(found->first, std::move(pin));
    return {};
}

Result<void> GraphDocument::ReorderDynamicPins(const GraphId graph_id, const NodeId node_id, std::vector<PinId> order) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto node_found = graph->nodes.find(node_id);
    if (node_found == graph->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    const auto& node = node_found->second;
    if (node.role == NodeRole::Subgraph || node.role == NodeRole::BoundaryInput ||
        node.role == NodeRole::BoundaryOutput) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Interface projection pins cannot be reordered"));
    }
    if (order.size() != node.pins.size()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pin order must contain every node pin"));
    }
    std::unordered_set<PinId, IdHash> expected(node.pins.begin(), node.pins.end());
    for (const auto pin : order) {
        if (!expected.erase(pin)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Pin order is not a permutation"));
        }
    }
    for (std::size_t index = 0; index < node.pins.size(); ++index) {
        const auto pin = graph->pins.find(node.pins[index]);
        if (pin == graph->pins.end()) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node references a missing pin"));
        }
        if (pin->second.storage == PinStorage::Static && order[index] != node.pins[index]) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidArgument, "Descriptor-owned static pins cannot be reordered"));
        }
    }
    NodeInstance reordered = node;
    reordered.pins = std::move(order);
    graph->nodes.insert_or_assign(node_id, std::move(reordered));
    return {};
}

Result<void> GraphDocument::AddLink(const GraphId graph_id, Link link) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    if (!link.id || !link.output || !link.input || link.output == link.input) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link contains invalid IDs"));
    }
    if (m_impl->indexes.link_owners.contains(link.id)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Link ID already exists"));
    }

    const auto output = graph->pins.find(link.output);
    const auto input = graph->pins.find(link.input);
    if (output == graph->pins.end() || input == graph->pins.end()) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Link endpoint does not exist in the graph"));
    }
    if (output->second.direction != PinDirection::Output || input->second.direction != PinDirection::Input) {
        return std::unexpected(MakeError(ErrorCode::InvalidDirection, "Link must run from an output to an input"));
    }
    if (output->second.kind != input->second.kind) {
        return std::unexpected(MakeError(ErrorCode::IncompatiblePins, "Data and execution pins cannot be linked"));
    }
    if (FindLinkBetween(link.output, link.input)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "The pins are already connected"));
    }
    if (output->second.cardinality == PinCardinality::Single && !IncidentLinks(link.output).empty()) {
        return std::unexpected(MakeError(ErrorCode::CardinalityExceeded, "Pin accepts only one connection"));
    }
    if (input->second.cardinality == PinCardinality::Single && !IncidentLinks(link.input).empty()) {
        return std::unexpected(MakeError(ErrorCode::CardinalityExceeded, "Pin accepts only one connection"));
    }

    m_impl->link_ids.Observe(link.id);
    const Link stored = link;
    graph->links.emplace(link.id, std::move(link));
    m_impl->indexes.AddLink(graph_id, stored);
    return {};
}

Result<Link> GraphDocument::RemoveLink(const GraphId graph_id, const LinkId link_id) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto found = graph->links.find(link_id);
    if (found == graph->links.end()) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    }
    Link removed = found->second;
    m_impl->indexes.RemoveLink(removed);
    graph->links.erase(found);
    return removed;
}

Result<void> GraphDocument::SetNodeProperty(const GraphId graph, const NodeId node_id, std::string key,
                                            std::optional<PropertyValue> value) {
    if (key.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property key cannot be empty"));
    }
    if (value && !ValidPropertyValue(*value)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Property value must be finite"));
    }
    auto* state = FindGraphMutable(graph);
    if (state == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto found = state->nodes.find(node_id);
    if (found == state->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    NodeInstance node = found->second;
    if (value) {
        node.properties.insert_or_assign(std::move(key), std::move(*value));
    } else {
        node.properties.erase(key);
    }
    state->nodes.insert_or_assign(node_id, std::move(node));
    return {};
}

Result<void> GraphDocument::SetNodeDisplayName(const GraphId graph, const NodeId node_id, std::string name) {
    auto* state = FindGraphMutable(graph);
    if (state == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto found = state->nodes.find(node_id);
    if (found == state->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    NodeInstance node = found->second;
    node.display_name = std::move(name);
    state->nodes.insert_or_assign(node_id, std::move(node));
    return {};
}

Result<void> GraphDocument::SetNodeSubgraph(const GraphId graph, const NodeId node_id,
                                            std::optional<SubgraphReference> subgraph) {
    auto* state = FindGraphMutable(graph);
    if (state == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto found = state->nodes.find(node_id);
    if (found == state->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    const auto local_subgraph = Detail::LocalSubgraph(subgraph);
    if (local_subgraph && (*local_subgraph == graph || !m_impl->graphs.contains(*local_subgraph) ||
                           (!m_impl->indexes.HasDependencyEdge(graph, *local_subgraph) &&
                            m_impl->indexes.HasDependencyPath(*local_subgraph, graph)))) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Node creates an invalid or cyclic subgraph reference"));
    }
    const NodeInstance previous = found->second;
    NodeInstance node = previous;
    if (node.subgraph && node.subgraph->ownership == SubgraphOwnership::Owned && node.subgraph != subgraph) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Owned subgraphs must be removed together with their owner"));
    }
    node.subgraph = subgraph;
    node.role = node.subgraph ? NodeRole::Subgraph : NodeRole::Regular;
    if (node.subgraph) {
        if (!ValidOwnership(node.subgraph->ownership)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph ownership is invalid"));
        }
        const GraphInterface* interface = SubgraphInterface(m_impl->graphs, *node.subgraph);
        if (interface == nullptr || !ValidInterface(*interface) ||
            !ProjectionMatches(*state, node, *interface, NodeRole::Subgraph)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph projection is invalid"));
        }
        if (const auto* local = std::get_if<DocumentGraphTarget>(&node.subgraph->target)) {
            const auto* target = FindGraph(local->graph);
            const bool owned = node.subgraph->ownership == SubgraphOwnership::Owned;
            if (target == nullptr || (owned && target->lifetime != GraphLifetime::Owned) ||
                (!owned && target->lifetime != GraphLifetime::Reusable)) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph target lifetime is invalid"));
            }
            const auto* owner = m_impl->indexes.OwnedCaller(local->graph);
            if (owned && owner != nullptr && (owner->graph != graph || owner->node != node_id)) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned graph already has an owner"));
            }
        } else {
            const auto& asset = std::get<GraphAssetTarget>(node.subgraph->target);
            if (node.subgraph->ownership != SubgraphOwnership::Referenced || asset.asset.Empty()) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph asset reference is invalid"));
            }
        }
    }
    m_impl->indexes.RemoveNode(graph, previous);
    node.subgraph = std::move(subgraph);
    m_impl->indexes.AddNode(graph, node);
    state->nodes.insert_or_assign(node_id, std::move(node));
    return {};
}

Result<void> GraphDocument::SetGraphReadOnly(const GraphId graph_id, const bool read_only) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    graph->read_only = read_only;
    return {};
}

Result<void> GraphDocument::SetNodeReadOnly(const GraphId graph, const NodeId node_id, const bool read_only) {
    auto* state = FindGraphMutable(graph);
    if (state == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto found = state->nodes.find(node_id);
    if (found == state->nodes.end()) {
        return std::unexpected(MakeError(ErrorCode::NodeNotFound, "Node does not exist"));
    }
    NodeInstance node = found->second;
    node.read_only = read_only;
    state->nodes.insert_or_assign(node_id, std::move(node));
    return {};
}

Result<void> GraphDocument::SetPinReadOnly(const GraphId graph_id, const PinId pin_id, const bool read_only) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto pin = graph->pins.find(pin_id);
    if (pin == graph->pins.end()) {
        return std::unexpected(MakeError(ErrorCode::PinNotFound, "Pin does not exist"));
    }
    PinInstance changed = pin->second;
    changed.read_only = read_only;
    graph->pins.insert_or_assign(pin_id, std::move(changed));
    return {};
}

Result<void> GraphDocument::SetLinkReadOnly(const GraphId graph_id, const LinkId link_id, const bool read_only) {
    auto* graph = FindGraphMutable(graph_id);
    if (graph == nullptr) {
        return std::unexpected(MakeError(ErrorCode::GraphNotFound, "Graph does not exist"));
    }
    const auto link = graph->links.find(link_id);
    if (link == graph->links.end()) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Link does not exist"));
    }
    Link changed = link->second;
    changed.read_only = read_only;
    graph->links.insert_or_assign(link_id, std::move(changed));
    return {};
}

Result<void> GraphDocument::AddIntergraphLink(IntergraphLink link) {
    if (!link.id || !link.source.graph || !link.source.node || !link.source.pin || !link.destination.graph ||
        !link.destination.node || !link.destination.pin || link.source.graph == link.destination.graph) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Intergraph link endpoints are invalid"));
    }
    if (m_impl->intergraph_links.contains(link.id)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Intergraph link ID already exists"));
    }
    const auto* source_node = FindNode(link.source.graph, link.source.node);
    const auto* source_pin = FindPin(link.source.graph, link.source.pin);
    const auto* destination_node = FindNode(link.destination.graph, link.destination.node);
    const auto* destination_pin = FindPin(link.destination.graph, link.destination.pin);
    if (source_node == nullptr || source_pin == nullptr || destination_node == nullptr || destination_pin == nullptr ||
        source_pin->node != source_node->id || destination_pin->node != destination_node->id ||
        source_node->role != NodeRole::IntergraphOutput || destination_node->role != NodeRole::IntergraphInput ||
        source_pin->direction != PinDirection::Input || destination_pin->direction != PinDirection::Output ||
        source_pin->type != destination_pin->type || source_pin->kind != destination_pin->kind) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Intergraph link must connect compatible "
                                                                  "output and input proxy nodes"));
    }
    if (m_impl->indexes.intergraph_endpoints.contains(link.source.pin) ||
        m_impl->indexes.intergraph_endpoints.contains(link.destination.pin)) {
        return std::unexpected(
            MakeError(ErrorCode::CardinalityExceeded, "An intergraph proxy endpoint is already connected"));
    }
    if (!m_impl->indexes.HasDependencyEdge(link.source.graph, link.destination.graph) &&
        m_impl->indexes.HasDependencyPath(link.destination.graph, link.source.graph)) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Intergraph link would create a graph dependency cycle"));
    }
    m_impl->intergraph_link_ids.Observe(link.id);
    const IntergraphLink stored = link;
    m_impl->intergraph_links.emplace(link.id, std::move(link));
    m_impl->indexes.AddIntergraph(stored);
    return {};
}

Result<IntergraphLink> GraphDocument::RemoveIntergraphLink(const IntergraphLinkId link) {
    const auto found = m_impl->intergraph_links.find(link);
    if (found == m_impl->intergraph_links.end()) {
        return std::unexpected(MakeError(ErrorCode::LinkNotFound, "Intergraph link does not exist"));
    }
    IntergraphLink removed = found->second;
    m_impl->indexes.RemoveIntergraph(removed);
    m_impl->intergraph_links.erase(link);
    return removed;
}

Result<void> GraphDocument::Import(const std::uint32_t schema_version, const GraphId root_graph,
                                   std::vector<Graph> graphs, std::vector<IntergraphLink> intergraph_links) {
    if (schema_version == 0 || !root_graph || graphs.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Imported document header is invalid"));
    }

    GraphTable imported;
    imported.reserve(graphs.size());
    for (auto& graph : graphs) {
        const GraphId id = graph.id;
        if (!graph.id || !ValidLifetime(graph.lifetime) || !ValidInterface(graph.interface) ||
            !imported.emplace(id, std::move(graph)).second) {
            return std::unexpected(MakeError(ErrorCode::DuplicateId, "Imported document contains duplicate graph IDs"));
        }
    }
    if (!imported.contains(root_graph)) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Imported root graph does not exist"));
    }

    std::unordered_set<NodeId, IdHash> all_nodes;
    std::unordered_set<PinId, IdHash> all_pins;
    std::unordered_set<LinkId, IdHash> all_links;
    for (const auto& [graph_id, graph] : imported) {
        if (graph.id != graph_id) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Imported graph key and ID differ"));
        }

        std::unordered_set<PinId, IdHash> referenced_pins;
        for (const auto& [node_id, node] : graph.nodes) {
            if (!node_id || node.id != node_id || node.type.Empty() || node.type_version == 0 ||
                !ValidRole(node.role) || !ValidProperties(node.properties) || !all_nodes.insert(node_id).second) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Imported document contains an invalid node"));
            }
            const auto local_subgraph = Detail::LocalSubgraph(node.subgraph);
            if (local_subgraph && (*local_subgraph == graph_id || !imported.contains(*local_subgraph))) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Imported node references an invalid subgraph"));
            }
            std::unordered_set<PinId, IdHash> node_pins;
            std::unordered_set<std::string> pin_keys;
            for (const PinId pin_id : node.pins) {
                const auto pin = graph.pins.find(pin_id);
                if (!node_pins.insert(pin_id).second || pin == graph.pins.end() || pin->second.node != node_id ||
                    !pin_keys.insert(pin->second.key).second) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Imported node pin references are inconsistent"));
                }
                referenced_pins.insert(pin_id);
            }
        }
        for (const auto& [pin_id, pin] : graph.pins) {
            if (!pin_id || pin.id != pin_id || !pin.node || pin.key.empty() || pin.type.Empty() ||
                !ValidDirection(pin.direction) || !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality) ||
                !ValidStorage(pin.storage) || !graph.nodes.contains(pin.node) || !referenced_pins.contains(pin_id) ||
                !all_pins.insert(pin_id).second) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Imported document contains an invalid pin"));
            }
        }

        std::unordered_map<PinId, std::size_t, IdHash> connection_counts;
        std::set<std::pair<PinId, PinId>> connected_pairs;
        for (const auto& [link_id, link] : graph.links) {
            if (!link_id || link.id != link_id || !all_links.insert(link_id).second) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Imported document contains an invalid link ID"));
            }
            const auto output = graph.pins.find(link.output);
            const auto input = graph.pins.find(link.input);
            if (output == graph.pins.end() || input == graph.pins.end() ||
                output->second.direction != PinDirection::Output || input->second.direction != PinDirection::Input ||
                output->second.kind != input->second.kind ||
                !connected_pairs.insert({link.output, link.input}).second) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Imported link endpoints are invalid"));
            }
            ++connection_counts[link.output];
            ++connection_counts[link.input];
        }
        for (const auto& [pin_id, count] : connection_counts) {
            if (graph.pins.at(pin_id).cardinality == PinCardinality::Single && count > 1) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Imported document exceeds pin cardinality"));
            }
        }
    }
    std::unordered_map<GraphId, std::size_t, IdHash> subgraph_indegree;
    std::unordered_map<GraphId, std::vector<GraphId>, IdHash> subgraph_edges;
    subgraph_indegree.reserve(imported.size());
    subgraph_edges.reserve(imported.size());
    for (const auto& [graph_id, graph] : imported) {
        subgraph_indegree.emplace(graph_id, 0);
        for (const auto& [node_id, node] : graph.nodes) {
            (void)node_id;
            if (const auto child = Detail::LocalSubgraph(node.subgraph)) {
                subgraph_edges[graph_id].push_back(*child);
                ++subgraph_indegree[*child];
            }
        }
    }
    std::vector<GraphId> pending_graphs;
    pending_graphs.reserve(imported.size());
    for (const auto& [graph_id, indegree] : subgraph_indegree) {
        if (indegree == 0) pending_graphs.push_back(graph_id);
    }
    std::size_t visited_graphs = 0;
    while (!pending_graphs.empty()) {
        const GraphId graph_id = pending_graphs.back();
        pending_graphs.pop_back();
        ++visited_graphs;
        for (const GraphId child : subgraph_edges[graph_id]) {
            auto& indegree = subgraph_indegree[child];
            if (--indegree == 0) pending_graphs.push_back(child);
        }
    }
    if (visited_graphs != imported.size()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Imported document contains cyclic subgraphs"));
    }

    auto replacement = std::make_unique<Impl>();
    replacement->schema_version = schema_version;
    replacement->root_graph = root_graph;
    replacement->graphs = std::move(imported);
    replacement->graph_revisions.reserve(replacement->graphs.size());
    for (const auto& [graph_id, graph] : replacement->graphs) {
        (void)graph;
        replacement->graph_revisions.emplace(graph_id, SemanticRevisionSet{});
    }
    for (auto& link : intergraph_links) {
        const IntergraphLinkId id = link.id;
        if (!id || !replacement->intergraph_links.emplace(id, std::move(link)).second) {
            return std::unexpected(
                MakeError(ErrorCode::DuplicateId, "Imported document contains duplicate intergraph link IDs"));
        }
    }
    for (const auto& [graph_id, graph] : replacement->graphs) {
        replacement->graph_ids.Observe(graph_id);
        for (const auto& [node_id, node] : graph.nodes) {
            (void)node;
            replacement->node_ids.Observe(node_id);
        }
        for (const auto& [pin_id, pin] : graph.pins) {
            (void)pin;
            replacement->pin_ids.Observe(pin_id);
        }
        for (const auto& [link_id, link] : graph.links) {
            (void)link;
            replacement->link_ids.Observe(link_id);
        }
    }
    for (const auto& [link_id, link] : replacement->intergraph_links) {
        (void)link;
        replacement->intergraph_link_ids.Observe(link_id);
    }
    replacement->indexes = Impl::Indexes::Build(replacement->graphs, replacement->intergraph_links);
    m_impl = std::move(replacement);
    return ValidateStructure();
}

Graph* GraphDocument::FindGraphMutable(const GraphId id) {
    return m_impl->graphs.EditValue(id);
}

const Graph* GraphDocument::FindGraph(const GraphId id) const noexcept {
    const auto found = m_impl->graphs.find(id);
    return found != m_impl->graphs.end() ? &found->second : nullptr;
}

const NodeInstance* GraphDocument::FindNode(const GraphId graph, const NodeId id) const noexcept {
    const auto* value = FindGraph(graph);
    if (value == nullptr) {
        return nullptr;
    }
    const auto found = value->nodes.find(id);
    return found != value->nodes.end() ? &found->second : nullptr;
}

const PinInstance* GraphDocument::FindPin(const GraphId graph, const PinId id) const noexcept {
    const auto* value = FindGraph(graph);
    if (value == nullptr) {
        return nullptr;
    }
    const auto found = value->pins.find(id);
    return found != value->pins.end() ? &found->second : nullptr;
}

const Link* GraphDocument::FindLink(const GraphId graph, const LinkId id) const noexcept {
    const auto* value = FindGraph(graph);
    if (value == nullptr) {
        return nullptr;
    }
    const auto found = value->links.find(id);
    return found != value->links.end() ? &found->second : nullptr;
}

const IntergraphLink* GraphDocument::FindIntergraphLink(const IntergraphLinkId id) const noexcept {
    const auto found = m_impl->intergraph_links.find(id);
    return found != m_impl->intergraph_links.end() ? &found->second : nullptr;
}

const IntergraphLinkMap& GraphDocument::IntergraphLinks() const noexcept {
    return m_impl->intergraph_links;
}

GraphId GraphDocument::FindNodeGraph(const NodeId node) const noexcept {
    const auto found = m_impl->indexes.node_owners.find(node);
    return found != m_impl->indexes.node_owners.end() ? found->second : GraphId{};
}

std::optional<PinOwner> GraphDocument::FindPinOwner(const PinId pin) const noexcept {
    const auto found = m_impl->indexes.pin_owners.find(pin);
    return found != m_impl->indexes.pin_owners.end() ? std::optional<PinOwner>{found->second} : std::nullopt;
}

GraphId GraphDocument::FindLinkGraph(const LinkId link) const noexcept {
    const auto found = m_impl->indexes.link_owners.find(link);
    return found != m_impl->indexes.link_owners.end() ? found->second : GraphId{};
}

LinkId GraphDocument::FindLinkBetween(const PinId output, const PinId input) const noexcept {
    return m_impl->indexes.FindLinkBetween(output, input);
}

const IncidentLinkRange& GraphDocument::IncidentLinks(const PinId pin) const noexcept {
    const auto found = m_impl->indexes.pin_incidence.find(pin);
    return found != m_impl->indexes.pin_incidence.end() ? found->second : IncidentLinkRange::Empty();
}

const SubgraphCallerRange& GraphDocument::SubgraphCallers(const GraphId graph) const noexcept {
    const auto found = m_impl->indexes.callers.find(graph);
    return found != m_impl->indexes.callers.end() ? found->second.callers : SubgraphCallerRange::Empty();
}

std::size_t GraphDocument::OwnedSubgraphCallerCount(const GraphId graph) const noexcept {
    return m_impl->indexes.OwnedCallerCount(graph);
}

IntergraphLinkId GraphDocument::IntergraphLinkForPin(const PinId pin) const noexcept {
    const auto found = m_impl->indexes.intergraph_endpoints.find(pin);
    return found != m_impl->indexes.intergraph_endpoints.end() ? found->second : IntergraphLinkId{};
}

const IntergraphLinkRange& GraphDocument::IntergraphLinksForGraph(const GraphId graph) const noexcept {
    const auto found = m_impl->indexes.graph_intergraph_links.find(graph);
    return found != m_impl->indexes.graph_intergraph_links.end() ? found->second : IntergraphLinkRange::Empty();
}

const BoundaryNodeRange& GraphDocument::BoundaryNodes(const GraphId graph, const NodeRole role) const noexcept {
    const Impl::Indexes::BoundaryNodes* index = nullptr;
    if (role == NodeRole::BoundaryInput) index = &m_impl->indexes.boundary_inputs;
    if (role == NodeRole::BoundaryOutput) index = &m_impl->indexes.boundary_outputs;
    if (index == nullptr) return BoundaryNodeRange::Empty();
    const auto found = index->find(graph);
    return found != index->end() ? found->second : BoundaryNodeRange::Empty();
}

bool GraphDocument::HasDependencyPath(const GraphId from, const GraphId target) const {
    return m_impl->indexes.HasDependencyPath(from, target);
}

std::vector<std::reference_wrapper<const Graph>> GraphDocument::Graphs() const {
    std::vector<std::reference_wrapper<const Graph>> graphs;
    graphs.reserve(m_impl->graphs.size());
    for (const auto& [id, graph] : m_impl->graphs) {
        (void)id;
        graphs.emplace_back(graph);
    }
    return graphs;
}

Result<void> GraphDocument::ValidateStructure() const {
    Detail::RecordFullStructureValidation();
    const auto root = m_impl->graphs.find(m_impl->root_graph);
    if (root == m_impl->graphs.end() || root->second.lifetime != GraphLifetime::Reusable) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Document root must be an existing reusable graph"));
    }

    std::unordered_set<NodeId, IdHash> all_nodes;
    std::unordered_set<PinId, IdHash> all_pins;
    std::unordered_set<LinkId, IdHash> all_links;
    std::unordered_map<GraphId, std::size_t, IdHash> owned_indegree;
    std::unordered_map<GraphId, std::vector<GraphId>, IdHash> dependencies;
    std::unordered_map<GraphId, std::size_t, IdHash> dependency_indegree;
    for (const auto& [graph_id, graph] : m_impl->graphs) {
        if (graph.id != graph_id || !ValidLifetime(graph.lifetime) || !ValidInterface(graph.interface)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph metadata is invalid"));
        }
        owned_indegree.emplace(graph_id, 0);
        dependency_indegree.emplace(graph_id, 0);

        std::size_t boundary_inputs = 0;
        std::size_t boundary_outputs = 0;
        std::unordered_set<PinId, IdHash> referenced_pins;
        for (const auto& [node_id, node] : graph.nodes) {
            if (!node_id || node.id != node_id || !all_nodes.insert(node_id).second ||
                FindNodeGraph(node_id) != graph_id || node.type.Empty() || node.type_version == 0 ||
                !ValidProperties(node.properties) || !ValidRole(node.role)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Node identity, type, properties, or role are invalid"));
            }
            std::unordered_set<PinId, IdHash> node_pins;
            std::unordered_set<std::string> pin_keys;
            for (const PinId pin_id : node.pins) {
                const auto pin = graph.pins.find(pin_id);
                if (!node_pins.insert(pin_id).second || pin == graph.pins.end() || pin->second.node != node_id ||
                    !pin_keys.insert(pin->second.key).second) {
                    return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Node pin references are inconsistent"));
                }
                referenced_pins.insert(pin_id);
            }
            if ((node.role == NodeRole::Subgraph) != node.subgraph.has_value()) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Subgraph nodes must have exactly one subgraph reference"));
            }
            if (node.subgraph && !ValidOwnership(node.subgraph->ownership)) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Subgraph ownership is invalid"));
            }
            if (node.role == NodeRole::BoundaryInput) {
                ++boundary_inputs;
                if (node.subgraph || !ProjectionMatches(graph, node, graph.interface, node.role)) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Boundary input node does not match the graph interface"));
                }
            }
            if (node.role == NodeRole::BoundaryOutput) {
                ++boundary_outputs;
                if (node.subgraph || !ProjectionMatches(graph, node, graph.interface, node.role)) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Boundary output node does not match the graph interface"));
                }
            }
            if (!node.subgraph) {
                continue;
            }
            const GraphInterface* target_interface = SubgraphInterface(m_impl->graphs, *node.subgraph);
            if (target_interface == nullptr || !ValidInterface(*target_interface) ||
                !ProjectionMatches(graph, node, *target_interface, NodeRole::Subgraph)) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Subgraph node pins do not match the target interface"));
            }
            if (const auto* local = std::get_if<DocumentGraphTarget>(&node.subgraph->target)) {
                const auto target = m_impl->graphs.find(local->graph);
                if (target == m_impl->graphs.end() || local->graph == graph_id) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Subgraph node references a missing or containing graph"));
                }
                if (node.subgraph->ownership == SubgraphOwnership::Owned) {
                    if (target->second.lifetime != GraphLifetime::Owned) {
                        return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                         "Owned subgraph references require an owned target graph"));
                    }
                    ++owned_indegree[local->graph];
                } else if (target->second.lifetime != GraphLifetime::Reusable) {
                    return std::unexpected(
                        MakeError(ErrorCode::InvalidGraph, "Referenced subgraphs cannot target an owned graph"));
                }
                dependencies[graph_id].push_back(local->graph);
                ++dependency_indegree[local->graph];
            } else {
                const auto& asset = std::get<GraphAssetTarget>(node.subgraph->target);
                if (node.subgraph->ownership != SubgraphOwnership::Referenced || asset.asset.Empty()) {
                    return std::unexpected(MakeError(ErrorCode::InvalidGraph,
                                                     "Graph assets can only be referenced by a stable non-empty ID"));
                }
            }
        }
        const bool has_interface_nodes = boundary_inputs != 0 || boundary_outputs != 0;
        if ((has_interface_nodes || !graph.interface.pins.empty()) && (boundary_inputs != 1 || boundary_outputs != 1)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "A graph interface requires exactly one "
                                                                      "input and one output boundary node"));
        }
        for (const auto& [pin_id, pin] : graph.pins) {
            if (!pin_id || pin.id != pin_id || !pin.node || pin.key.empty() || pin.type.Empty() ||
                !ValidDirection(pin.direction) || !ValidKind(pin.kind) || !ValidCardinality(pin.cardinality) ||
                !ValidStorage(pin.storage) || !graph.nodes.contains(pin.node) || !referenced_pins.contains(pin_id) ||
                FindPinOwner(pin_id) != std::optional<PinOwner>{PinOwner{graph_id, pin.node}} ||
                !all_pins.insert(pin_id).second) {
                return std::unexpected(
                    MakeError(ErrorCode::InvalidGraph, "Pin metadata, ownership, or document identity is invalid"));
            }
        }
        std::unordered_map<PinId, std::size_t, IdHash> connection_counts;
        std::set<std::pair<PinId, PinId>> connected_pairs;
        for (const auto& [link_id, link] : graph.links) {
            const auto output = graph.pins.find(link.output);
            const auto input = graph.pins.find(link.input);
            if (!link_id || link.id != link_id || !all_links.insert(link_id).second ||
                FindLinkGraph(link_id) != graph_id || output == graph.pins.end() || input == graph.pins.end() ||
                output->second.direction != PinDirection::Output || input->second.direction != PinDirection::Input ||
                output->second.kind != input->second.kind ||
                !connected_pairs.insert({link.output, link.input}).second) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Link identity or endpoints are invalid"));
            }
            ++connection_counts[link.output];
            ++connection_counts[link.input];
        }
        for (const auto& [pin_id, count] : connection_counts) {
            if (graph.pins.at(pin_id).cardinality == PinCardinality::Single && count > 1) {
                return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph exceeds pin cardinality"));
            }
        }
    }

    for (const auto& [graph_id, graph] : m_impl->graphs) {
        const std::size_t owners = owned_indegree[graph_id];
        if ((graph.lifetime == GraphLifetime::Owned && owners != 1) ||
            (graph.lifetime == GraphLifetime::Reusable && owners != 0)) {
            return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Owned graphs require exactly one owner "
                                                                      "and reusable graphs cannot have one"));
        }
    }

    std::unordered_set<IntergraphEndpoint, std::function<std::size_t(const IntergraphEndpoint&)>> endpoints(
        0, [](const IntergraphEndpoint& endpoint) {
            std::size_t value = IdHash{}(endpoint.graph);
            value ^= IdHash{}(endpoint.node) + 0x9E3779B9U + (value << 6U) + (value >> 2U);
            value ^= IdHash{}(endpoint.pin) + 0x9E3779B9U + (value << 6U) + (value >> 2U);
            return value;
        });
    for (const auto& [link_id, link] : m_impl->intergraph_links) {
        if (!link_id || link.id != link_id || !endpoints.insert(link.source).second ||
            IntergraphLinkForPin(link.source.pin) != link_id || IntergraphLinkForPin(link.destination.pin) != link_id ||
            !endpoints.insert(link.destination).second) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Intergraph link identity or endpoint cardinality is invalid"));
        }
        const auto* source_node = FindNode(link.source.graph, link.source.node);
        const auto* source_pin = FindPin(link.source.graph, link.source.pin);
        const auto* destination_node = FindNode(link.destination.graph, link.destination.node);
        const auto* destination_pin = FindPin(link.destination.graph, link.destination.pin);
        if (source_node == nullptr || source_pin == nullptr || destination_node == nullptr ||
            destination_pin == nullptr || link.source.graph == link.destination.graph ||
            source_node->role != NodeRole::IntergraphOutput || destination_node->role != NodeRole::IntergraphInput ||
            source_pin->node != source_node->id || destination_pin->node != destination_node->id ||
            source_pin->direction != PinDirection::Input || destination_pin->direction != PinDirection::Output ||
            source_pin->type != destination_pin->type || source_pin->kind != destination_pin->kind) {
            return std::unexpected(
                MakeError(ErrorCode::InvalidGraph, "Intergraph link endpoints are invalid or incompatible"));
        }
        dependencies[link.source.graph].push_back(link.destination.graph);
        ++dependency_indegree[link.destination.graph];
    }

    std::vector<GraphId> pending;
    for (const auto& [graph_id, indegree] : dependency_indegree) {
        if (indegree == 0) {
            pending.push_back(graph_id);
        }
    }
    std::size_t visited = 0;
    while (!pending.empty()) {
        const GraphId graph = pending.back();
        pending.pop_back();
        ++visited;
        for (const GraphId target : dependencies[graph]) {
            if (--dependency_indegree[target] == 0) {
                pending.push_back(target);
            }
        }
    }
    if (visited != m_impl->graphs.size()) {
        return std::unexpected(MakeError(ErrorCode::InvalidGraph, "Graph dependencies contain a recursive cycle"));
    }
    const Impl::Indexes expected_indexes = Impl::Indexes::Build(m_impl->graphs, m_impl->intergraph_links);
    if (!m_impl->indexes.Equivalent(expected_indexes)) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidGraph, "Derived graph indexes do not match retained semantic state"));
    }
    return {};
}

GraphDocument GraphDocument::SnapshotForTransaction() const {
    GraphDocument snapshot{SnapshotTag{}};
    *snapshot.m_impl = *m_impl;
    return snapshot;
}

std::uint64_t GraphDocument::AllocationEpoch() const noexcept {
    return m_impl->allocation_epoch;
}

bool GraphDocument::CanCommit(
    const std::uint64_t identity, const std::uint64_t revision, const std::uint64_t allocation_epoch,
    const SemanticRevisionSet& changes,
    const std::unordered_map<GraphId, SemanticRevisionSet, IdHash>& graph_changes) const noexcept {
    const auto can_increment = [](const std::uint64_t value, const std::uint64_t change) {
        return change == 0 || value != std::numeric_limits<std::uint64_t>::max();
    };
    if (Identity() != identity || m_impl->model_revision != revision || m_impl->allocation_epoch != allocation_epoch ||
        !can_increment(m_impl->model_revision, changes.serial) ||
        !can_increment(m_impl->topology_revision, changes.topology) ||
        !can_increment(m_impl->value_revision, changes.value) ||
        !can_increment(m_impl->layout_revision, changes.layout)) {
        return false;
    }
    return std::ranges::all_of(graph_changes, [&](const auto& entry) {
        const auto graph = m_impl->graph_revisions.find(entry.first);
        if (graph == m_impl->graph_revisions.end()) return true;
        const auto& current = graph->second;
        return can_increment(current.topology, entry.second.topology) &&
               can_increment(current.value, entry.second.value) && can_increment(current.layout, entry.second.layout);
    });
}

void GraphDocument::CommitFrom(GraphDocument&& staged, const SemanticRevisionSet& changes,
                               const std::unordered_map<GraphId, SemanticRevisionSet, IdHash>& graph_changes) noexcept {
    if (changes.serial == 0) {
        return;
    }
    const std::uint64_t next_revision = m_impl->model_revision + 1;
    const std::uint64_t next_topology = m_impl->topology_revision + (changes.topology != 0 ? 1 : 0);
    const std::uint64_t next_value = m_impl->value_revision + (changes.value != 0 ? 1 : 0);
    const std::uint64_t next_layout = m_impl->layout_revision + (changes.layout != 0 ? 1 : 0);
    for (const auto& [graph_id, domains] : graph_changes) {
        if (!staged.m_impl->graphs.contains(graph_id)) continue;
        const auto current = m_impl->graph_revisions.find(graph_id);
        const SemanticRevisionSet previous =
            current != m_impl->graph_revisions.end() ? current->second : SemanticRevisionSet{};
        SemanticRevisionSet revisions;
        revisions.serial = next_revision;
        revisions.topology = current == m_impl->graph_revisions.end() && domains.topology != 0
                                 ? next_revision
                                 : previous.topology + (domains.topology != 0 ? 1 : 0);
        revisions.value = current == m_impl->graph_revisions.end() && domains.value != 0
                              ? next_revision
                              : previous.value + (domains.value != 0 ? 1 : 0);
        revisions.layout = current == m_impl->graph_revisions.end() && domains.layout != 0
                               ? next_revision
                               : previous.layout + (domains.layout != 0 ? 1 : 0);
        staged.m_impl->graph_revisions.insert_or_assign(graph_id, revisions);
    }
    *m_impl = std::move(*staged.m_impl);
    m_impl->model_revision = next_revision;
    m_impl->topology_revision = next_topology;
    m_impl->value_revision = next_value;
    m_impl->layout_revision = next_layout;
}

std::uint64_t GraphDocument::Identity() const noexcept {
    return m_impl->identity;
}

} // namespace Uni::GUI::Nodes
