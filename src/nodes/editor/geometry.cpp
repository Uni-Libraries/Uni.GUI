#include "geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace Uni::GUI::Nodes::Detail {
namespace {

constexpr std::size_t MaximumAdaptiveDepth = 24;

[[nodiscard]] Error MakeError(const ErrorCode code, std::string message) {
    return Error{code, std::move(message)};
}

[[nodiscard]] bool Finite(const Vec2 point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool ValidBounds(const GraphRect bounds) noexcept {
    return Finite(bounds.min) && Finite(bounds.max) &&
        bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y;
}

[[nodiscard]] bool ValidKind(const SpatialKind kind) noexcept {
    switch (kind) {
    case SpatialKind::Node:
    case SpatialKind::Pin:
    case SpatialKind::LinkSegment:
    case SpatialKind::Group:
    case SpatialKind::RoutePoint:
        return true;
    }
    return false;
}

[[nodiscard]] double Center(const GraphRect bounds, const bool x_axis) noexcept {
    const double minimum = x_axis ? bounds.min.x : bounds.min.y;
    const double maximum = x_axis ? bounds.max.x : bounds.max.y;
    return minimum + (maximum - minimum) * 0.5;
}

[[nodiscard]] Vec2 PrimitiveStart(const LinkPathPrimitive& primitive) noexcept {
    return std::visit([](const auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinePathSegment>) {
            return value.start;
        } else {
            return value.p0;
        }
    }, primitive);
}

[[nodiscard]] Vec2 PrimitiveEnd(const LinkPathPrimitive& primitive) noexcept {
    return std::visit([](const auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinePathSegment>) {
            return value.end;
        } else {
            return value.p3;
        }
    }, primitive);
}

[[nodiscard]] bool FinitePrimitive(const LinkPathPrimitive& primitive) noexcept {
    return std::visit([](const auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinePathSegment>) {
            return Finite(value.start) && Finite(value.end);
        } else {
            return Finite(value.p0) && Finite(value.p1) && Finite(value.p2) && Finite(value.p3);
        }
    }, primitive);
}

[[nodiscard]] bool BoundedPrimitive(
    const LinkPathPrimitive& primitive,
    const float maximum_coordinate) noexcept {
    const auto bounded = [maximum_coordinate](const Vec2 point) {
        return Finite(point) && std::abs(point.x) <= maximum_coordinate &&
            std::abs(point.y) <= maximum_coordinate;
    };
    return std::visit([&](const auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinePathSegment>) {
            return bounded(value.start) && bounded(value.end);
        } else {
            return bounded(value.p0) && bounded(value.p1) && bounded(value.p2) && bounded(value.p3);
        }
    }, primitive);
}

[[nodiscard]] bool Close(const Vec2 first, const Vec2 second, const float epsilon) noexcept {
    return std::hypot(
        static_cast<double>(first.x) - second.x,
        static_cast<double>(first.y) - second.y) <= epsilon;
}

[[nodiscard]] Vec2 Midpoint(const Vec2 first, const Vec2 second) noexcept {
    return {
        std::midpoint(first.x, second.x),
        std::midpoint(first.y, second.y),
    };
}

struct CubicSplit final {
    CubicPathSegment left;
    CubicPathSegment right;
};

[[nodiscard]] CubicSplit Split(const CubicPathSegment& cubic) noexcept {
    const Vec2 p01 = Midpoint(cubic.p0, cubic.p1);
    const Vec2 p12 = Midpoint(cubic.p1, cubic.p2);
    const Vec2 p23 = Midpoint(cubic.p2, cubic.p3);
    const Vec2 p012 = Midpoint(p01, p12);
    const Vec2 p123 = Midpoint(p12, p23);
    const Vec2 p0123 = Midpoint(p012, p123);
    return {
        .left = CubicPathSegment{cubic.p0, p01, p012, p0123},
        .right = CubicPathSegment{p0123, p123, p23, cubic.p3},
    };
}

[[nodiscard]] double DistanceSquaredToLineSegment(
    const Vec2 point,
    const Vec2 start,
    const Vec2 end) noexcept {
    const double segment_x = static_cast<double>(end.x) - start.x;
    const double segment_y = static_cast<double>(end.y) - start.y;
    const double length_squared = segment_x * segment_x + segment_y * segment_y;
    if (length_squared == 0.0) {
        const double x = static_cast<double>(point.x) - start.x;
        const double y = static_cast<double>(point.y) - start.y;
        return x * x + y * y;
    }

    const double relative_x = static_cast<double>(point.x) - start.x;
    const double relative_y = static_cast<double>(point.y) - start.y;
    const double projection = std::clamp(
        (relative_x * segment_x + relative_y * segment_y) / length_squared,
        0.0,
        1.0);
    const double x = relative_x - segment_x * projection;
    const double y = relative_y - segment_y * projection;
    return x * x + y * y;
}

[[nodiscard]] bool FlatEnough(const CubicPathSegment& cubic, const double tolerance_squared) noexcept {
    return DistanceSquaredToLineSegment(cubic.p1, cubic.p0, cubic.p3) <= tolerance_squared &&
        DistanceSquaredToLineSegment(cubic.p2, cubic.p0, cubic.p3) <= tolerance_squared;
}

void FlattenCubic(
    const CubicPathSegment& cubic,
    const double tolerance_squared,
    const std::size_t depth,
    const std::size_t max_depth,
    std::vector<Vec2>& output) {
    if (depth >= max_depth || FlatEnough(cubic, tolerance_squared)) {
        output.push_back(cubic.p3);
        return;
    }

    const CubicSplit halves = Split(cubic);
    FlattenCubic(halves.left, tolerance_squared, depth + 1, max_depth, output);
    FlattenCubic(halves.right, tolerance_squared, depth + 1, max_depth, output);
}

[[nodiscard]] float ToFloatDistance(const double squared_distance) noexcept {
    const double distance = std::sqrt(squared_distance);
    if (distance >= std::numeric_limits<float>::max()) {
        return std::numeric_limits<float>::max();
    }
    return static_cast<float>(distance);
}

[[nodiscard]] GraphRect InvalidBounds() noexcept {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    return {{nan, nan}, {nan, nan}};
}

[[nodiscard]] double EvaluateCubicAxis(
    const double p0,
    const double p1,
    const double p2,
    const double p3,
    const double t) noexcept {
    const double p01 = p0 + (p1 - p0) * t;
    const double p12 = p1 + (p2 - p1) * t;
    const double p23 = p2 + (p3 - p2) * t;
    const double p012 = p01 + (p12 - p01) * t;
    const double p123 = p12 + (p23 - p12) * t;
    return p012 + (p123 - p012) * t;
}

void IncludeCubicExtrema(
    const float fp0,
    const float fp1,
    const float fp2,
    const float fp3,
    float& minimum,
    float& maximum) noexcept {
    const double p0 = fp0;
    const double p1 = fp1;
    const double p2 = fp2;
    const double p3 = fp3;
    const double a = -p0 + 3.0 * p1 - 3.0 * p2 + p3;
    const double b = 2.0 * (p0 - 2.0 * p1 + p2);
    const double c = p1 - p0;

    const auto include_root = [&](const double root) {
        if (root > 0.0 && root < 1.0) {
            const float value = static_cast<float>(EvaluateCubicAxis(p0, p1, p2, p3, root));
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    };

    if (a == 0.0) {
        if (b != 0.0) {
            include_root(-c / b);
        }
        return;
    }

    double discriminant = b * b - 4.0 * a * c;
    const double discriminant_scale = b * b + std::abs(4.0 * a * c);
    if (discriminant < 0.0 &&
        discriminant >= -std::numeric_limits<double>::epsilon() * 8.0 * discriminant_scale) {
        discriminant = 0.0;
    }
    if (discriminant < 0.0) {
        return;
    }

    const double root_discriminant = std::sqrt(discriminant);
    const double q = -0.5 * (b + std::copysign(root_discriminant, b));
    if (q == 0.0) {
        include_root(-b / (2.0 * a));
        return;
    }
    include_root(q / a);
    include_root(c / q);
}

} // namespace

Vec2 EditorViewTransform::ToScreen(const Vec2 graph_position) const noexcept {
    return canvas_origin + (pan + graph_position * zoom) * ui_scale;
}

Vec2 EditorViewTransform::ToGraph(const Vec2 screen_position) const noexcept {
    return ((screen_position - canvas_origin) * (1.0f / ui_scale) - pan) * (1.0f / zoom);
}

float EditorViewTransform::GraphScale() const noexcept {
    return ui_scale * zoom;
}

float MeasureNodeHeaderHeight(
    const float reference_font_size,
    const NodeHeaderLayout& layout) noexcept {
    float text_height = reference_font_size * layout.primary_text_scale;
    if (layout.maximum_text_lines > 1) {
        text_height += layout.line_spacing + reference_font_size * layout.secondary_text_scale;
    }
    return std::max(
        layout.minimum_height,
        layout.vertical_padding * 2.0f + std::max(text_height, layout.item_height));
}

Result<void> SpatialIndex::Build(const std::span<const SpatialEntry> entries) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (!ValidBounds(entries[index].bounds)) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidArgument,
                "Spatial entry " + std::to_string(index) + " has invalid bounds"));
        }
        if (!ValidKind(entries[index].kind)) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidArgument,
                "Spatial entry " + std::to_string(index) + " has an invalid kind"));
        }
    }

    SpatialIndex candidate;
    candidate.m_entries.assign(entries.begin(), entries.end());
    if (!candidate.m_entries.empty()) {
        candidate.m_nodes.reserve(candidate.m_entries.size() * 2 - 1);
        (void)candidate.BuildNode(0, candidate.m_entries.size());
    }
    m_entries.swap(candidate.m_entries);
    m_nodes.swap(candidate.m_nodes);
    return {};
}

std::size_t SpatialIndex::BuildNode(const std::size_t begin, const std::size_t end) {
    const std::size_t node_index = m_nodes.size();
    m_nodes.emplace_back();
    if (end - begin == 1) {
        m_nodes[node_index] = BvhNode{
            .bounds = m_entries[begin].bounds,
            .entry = begin,
            .leaf = true,
        };
        return node_index;
    }

    GraphRect bounds = m_entries[begin].bounds;
    for (std::size_t index = begin + 1; index < end; ++index) {
        bounds = Union(bounds, m_entries[index].bounds);
    }
    const double width = static_cast<double>(bounds.max.x) - bounds.min.x;
    const double height = static_cast<double>(bounds.max.y) - bounds.min.y;
    const bool x_axis = width >= height;
    const std::size_t middle = begin + (end - begin) / 2;
    std::nth_element(
        m_entries.begin() + static_cast<std::ptrdiff_t>(begin),
        m_entries.begin() + static_cast<std::ptrdiff_t>(middle),
        m_entries.begin() + static_cast<std::ptrdiff_t>(end),
        [x_axis](const SpatialEntry& first, const SpatialEntry& second) {
            const auto first_key = std::tuple{
                Center(first.bounds, x_axis),
                Center(first.bounds, !x_axis),
                first.kind,
                first.id,
                first.sub_index,
                first.bounds.min.x,
                first.bounds.min.y,
                first.bounds.max.x,
                first.bounds.max.y,
            };
            const auto second_key = std::tuple{
                Center(second.bounds, x_axis),
                Center(second.bounds, !x_axis),
                second.kind,
                second.id,
                second.sub_index,
                second.bounds.min.x,
                second.bounds.min.y,
                second.bounds.max.x,
                second.bounds.max.y,
            };
            return first_key < second_key;
        });

    const std::size_t left = BuildNode(begin, middle);
    const std::size_t right = BuildNode(middle, end);
    m_nodes[node_index] = BvhNode{
        .bounds = Union(m_nodes[left].bounds, m_nodes[right].bounds),
        .left = left,
        .right = right,
    };
    return node_index;
}

std::vector<SpatialEntry> SpatialIndex::Query(const GraphRect bounds) const {
    std::vector<SpatialEntry> result;
    if (m_nodes.empty() || !ValidBounds(bounds)) {
        return result;
    }

    std::vector<std::size_t> pending;
    pending.reserve(32);
    pending.push_back(0);
    while (!pending.empty()) {
        const std::size_t node_index = pending.back();
        pending.pop_back();
        const BvhNode& node = m_nodes[node_index];
        if (!Overlaps(node.bounds, bounds)) {
            continue;
        }
        if (node.leaf) {
            result.push_back(m_entries[node.entry]);
            continue;
        }
        pending.push_back(node.right);
        pending.push_back(node.left);
    }

    std::ranges::sort(result, [](const SpatialEntry& first, const SpatialEntry& second) {
        return std::tuple{first.kind, first.id, first.sub_index} <
            std::tuple{second.kind, second.id, second.sub_index};
    });
    return result;
}

std::size_t SpatialIndex::Size() const noexcept {
    return m_entries.size();
}

bool SpatialIndex::Empty() const noexcept {
    return m_entries.empty();
}

GraphRect Normalize(const GraphRect bounds) noexcept {
    if (!Finite(bounds.min) || !Finite(bounds.max)) {
        return InvalidBounds();
    }
    return {
        .min = {std::min(bounds.min.x, bounds.max.x), std::min(bounds.min.y, bounds.max.y)},
        .max = {std::max(bounds.min.x, bounds.max.x), std::max(bounds.min.y, bounds.max.y)},
    };
}

GraphRect Expand(const GraphRect bounds, const float amount) noexcept {
    return {
        .min = {bounds.min.x - amount, bounds.min.y - amount},
        .max = {bounds.max.x + amount, bounds.max.y + amount},
    };
}

GraphRect Union(const GraphRect first, const GraphRect second) noexcept {
    if (!ValidBounds(first) || !ValidBounds(second)) {
        return InvalidBounds();
    }
    return {
        .min = {std::min(first.min.x, second.min.x), std::min(first.min.y, second.min.y)},
        .max = {std::max(first.max.x, second.max.x), std::max(first.max.y, second.max.y)},
    };
}

bool Overlaps(const GraphRect first, const GraphRect second) noexcept {
    return first.min.x <= second.max.x && first.max.x >= second.min.x &&
        first.min.y <= second.max.y && first.max.y >= second.min.y;
}

bool Contains(const GraphRect bounds, const Vec2 point) noexcept {
    return point.x >= bounds.min.x && point.x <= bounds.max.x &&
        point.y >= bounds.min.y && point.y <= bounds.max.y;
}

bool Contains(const GraphRect outer, const GraphRect inner) noexcept {
    return inner.min.x >= outer.min.x && inner.max.x <= outer.max.x &&
        inner.min.y >= outer.min.y && inner.max.y <= outer.max.y;
}

GraphRect PrimitiveBounds(const LinkPathPrimitive& primitive) noexcept {
    return std::visit([](const auto& value) -> GraphRect {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinePathSegment>) {
            if (!Finite(value.start) || !Finite(value.end)) {
                return InvalidBounds();
            }
            return Normalize({value.start, value.end});
        } else {
            if (!Finite(value.p0) || !Finite(value.p1) || !Finite(value.p2) || !Finite(value.p3)) {
                return InvalidBounds();
            }
            GraphRect bounds = Normalize({value.p0, value.p3});
            IncludeCubicExtrema(value.p0.x, value.p1.x, value.p2.x, value.p3.x, bounds.min.x, bounds.max.x);
            IncludeCubicExtrema(value.p0.y, value.p1.y, value.p2.y, value.p3.y, bounds.min.y, bounds.max.y);
            return bounds;
        }
    }, primitive);
}

GraphRect PrimitiveBounds(const LinkPathSegment& segment) noexcept {
    return PrimitiveBounds(segment.primitive);
}

GraphRect PathBounds(const LinkPath& path) noexcept {
    if (path.segments.empty()) {
        return {};
    }
    GraphRect bounds = PrimitiveBounds(path.segments.front());
    for (std::size_t index = 1; index < path.segments.size(); ++index) {
        bounds = Union(bounds, PrimitiveBounds(path.segments[index]));
    }
    return bounds;
}

Result<void> ValidateLinkPath(
    const LinkPath& path,
    const Vec2 expected_start,
    const Vec2 expected_end,
    const std::size_t max_segments,
    const float epsilon,
    const float maximum_coordinate) {
    if (!Finite(expected_start) || !Finite(expected_end) ||
        !std::isfinite(maximum_coordinate) || maximum_coordinate <= 0.0f ||
        std::abs(expected_start.x) > maximum_coordinate ||
        std::abs(expected_start.y) > maximum_coordinate ||
        std::abs(expected_end.x) > maximum_coordinate ||
        std::abs(expected_end.y) > maximum_coordinate) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Expected link endpoints must be finite"));
    }
    if (!std::isfinite(epsilon) || epsilon < 0.0f) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link path epsilon is invalid"));
    }
    if (path.segments.empty()) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link path must contain at least one segment"));
    }
    if (path.segments.size() > max_segments) {
        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Link path contains too many segments"));
    }

    for (std::size_t index = 0; index < path.segments.size(); ++index) {
        const LinkPathPrimitive& primitive = path.segments[index].primitive;
        if (!BoundedPrimitive(primitive, maximum_coordinate)) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidArgument,
                "Link path segment " + std::to_string(index) + " is outside editor coordinate bounds"));
        }
        if (index != 0 && !Close(
                PrimitiveEnd(path.segments[index - 1].primitive),
                PrimitiveStart(primitive),
                epsilon)) {
            return std::unexpected(MakeError(
                ErrorCode::InvalidArgument,
                "Link path is discontinuous before segment " + std::to_string(index)));
        }
    }

    if (!Close(PrimitiveStart(path.segments.front().primitive), expected_start, epsilon)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link path does not start at the expected endpoint"));
    }
    if (!Close(PrimitiveEnd(path.segments.back().primitive), expected_end, epsilon)) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Link path does not end at the expected endpoint"));
    }
    return {};
}

std::vector<Vec2> FlattenPathSegmentAdaptive(
    const LinkPathSegment& segment,
    const float tolerance,
    const std::size_t max_depth) {
    if (!FinitePrimitive(segment.primitive)) {
        return {};
    }
    if (const auto* line = std::get_if<LinePathSegment>(&segment.primitive)) {
        return {line->start, line->end};
    }
    if (!std::isfinite(tolerance) || tolerance <= 0.0f) {
        return {};
    }

    const auto& cubic = std::get<CubicPathSegment>(segment.primitive);
    std::vector<Vec2> output;
    output.reserve(16);
    output.push_back(cubic.p0);
    const double double_tolerance = tolerance;
    FlattenCubic(
        cubic,
        double_tolerance * double_tolerance,
        0,
        std::min(max_depth, MaximumAdaptiveDepth),
        output);
    return output;
}

float DistanceToPathSegmentAdaptive(
    const Vec2 point,
    const LinkPathSegment& segment,
    const float tolerance,
    const float maximum_distance) {
    if (!Finite(point) || !FinitePrimitive(segment.primitive) ||
        std::isnan(maximum_distance) || maximum_distance < 0.0f) {
        return std::numeric_limits<float>::max();
    }

    const GraphRect bounds = PrimitiveBounds(segment);
    if (!Contains(Expand(bounds, maximum_distance), point)) {
        return std::numeric_limits<float>::max();
    }
    if (const auto* line = std::get_if<LinePathSegment>(&segment.primitive)) {
        return ToFloatDistance(DistanceSquaredToLineSegment(point, line->start, line->end));
    }

    const std::vector<Vec2> flattened = FlattenPathSegmentAdaptive(segment, tolerance);
    if (flattened.size() < 2) {
        return std::numeric_limits<float>::max();
    }
    double minimum_squared = std::numeric_limits<double>::max();
    for (std::size_t index = 1; index < flattened.size(); ++index) {
        minimum_squared = std::min(
            minimum_squared,
            DistanceSquaredToLineSegment(point, flattened[index - 1], flattened[index]));
    }
    return ToFloatDistance(minimum_squared);
}

} // namespace Uni::GUI::Nodes::Detail
