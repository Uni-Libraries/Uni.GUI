#pragma once

/// @file
/// @brief Graph-space link geometry and synchronous router callbacks.

#include <uni/gui/export.h>
#include <uni/gui/nodes/presentation.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace Uni::GUI::Nodes {

/// Axis-aligned bounds in graph coordinates, ordered independently on each axis.
struct GraphRect final {
    Vec2 min;
    Vec2 max;
};

/// Pin position and normalized direction away from its owning node.
struct LinkEndpoint final {
    PinId pin;
    NodeId node;
    Vec2 position;
    Vec2 outward_normal;
};

/// A non-hidden node's bounds, including endpoint nodes and nodes outside the viewport.
struct RoutingObstacle final {
    NodeId node;
    GraphRect bounds;
};

/// Directed straight line from start to end in graph units.
struct LinePathSegment final {
    Vec2 start;
    Vec2 end;
};

/// Directed cubic Bezier from p0 to p3 with controls p1 and p2.
struct CubicPathSegment final {
    Vec2 p0;
    Vec2 p1;
    Vec2 p2;
    Vec2 p3;
};

using LinkPathPrimitive = std::variant<LinePathSegment, CubicPathSegment>;

/// Primitive and insertion slot in [0, route_points.size()] for a manual point picked on it.
struct LinkPathSegment final {
    LinkPathPrimitive primitive;
    std::size_t route_point_insert_index{0};
};

/// Nonempty continuous output-to-input path. The editor enforces coordinate and segment limits.
struct LinkPath final {
    std::vector<LinkPathSegment> segments;
};

/// Per-call input: link and obstacles are borrowed; copying route_points retains its immutable generation.
struct LinkRoutingContext final {
    GraphId graph;
    const Link& link;
    LinkEndpoint output;
    LinkEndpoint input;
    PersistentRoutePointSequence route_points;
    std::span<const RoutingObstacle> obstacles;
};

/// Pure calculation: do not mutate the editor, its inputs or registries, allocate IDs, or issue ImGui calls.
using LinkRouterFn = std::function<Result<LinkPath>(const LinkRoutingContext&)>;

struct LinkRouterDescriptor final {
    TypeId type;
    LinkRouterFn callback;
    bool obstacle_aware{false}; ///< Set when moving a non-endpoint obstacle can change the path.
};

[[nodiscard]] UNI_GUI_EXPORT const TypeId& BezierLinkRouterType() noexcept;
[[nodiscard]] UNI_GUI_EXPORT const TypeId& StraightLinkRouterType() noexcept;
[[nodiscard]] UNI_GUI_EXPORT const TypeId& OrthogonalLinkRouterType() noexcept;

/// UI-thread registry, initially populated with Bezier, straight and orthogonal routers.
class UNI_GUI_EXPORT LinkRouterRegistry final {
public:
    LinkRouterRegistry();
    ~LinkRouterRegistry();
    LinkRouterRegistry(LinkRouterRegistry&& other);
    LinkRouterRegistry& operator=(LinkRouterRegistry&& other);
    LinkRouterRegistry(const LinkRouterRegistry&) = delete;
    LinkRouterRegistry& operator=(const LinkRouterRegistry&) = delete;

    /// Requires a unique nonempty type and callable. Failure leaves Revision() unchanged.
    [[nodiscard]] Result<void> Register(LinkRouterDescriptor descriptor);
    [[nodiscard]] bool Unregister(const TypeId& type);
    /// Returns a borrowed descriptor or nullptr; do not retain it across removal or registry replacement.
    [[nodiscard]] const LinkRouterDescriptor* Find(const TypeId& type) const noexcept;
    /// Identifies the registry contents across moves; use with Revision() for cache keys.
    [[nodiscard]] std::uint64_t Identity() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Uni::GUI::Nodes
