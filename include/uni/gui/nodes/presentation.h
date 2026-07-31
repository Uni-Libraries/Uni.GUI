#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/graph.h>

#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Uni::GUI::Nodes {

class CommandStack;
class GraphTransaction;
class GraphPresentation;
namespace Detail {
struct GraphIoAccess;
}

[[nodiscard]] UNI_GUI_EXPORT Result<void> ValidateGraphPresentation(
    const GraphDocument& document,
    const GraphPresentation& presentation);

struct RoutePoint final {
    RoutePointId id;
    Vec2 position;

    bool operator==(const RoutePoint&) const = default;
};

struct RoutePointIdDelta final {
    std::vector<RoutePointId> added;
    std::vector<RoutePointId> removed;
};

struct RoutePointStorageStatistics final {
    std::size_t point_count{0};
    std::size_t chunk_count{0};
    std::size_t capacity_points{0};
    std::size_t min_chunk_points{0};
    std::size_t max_chunk_points{0};
    std::size_t under_half_full_chunks{0};
    std::size_t mergeable_adjacent_pairs{0};
};

class UNI_GUI_EXPORT PersistentRoutePointSequence final {
private:
    struct Impl;

public:
    static constexpr std::size_t ChunkCapacity = 256;

    class const_iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = RoutePoint;
        using difference_type = std::ptrdiff_t;
        using pointer = const RoutePoint*;
        using reference = const RoutePoint&;

        const_iterator() = default;
        [[nodiscard]] UNI_GUI_EXPORT reference operator*() const noexcept;
        [[nodiscard]] UNI_GUI_EXPORT pointer operator->() const noexcept;
        UNI_GUI_EXPORT const_iterator& operator++() noexcept;
        UNI_GUI_EXPORT const_iterator operator++(int) noexcept;
        bool operator==(const const_iterator&) const = default;

    private:
        const_iterator(std::shared_ptr<const Impl> impl, std::size_t chunk, std::size_t point) noexcept;
        void Advance() noexcept;

        std::shared_ptr<const Impl> m_impl;
        std::size_t m_chunk{0};
        std::size_t m_point{0};

        friend class PersistentRoutePointSequence;
    };

    PersistentRoutePointSequence() = default;
    explicit PersistentRoutePointSequence(std::vector<RoutePoint> points);
    PersistentRoutePointSequence(std::initializer_list<RoutePoint> points);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] const RoutePoint& front() const;
    [[nodiscard]] const RoutePoint& operator[](std::size_t index) const;
    [[nodiscard]] const RoutePoint* Find(RoutePointId point) const noexcept;
    [[nodiscard]] bool contains(RoutePointId point) const noexcept;
    [[nodiscard]] bool Valid() const noexcept;
    [[nodiscard]] bool ValidateStructure() const noexcept;
    [[nodiscard]] RoutePointStorageStatistics StorageStatistics() const noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;
    [[nodiscard]] bool SharesStorageWith(const PersistentRoutePointSequence& other) const noexcept;
    [[nodiscard]] std::vector<RoutePoint> ToVector() const;
    [[nodiscard]] RoutePointIdDelta DifferenceIds(
        const PersistentRoutePointSequence& other) const;
    [[nodiscard]] Result<PersistentRoutePointSequence> WithMovedPoint(
        RoutePointId point,
        Vec2 position) const;
    [[nodiscard]] bool operator==(const PersistentRoutePointSequence& other) const;

private:
    [[nodiscard]] Result<void> Insert(std::size_t index, RoutePoint point);
    [[nodiscard]] Result<void> Move(RoutePointId point, Vec2 position);
    [[nodiscard]] Result<void> Remove(std::span<const RoutePointId> points);

    std::shared_ptr<const Impl> m_impl;

    friend class GraphPresentation;
};

struct NodePresentation final {
    Vec2 position;
    Vec2 size;
    bool collapsed{false};
    std::uint64_t z_order{0};
    std::optional<std::uint32_t> color;
    bool locked{false};

    bool operator==(const NodePresentation&) const = default;
};

struct LinkStyle final {
    TypeId router;
    std::optional<std::uint32_t> color;
    bool locked{false};

    bool operator==(const LinkStyle&) const = default;
};

class UNI_GUI_EXPORT LinkPresentation final {
public:
    LinkPresentation();
    explicit LinkPresentation(LinkStyle style, PersistentRoutePointSequence route = {});

    [[nodiscard]] const LinkStyle& Style() const noexcept;
    [[nodiscard]] const PersistentRoutePointSequence& Route() const noexcept;
    [[nodiscard]] bool SharesStyleWith(const LinkPresentation& other) const noexcept;
    [[nodiscard]] bool operator==(const LinkPresentation& other) const;

private:
    LinkPresentation(
        std::shared_ptr<const LinkStyle> style,
        PersistentRoutePointSequence route) noexcept;

    std::shared_ptr<const LinkStyle> m_style;
    PersistentRoutePointSequence m_route;

    friend class GraphPresentation;
};

enum class GroupKind {
    Group,
    Comment,
};

class GroupMemberSet final {
public:
    using Storage = CowAdjacencyMap<
        NodeId,
        NodeId,
        Detail::CowCopyDomain::GroupMemberships>;
    using const_iterator = Storage::const_iterator;

    GroupMemberSet() = default;
    GroupMemberSet(std::initializer_list<NodeId> members) { Insert(members); }
    GroupMemberSet(const std::vector<NodeId>& members) { Insert(members); }
    GroupMemberSet(std::span<const NodeId> members) { Insert(members); }

    [[nodiscard]] bool empty() const noexcept { return m_members.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return m_members.size(); }
    [[nodiscard]] bool contains(NodeId node) const noexcept { return m_members.contains(node); }
    [[nodiscard]] const_iterator begin() const { return m_members.begin(); }
    [[nodiscard]] const_iterator end() const { return m_members.end(); }

    bool Insert(NodeId node) { return m_members.insert_or_assign(node, node); }
    std::size_t Erase(NodeId node) { return m_members.erase(node); }

    template<std::ranges::input_range Range>
    void Insert(Range&& members) {
        for (const NodeId member : members) Insert(member);
    }

    template<std::ranges::input_range Range>
    void Erase(Range&& members) {
        std::vector<NodeId> removals;
        if constexpr (std::ranges::sized_range<Range>) {
            removals.reserve(std::ranges::size(members));
        }
        for (const NodeId member : members) removals.push_back(member);
        for (const NodeId member : removals) Erase(member);
    }

    bool operator==(const GroupMemberSet&) const = default;

private:
    Storage m_members;
};

struct GroupStyle final {
    std::string title;
    std::string body;
    std::uint32_t color{0x403F3F3FU};
    GroupKind kind{GroupKind::Group};

    bool operator==(const GroupStyle&) const = default;
};

using GroupStyleHandle = std::shared_ptr<const GroupStyle>;

[[nodiscard]] UNI_GUI_EXPORT GroupStyleHandle DefaultGroupStyle();
[[nodiscard]] UNI_GUI_EXPORT GroupStyleHandle MakeGroupStyle(GroupStyle style);

struct GroupGeometry final {
    Vec2 position;
    Vec2 size{320.0f, 180.0f};
    bool collapsed{false};
    std::uint64_t z_order{0};

    bool operator==(const GroupGeometry&) const = default;
};

struct GroupProtection final {
    bool locked{false};

    bool operator==(const GroupProtection&) const = default;
};

struct UNI_GUI_EXPORT GroupPresentation final {
    GroupId id;
    GraphId graph;
    GroupGeometry geometry;
    GroupStyleHandle style{DefaultGroupStyle()};
    GroupMemberSet members;
    GroupProtection protection;

    [[nodiscard]] bool SharesStyleWith(const GroupPresentation& other) const noexcept;
    [[nodiscard]] bool operator==(const GroupPresentation& other) const;
};

using NodePresentationMap =
    CowEntityMap<NodeId, NodePresentation, Detail::CowCopyDomain::NodePresentations>;
using LinkPresentationMap =
    CowEntityMap<LinkId, LinkPresentation, Detail::CowCopyDomain::LinkPresentations>;
using GroupPresentationMap =
    CowEntityMap<GroupId, GroupPresentation, Detail::CowCopyDomain::Groups>;
using GroupAdjacencyRange =
    CowAdjacencyMap<GroupId, GroupId, Detail::CowCopyDomain::PresentationIndexes>;

class UNI_GUI_EXPORT GraphPresentation final {
public:
    GraphPresentation();
    ~GraphPresentation();
    GraphPresentation(GraphPresentation&& other);
    GraphPresentation& operator=(GraphPresentation&& other);
    GraphPresentation(const GraphPresentation&) = delete;
    GraphPresentation& operator=(const GraphPresentation&) = delete;
    void Swap(GraphPresentation& other) noexcept;

    [[nodiscard]] std::uint64_t PresentationRevision() const noexcept;
    [[nodiscard]] std::uint64_t GeometryRevision() const noexcept;
    [[nodiscard]] std::uint64_t Identity() const noexcept;
    [[nodiscard]] std::uint64_t AllocationEpoch() const noexcept;
    [[nodiscard]] GroupId AllocateGroupId() noexcept;
    [[nodiscard]] RoutePointId AllocateRoutePointId() noexcept;

    [[nodiscard]] const NodePresentation* FindNode(NodeId node) const noexcept;
    [[nodiscard]] const LinkPresentation* FindLink(LinkId link) const noexcept;
    [[nodiscard]] const GroupPresentation* FindGroup(GroupId group) const noexcept;
    [[nodiscard]] const RoutePoint* FindRoutePoint(LinkId link, RoutePointId point) const noexcept;
    [[nodiscard]] const NodePresentationMap& Nodes() const noexcept;
    [[nodiscard]] const LinkPresentationMap& Links() const noexcept;
    [[nodiscard]] const GroupPresentationMap& Groups() const noexcept;
    [[nodiscard]] LinkId RoutePointOwner(RoutePointId point) const noexcept;
    [[nodiscard]] const GroupAdjacencyRange& GroupsForNode(NodeId node) const noexcept;
    [[nodiscard]] const GroupAdjacencyRange& GroupsForGraph(GraphId graph) const noexcept;

private:
    void SetNode(NodeId node, std::optional<NodePresentation> value);
    [[nodiscard]] Result<RoutePointIdDelta> SetLink(
        LinkId link,
        std::optional<LinkPresentation> value);
    [[nodiscard]] Result<void> SetLinkRouter(LinkId link, TypeId router);
    [[nodiscard]] Result<void> SetLinkColor(LinkId link, std::optional<std::uint32_t> color);
    [[nodiscard]] Result<void> SetLinkLocked(LinkId link, bool locked);
    [[nodiscard]] Result<RoutePointIdDelta> SetLinkRoute(
        LinkId link,
        PersistentRoutePointSequence route);
    [[nodiscard]] Result<void> InsertRoutePoint(LinkId link, RoutePoint point, std::size_t index);
    [[nodiscard]] Result<void> MoveRoutePoint(LinkId link, RoutePointId point, Vec2 position);
    [[nodiscard]] Result<void> RemoveRoutePoints(LinkId link, std::span<const RoutePointId> points);
    [[nodiscard]] Result<void> AddGroup(GroupPresentation group);
    [[nodiscard]] Result<GroupPresentation> RemoveGroup(GroupId group);
    [[nodiscard]] Result<void> SetGroupPosition(GroupId group, Vec2 position);
    [[nodiscard]] Result<void> SetGroupSize(GroupId group, Vec2 size);
    [[nodiscard]] Result<void> SetGroupCollapsed(GroupId group, bool collapsed);
    [[nodiscard]] Result<void> SetGroupZOrder(GroupId group, std::uint64_t z_order);
    [[nodiscard]] Result<void> SetGroupStyle(GroupId group, GroupStyleHandle style);
    [[nodiscard]] Result<void> SetGroupMembers(GroupId group, GroupMemberSet members);
    [[nodiscard]] Result<void> AddGroupMembers(GroupId group, std::span<const NodeId> members);
    [[nodiscard]] Result<void> RemoveGroupMembers(GroupId group, std::span<const NodeId> members);
    [[nodiscard]] Result<void> SetGroupLocked(GroupId group, bool locked);
    [[nodiscard]] Result<void> Import(
        const GraphDocument& document,
        NodePresentationMap nodes,
        LinkPresentationMap links,
        GroupPresentationMap groups);
    [[nodiscard]] GraphPresentation SnapshotForTransaction() const;
    [[nodiscard]] bool ValidateIndexes() const;
    [[nodiscard]] bool CanCommit(
        std::uint64_t identity,
        std::uint64_t revision,
        std::uint64_t allocation_epoch,
        bool changed,
        bool geometry_changed) const noexcept;
    void CommitFrom(GraphPresentation&& staged, bool changed, bool geometry_changed) noexcept;
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    friend class CommandStack;
    friend class GraphTransaction;
    friend struct Detail::GraphIoAccess;
    friend UNI_GUI_EXPORT Result<void> ValidateGraphPresentation(
        const GraphDocument& document,
        const GraphPresentation& presentation);
};

} // namespace Uni::GUI::Nodes
