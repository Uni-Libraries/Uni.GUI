#include <uni/gui/nodes/routing.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace Uni::GUI::Nodes {
namespace {

const TypeId BezierRouterType{"uni.gui.nodes.router.bezier"};
const TypeId StraightRouterType{"uni.gui.nodes.router.straight"};
const TypeId OrthogonalRouterType{"uni.gui.nodes.router.orthogonal"};

std::atomic<std::uint64_t> NextRouterRegistryIdentity{1};

[[nodiscard]] std::uint64_t AllocateIdentity() noexcept {
    std::uint64_t identity = NextRouterRegistryIdentity.fetch_add(1, std::memory_order_relaxed);
    if (identity == 0) identity = NextRouterRegistryIdentity.fetch_add(1, std::memory_order_relaxed);
    return identity;
}

constexpr float OrthogonalStubLength = 24.0f;

enum class Axis {
    Horizontal,
    Vertical,
};

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

[[nodiscard]] bool Finite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool SamePoint(const Vec2 first, const Vec2 second) noexcept {
    return first.x == second.x && first.y == second.y;
}

[[nodiscard]] Vec2 Negated(const Vec2 value) noexcept {
    return {-value.x, -value.y};
}

[[nodiscard]] Vec2 Normalize(const Vec2 value, const Vec2 fallback) noexcept {
    const float length = std::hypot(value.x, value.y);
    if (std::isfinite(length) && length > 0.0f) {
        return {value.x / length, value.y / length};
    }
    const float fallback_length = std::hypot(fallback.x, fallback.y);
    if (std::isfinite(fallback_length) && fallback_length > 0.0f) {
        return {fallback.x / fallback_length, fallback.y / fallback_length};
    }
    return {1.0f, 0.0f};
}

[[nodiscard]] Vec2 Direction(const Vec2 from, const Vec2 to, const Vec2 fallback = {1.0f, 0.0f}) noexcept {
    return Normalize(to - from, fallback);
}

[[nodiscard]] Result<void> ValidateCoordinates(const LinkRoutingContext& context, const bool normals) {
    if (!Finite(context.output.position) || !Finite(context.input.position) ||
        (normals && (!Finite(context.output.outward_normal) || !Finite(context.input.outward_normal)))) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link routing endpoints are invalid"));
    }
    if (std::ranges::any_of(context.route_points, [](const RoutePoint& point) {
            return !Finite(point.position);
        })) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link route point is invalid"));
    }
    return {};
}

[[nodiscard]] std::vector<Vec2> Anchors(const LinkRoutingContext& context) {
    std::vector<Vec2> anchors;
    anchors.reserve(context.route_points.size() + 2);
    anchors.push_back(context.output.position);
    for (const auto& point : context.route_points) {
        anchors.push_back(point.position);
    }
    anchors.push_back(context.input.position);
    return anchors;
}

[[nodiscard]] Result<LinkPath> RouteStraight(const LinkRoutingContext& context) {
    if (auto valid = ValidateCoordinates(context, false); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    const auto anchors = Anchors(context);
    LinkPath path;
    path.segments.reserve(anchors.size() - 1);
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index) {
        path.segments.push_back(LinkPathSegment{
            .primitive = LinePathSegment{anchors[index], anchors[index + 1]},
            .route_point_insert_index = index,
        });
    }
    return path;
}

[[nodiscard]] Result<LinkPath> RouteBezier(const LinkRoutingContext& context) {
    if (auto valid = ValidateCoordinates(context, true); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    const auto anchors = Anchors(context);
    std::vector<Vec2> tangents(anchors.size());
    tangents.front() = Normalize(
        context.output.outward_normal,
        Direction(anchors.front(), anchors[1]));
    tangents.back() = Normalize(
        Negated(context.input.outward_normal),
        Direction(anchors[anchors.size() - 2], anchors.back()));
    for (std::size_t index = 1; index + 1 < anchors.size(); ++index) {
        const Vec2 incoming = Direction(anchors[index - 1], anchors[index], {0.0f, 0.0f});
        const Vec2 outgoing = Direction(anchors[index], anchors[index + 1], incoming);
        tangents[index] = Normalize(incoming + outgoing, outgoing);
    }

    LinkPath path;
    path.segments.reserve(anchors.size() - 1);
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index) {
        const float distance = std::hypot(
            anchors[index + 1].x - anchors[index].x,
            anchors[index + 1].y - anchors[index].y);
        if (!std::isfinite(distance)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Bezier link extent is out of range"));
        }
        const float control_distance = distance / 3.0f;
        const CubicPathSegment cubic{
            .p0 = anchors[index],
            .p1 = anchors[index] + tangents[index] * control_distance,
            .p2 = anchors[index + 1] - tangents[index + 1] * control_distance,
            .p3 = anchors[index + 1],
        };
        if (!Finite(cubic.p1) || !Finite(cubic.p2)) {
            return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Bezier link controls are out of range"));
        }
        path.segments.push_back(LinkPathSegment{
            .primitive = cubic,
            .route_point_insert_index = index,
        });
    }
    return path;
}

[[nodiscard]] Vec2 CardinalDirection(const Vec2 value, const Vec2 fallback) noexcept {
    const Vec2 direction = Normalize(value, fallback);
    if (std::abs(direction.x) >= std::abs(direction.y)) {
        return {direction.x < 0.0f ? -1.0f : 1.0f, 0.0f};
    }
    return {0.0f, direction.y < 0.0f ? -1.0f : 1.0f};
}

[[nodiscard]] Axis DirectionAxis(const Vec2 direction) noexcept {
    return direction.x != 0.0f ? Axis::Horizontal : Axis::Vertical;
}

[[nodiscard]] Axis Perpendicular(const Axis axis) noexcept {
    return axis == Axis::Horizontal ? Axis::Vertical : Axis::Horizontal;
}

[[nodiscard]] bool Mergeable(const Vec2 first, const Vec2 middle, const Vec2 last) noexcept {
    if (first.y == middle.y && middle.y == last.y) {
        return (middle.x > first.x && last.x > middle.x) ||
            (middle.x < first.x && last.x < middle.x);
    }
    if (first.x == middle.x && middle.x == last.x) {
        return (middle.y > first.y && last.y > middle.y) ||
            (middle.y < first.y && last.y < middle.y);
    }
    return false;
}

void AppendPoint(std::vector<Vec2>& points, const Vec2 point) {
    if (SamePoint(points.back(), point)) {
        return;
    }
    while (points.size() >= 2 && Mergeable(points[points.size() - 2], points.back(), point)) {
        points.pop_back();
    }
    points.push_back(point);
}

void AppendConnector(
    std::vector<Vec2>& points,
    const Vec2 end,
    const std::optional<Axis> first_axis,
    const std::optional<Axis> last_axis) {
    const Vec2 start = points.back();
    if (start.x == end.x || start.y == end.y) {
        AppendPoint(points, end);
        return;
    }

    if (first_axis && last_axis && *first_axis == *last_axis) {
        if (*first_axis == Axis::Horizontal) {
            const float middle_x = start.x + (end.x - start.x) * 0.5f;
            AppendPoint(points, {middle_x, start.y});
            AppendPoint(points, {middle_x, end.y});
        } else {
            const float middle_y = start.y + (end.y - start.y) * 0.5f;
            AppendPoint(points, {start.x, middle_y});
            AppendPoint(points, {end.x, middle_y});
        }
        AppendPoint(points, end);
        return;
    }

    Axis initial_axis;
    if (first_axis) {
        initial_axis = *first_axis;
    } else if (last_axis) {
        initial_axis = Perpendicular(*last_axis);
    } else {
        initial_axis = std::abs(end.x - start.x) >= std::abs(end.y - start.y)
            ? Axis::Horizontal
            : Axis::Vertical;
    }
    AppendPoint(points, initial_axis == Axis::Horizontal
        ? Vec2{end.x, start.y}
        : Vec2{start.x, end.y});
    AppendPoint(points, end);
}

[[nodiscard]] Result<LinkPath> RouteOrthogonal(const LinkRoutingContext& context) {
    if (auto valid = ValidateCoordinates(context, true); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    const auto anchors = Anchors(context);
    const Vec2 output_direction = CardinalDirection(
        context.output.outward_normal,
        anchors[1] - anchors.front());
    const Vec2 input_direction = CardinalDirection(
        context.input.outward_normal,
        anchors[anchors.size() - 2] - anchors.back());
    const Vec2 output_stub = anchors.front() + output_direction * OrthogonalStubLength;
    const Vec2 input_stub = anchors.back() + input_direction * OrthogonalStubLength;
    if (!Finite(output_stub) || !Finite(input_stub)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Orthogonal link stubs are out of range"));
    }

    LinkPath path;
    path.segments.reserve((anchors.size() - 1) * 4);
    for (std::size_t index = 0; index + 1 < anchors.size(); ++index) {
        std::vector<Vec2> points{anchors[index]};
        if (index == 0) {
            AppendPoint(points, output_stub);
        }
        const bool final_interval = index + 2 == anchors.size();
        const Vec2 connection_end = final_interval ? input_stub : anchors[index + 1];
        const auto first_axis = index == 0
            ? std::optional{Perpendicular(DirectionAxis(output_direction))}
            : std::nullopt;
        const auto last_axis = final_interval
            ? std::optional{Perpendicular(DirectionAxis(input_direction))}
            : std::nullopt;
        AppendConnector(points, connection_end, first_axis, last_axis);
        if (final_interval) {
            AppendPoint(points, anchors.back());
        }
        for (std::size_t point = 0; point + 1 < points.size(); ++point) {
            if (SamePoint(points[point], points[point + 1])) {
                continue;
            }
            path.segments.push_back(LinkPathSegment{
                .primitive = LinePathSegment{points[point], points[point + 1]},
                .route_point_insert_index = index,
            });
        }
    }
    return path;
}

} // namespace

struct LinkRouterRegistry::Impl final {
    std::map<TypeId, LinkRouterDescriptor> descriptors;
    std::uint64_t revision{0};
    std::uint64_t identity{AllocateIdentity()};
};

const TypeId& BezierLinkRouterType() noexcept {
    return BezierRouterType;
}

const TypeId& StraightLinkRouterType() noexcept {
    return StraightRouterType;
}

const TypeId& OrthogonalLinkRouterType() noexcept {
    return OrthogonalRouterType;
}

LinkRouterRegistry::LinkRouterRegistry()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->descriptors.emplace(
        BezierLinkRouterType(),
        LinkRouterDescriptor{BezierLinkRouterType(), RouteBezier});
    m_impl->descriptors.emplace(
        StraightLinkRouterType(),
        LinkRouterDescriptor{StraightLinkRouterType(), RouteStraight});
    m_impl->descriptors.emplace(
        OrthogonalLinkRouterType(),
        LinkRouterDescriptor{OrthogonalLinkRouterType(), RouteOrthogonal});
}

LinkRouterRegistry::~LinkRouterRegistry() = default;

LinkRouterRegistry::LinkRouterRegistry(LinkRouterRegistry&& other)
    : m_impl(std::move(other.m_impl)) {
    other.m_impl = std::make_unique<Impl>();
}

LinkRouterRegistry& LinkRouterRegistry::operator=(LinkRouterRegistry&& other) {
    if (this != &other) {
        auto replacement = std::make_unique<Impl>();
        m_impl = std::move(other.m_impl);
        other.m_impl = std::move(replacement);
    }
    return *this;
}

Result<void> LinkRouterRegistry::Register(LinkRouterDescriptor descriptor) {
    if (descriptor.type.Empty() || !descriptor.callback) {
        return std::unexpected(MakeError(
            ErrorCode::InvalidArgument,
            "Link router descriptor requires a stable type and callback"));
    }
    if (m_impl->descriptors.contains(descriptor.type)) {
        return std::unexpected(MakeError(ErrorCode::DuplicateId, "Link router type is already registered"));
    }
    m_impl->descriptors.emplace(descriptor.type, std::move(descriptor));
    ++m_impl->revision;
    return {};
}

bool LinkRouterRegistry::Unregister(const TypeId& type) {
    if (m_impl->descriptors.erase(type) == 0) {
        return false;
    }
    ++m_impl->revision;
    return true;
}

const LinkRouterDescriptor* LinkRouterRegistry::Find(const TypeId& type) const noexcept {
    const auto found = m_impl->descriptors.find(type);
    return found != m_impl->descriptors.end() ? &found->second : nullptr;
}

std::uint64_t LinkRouterRegistry::Identity() const noexcept {
    return m_impl->identity;
}

std::uint64_t LinkRouterRegistry::Revision() const noexcept {
    return m_impl->revision;
}

} // namespace Uni::GUI::Nodes
