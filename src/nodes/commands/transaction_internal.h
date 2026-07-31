#pragma once

#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes::TransactionDetail {

using CommandDetail::Finite;

[[nodiscard]] inline bool ValidNodePresentation(const NodePresentation& value) noexcept {
    return Finite(value.position) && Finite(value.size) && value.size.x >= 0.0f && value.size.y >= 0.0f;
}

[[nodiscard]] inline bool ValidLinkPresentation(const LinkPresentation& value) noexcept {
    return value.Route().Valid();
}

[[nodiscard]] inline bool ValidGroupHeader(const GroupPresentation& value) noexcept {
    return value.id && value.graph && value.style && Finite(value.geometry.position) &&
           Finite(value.geometry.size) && value.geometry.size.x >= 0.0f && value.geometry.size.y >= 0.0f &&
           (value.style->kind == GroupKind::Group || value.style->kind == GroupKind::Comment);
}

[[nodiscard]] inline bool ValidGroupPresentation(const GraphDocument& document,
                                                 const GroupPresentation& value) noexcept {
    return ValidGroupHeader(value) && std::ranges::all_of(value.members, [&](const NodeId node) {
               return node && document.FindNode(value.graph, node) != nullptr;
           });
}

[[nodiscard]] inline bool ContainsNode(const GraphDocument& document, const NodeId node) {
    return static_cast<bool>(document.FindNodeGraph(node));
}

[[nodiscard]] inline bool ContainsLink(const GraphDocument& document, const LinkId link) {
    return static_cast<bool>(document.FindLinkGraph(link));
}

[[nodiscard]] inline GraphId FindNodeGraph(const GraphDocument& document, const NodeId node) {
    return document.FindNodeGraph(node);
}

[[nodiscard]] inline GraphId FindLinkGraph(const GraphDocument& document, const LinkId link) {
    return document.FindLinkGraph(link);
}

enum class SemanticDomain : std::uint8_t {
    None = 0,
    Topology = 1U << 0U,
    Value = 1U << 1U,
    Layout = 1U << 2U,
};

enum class GroupImpact : std::uint8_t {
    None = 0,
    Geometry = 1U << 0U,
    Style = 1U << 1U,
    Membership = 1U << 2U,
    Protection = 1U << 3U,
    Lifecycle = 1U << 4U,
};

[[nodiscard]] constexpr GroupImpact operator|(const GroupImpact left, const GroupImpact right) noexcept {
    return static_cast<GroupImpact>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

constexpr GroupImpact& operator|=(GroupImpact& left, const GroupImpact right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool HasImpact(const GroupImpact value, const GroupImpact impact) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(impact)) != 0;
}

[[nodiscard]] constexpr SemanticDomain operator|(const SemanticDomain left, const SemanticDomain right) noexcept {
    return static_cast<SemanticDomain>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

constexpr SemanticDomain& operator|=(SemanticDomain& left, const SemanticDomain right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool HasDomain(const SemanticDomain value, const SemanticDomain domain) noexcept {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(domain)) != 0;
}

[[nodiscard]] constexpr SemanticDomain ImpactDomain(const PropertyImpact impact) noexcept {
    switch (impact) {
    case PropertyImpact::RuntimeOnly:
    case PropertyImpact::Rendering: return SemanticDomain::Value;
    case PropertyImpact::Geometry: return SemanticDomain::Value | SemanticDomain::Layout;
    case PropertyImpact::Topology: return SemanticDomain::Value | SemanticDomain::Layout | SemanticDomain::Topology;
    }
    return SemanticDomain::Value | SemanticDomain::Layout;
}

template <typename Id> struct EntityTouch final {
    GraphId graph;
    Id id;

    bool operator==(const EntityTouch&) const = default;
};

template <typename Id> struct EntityTouchHash final {
    std::size_t operator()(const EntityTouch<Id>& value) const noexcept {
        const std::size_t first = IdHash{}(value.graph);
        const std::size_t second = IdHash{}(value.id);
        return first ^ (second + 0x9E3779B9U + (first << 6U) + (first >> 2U));
    }
};

template <typename Key, typename Hash = IdHash> using ImpactMap = std::unordered_map<Key, SemanticDomain, Hash>;

struct TransactionJournal final {
    struct LinkPresentationTouch final {
        LinkPresentationImpact impact{LinkPresentationImpact::None};
        bool full_route_validation{false};
        std::unordered_set<RoutePointId, IdHash> route_points;
    };

    bool schema{false};
    bool root{false};
    ImpactMap<GraphId> graphs;
    std::unordered_set<GraphId, IdHash> replaced_graphs;
    ImpactMap<EntityTouch<NodeId>, EntityTouchHash<NodeId>> nodes;
    ImpactMap<EntityTouch<PinId>, EntityTouchHash<PinId>> pins;
    ImpactMap<EntityTouch<LinkId>, EntityTouchHash<LinkId>> links;
    ImpactMap<IntergraphLinkId> intergraph_links;
    std::unordered_set<NodeId, IdHash> node_presentations;
    std::unordered_map<LinkId, LinkPresentationTouch, IdHash> link_presentations;
    std::unordered_map<GroupId, GroupImpact, IdHash> groups;
};

template <typename Value> [[nodiscard]] bool Different(const Value* before, const Value* after) {
    return before == nullptr ? after != nullptr : after == nullptr || *before != *after;
}

[[nodiscard]] inline bool DifferentGraphState(const Graph* before, const Graph* after) {
    if (before == nullptr || after == nullptr) {
        return before != after;
    }
    return *before != *after;
}

[[nodiscard]] inline bool NodePresentationGeometryChanged(const NodePresentation* before,
                                                          const NodePresentation* after) {
    if (before == nullptr || after == nullptr) return before != after;
    return before->position != after->position || before->size != after->size ||
           before->collapsed != after->collapsed || before->z_order != after->z_order;
}

struct FinalizedJournal final {
    bool model_changed{false};
    bool presentation_changed{false};
    bool presentation_geometry_changed{false};
    SemanticDomain document_domains{SemanticDomain::None};
    ImpactMap<GraphId> graph_domains;
    std::vector<GraphId> graphs;
    std::vector<EntityTouch<NodeId>> nodes;
    std::vector<EntityTouch<PinId>> pins;
    std::vector<EntityTouch<LinkId>> links;
    std::vector<IntergraphLinkId> intergraph_links;
    std::vector<NodeId> node_presentations;
    std::vector<LinkId> link_presentations;
    std::unordered_map<LinkId, TransactionJournal::LinkPresentationTouch, IdHash> link_presentation_impacts;
    std::vector<GroupId> groups;
    std::unordered_map<GroupId, GroupImpact, IdHash> group_impacts;
    bool has_replaced_graph{false};
    std::uint64_t entries{0};
};

} // namespace Uni::GUI::Nodes::TransactionDetail

namespace Uni::GUI::Nodes {

struct GraphTransaction::Impl final {
    GraphDocument* document;
    GraphPresentation* presentation;
    GraphDocument baseline_document;
    GraphPresentation baseline_presentation;
    GraphDocument staged_document;
    GraphPresentation staged_presentation;
    Revisions expected;
    std::uint64_t document_identity;
    std::uint64_t presentation_identity;
    std::uint64_t document_allocation_epoch;
    std::uint64_t presentation_allocation_epoch;
    RegistrySnapshot registry;
    bool enforce_protection;
    bool record_operations;
    std::size_t max_operations;
    std::size_t reserved_operations{0};
    bool operation_limit_exceeded{false};
    bool model_changed{false};
    bool presentation_changed{false};
    TransactionDetail::TransactionJournal journal;
    std::unordered_map<GraphId, Graph, IdHash> replacement_baselines;
    std::vector<OperationIntent> operations;
    std::vector<CommandPath> command_paths;

    [[nodiscard]] Result<void> ConsumeOperations(std::size_t count = 1);
    void ReleaseOperations(std::size_t count = 1) noexcept;
    [[nodiscard]] std::size_t AvailableOperations() const noexcept;

    template <typename Factory> void StoreOperationLazy(Factory&& factory) {
        if (!record_operations) return;
        if (operations.size() >= reserved_operations) {
            operation_limit_exceeded = true;
            return;
        }
        OperationIntent operation = std::forward<Factory>(factory)();
        if (!command_paths.empty()) operation.path = command_paths.back();
        operations.push_back(std::move(operation));
        Detail::RecordOperationIntent();
    }

    [[nodiscard]] static std::shared_ptr<const GraphMetadata> Metadata(const Graph& graph);
    void RecordGraphAdded(const Graph& graph);
    void RecordGraphRemoved(const Graph& graph);
    void RecordGraphReplaced(const Graph& before, const Graph& after);
    [[nodiscard]] static std::size_t GraphReplacementOperationCount(const Graph& before, const Graph& after,
                                                                    std::size_t limit);
    [[nodiscard]] static GroupGeometryOperation GroupGeometry(const GroupPresentation& group);
    [[nodiscard]] static std::shared_ptr<const GroupMembershipChange>
    GroupMemberDifference(const GroupMemberSet& before, const GroupMemberSet& after);

    void TouchGraph(GraphId graph, TransactionDetail::SemanticDomain domain);
    void TouchNode(GraphId graph, NodeId node, TransactionDetail::SemanticDomain domain);
    void TouchPin(GraphId graph, PinId pin, TransactionDetail::SemanticDomain domain);
    void TouchLink(GraphId graph, LinkId link, TransactionDetail::SemanticDomain domain);
    void TouchLinkPresentation(
        LinkId link,
        LinkPresentationImpact impact,
        bool full_route_validation = false,
        std::span<const RoutePointId> route_points = {});
    [[nodiscard]] TransactionDetail::FinalizedJournal FinalizeJournal() const;

    [[nodiscard]] bool GraphReadOnly(GraphId graph) const;
    [[nodiscard]] bool NodeReadOnly(GraphId graph, NodeId node) const;
    [[nodiscard]] bool PinReadOnly(GraphId graph, PinId pin) const;
    [[nodiscard]] bool LinkReadOnly(GraphId graph, LinkId link) const;
    [[nodiscard]] bool NodeLocked(NodeId node) const;
    [[nodiscard]] bool LinkLocked(LinkId link) const;
    [[nodiscard]] bool GroupLocked(GroupId group) const;
    [[nodiscard]] Result<void> CheckLinkRemoval(GraphId graph, LinkId link) const;
};

} // namespace Uni::GUI::Nodes
