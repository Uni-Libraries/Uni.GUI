#pragma once

#include <uni/gui/nodes/error.h>
#include <uni/gui/nodes/routing.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Uni::GUI::Nodes::Detail {

enum class SpatialKind {
    Node,
    Pin,
    LinkSegment,
    Group,
    RoutePoint,
};

struct SpatialEntry final {
    GraphRect bounds;
    SpatialKind kind{SpatialKind::Node};
    std::uint64_t id{0};
    std::uint32_t sub_index{0};
};

class SpatialIndex final {
public:
    [[nodiscard]] Result<void> Build(std::span<const SpatialEntry> entries);
    [[nodiscard]] std::vector<SpatialEntry> Query(GraphRect bounds) const;
    [[nodiscard]] std::size_t Size() const noexcept;
    [[nodiscard]] bool Empty() const noexcept;

private:
    struct BvhNode final {
        GraphRect bounds;
        std::size_t left{0};
        std::size_t right{0};
        std::size_t entry{0};
        bool leaf{false};
    };

    [[nodiscard]] std::size_t BuildNode(std::size_t begin, std::size_t end);

    std::vector<SpatialEntry> m_entries;
    std::vector<BvhNode> m_nodes;
};

[[nodiscard]] GraphRect Normalize(GraphRect bounds) noexcept;
[[nodiscard]] GraphRect Expand(GraphRect bounds, float amount) noexcept;
[[nodiscard]] GraphRect Union(GraphRect first, GraphRect second) noexcept;
[[nodiscard]] bool Overlaps(GraphRect first, GraphRect second) noexcept;
[[nodiscard]] bool Contains(GraphRect bounds, Vec2 point) noexcept;
[[nodiscard]] bool Contains(GraphRect outer, GraphRect inner) noexcept;

[[nodiscard]] GraphRect PrimitiveBounds(const LinkPathPrimitive& primitive) noexcept;
[[nodiscard]] GraphRect PrimitiveBounds(const LinkPathSegment& segment) noexcept;
[[nodiscard]] GraphRect PathBounds(const LinkPath& path) noexcept;

[[nodiscard]] Result<void> ValidateLinkPath(
    const LinkPath& path,
    Vec2 expected_start,
    Vec2 expected_end,
    std::size_t max_segments,
    float epsilon = 1.0e-4f,
    float maximum_coordinate = 1.0e9f);

[[nodiscard]] std::vector<Vec2> FlattenPathSegmentAdaptive(
    const LinkPathSegment& segment,
    float tolerance,
    std::size_t max_depth = 12);

[[nodiscard]] float DistanceToPathSegmentAdaptive(
    Vec2 point,
    const LinkPathSegment& segment,
    float tolerance,
    float maximum_distance);

} // namespace Uni::GUI::Nodes::Detail
