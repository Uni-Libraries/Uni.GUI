#pragma once

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

struct GraphRect final {
    Vec2 min;
    Vec2 max;
};

struct LinkEndpoint final {
    PinId pin;
    NodeId node;
    Vec2 position;
    Vec2 outward_normal;
};

struct RoutingObstacle final {
    NodeId node;
    GraphRect bounds;
};

struct LinePathSegment final {
    Vec2 start;
    Vec2 end;
};

struct CubicPathSegment final {
    Vec2 p0;
    Vec2 p1;
    Vec2 p2;
    Vec2 p3;
};

using LinkPathPrimitive = std::variant<LinePathSegment, CubicPathSegment>;

struct LinkPathSegment final {
    LinkPathPrimitive primitive;
    std::size_t route_point_insert_index{0};
};

struct LinkPath final {
    std::vector<LinkPathSegment> segments;
};

struct LinkRoutingContext final {
    GraphId graph;
    const Link& link;
    LinkEndpoint output;
    LinkEndpoint input;
    PersistentRoutePointSequence route_points;
    std::span<const RoutingObstacle> obstacles;
};

using LinkRouterFn = std::function<Result<LinkPath>(const LinkRoutingContext&)>;

struct LinkRouterDescriptor final {
    TypeId type;
    LinkRouterFn callback;
    bool obstacle_aware{false};
};

[[nodiscard]] UNI_GUI_EXPORT const TypeId& BezierLinkRouterType() noexcept;
[[nodiscard]] UNI_GUI_EXPORT const TypeId& StraightLinkRouterType() noexcept;
[[nodiscard]] UNI_GUI_EXPORT const TypeId& OrthogonalLinkRouterType() noexcept;

class UNI_GUI_EXPORT LinkRouterRegistry final {
public:
    LinkRouterRegistry();
    ~LinkRouterRegistry();
    LinkRouterRegistry(LinkRouterRegistry&& other);
    LinkRouterRegistry& operator=(LinkRouterRegistry&& other);
    LinkRouterRegistry(const LinkRouterRegistry&) = delete;
    LinkRouterRegistry& operator=(const LinkRouterRegistry&) = delete;

    [[nodiscard]] Result<void> Register(LinkRouterDescriptor descriptor);
    [[nodiscard]] bool Unregister(const TypeId& type);
    [[nodiscard]] const LinkRouterDescriptor* Find(const TypeId& type) const noexcept;
    [[nodiscard]] std::uint64_t Identity() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Uni::GUI::Nodes
