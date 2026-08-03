#include "nodes/editor/geometry.h"

#include <uni/gui/nodes/routing.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

using namespace Uni::GUI::Nodes;
using namespace Uni::GUI::Nodes::Detail;

[[noreturn]] void Fail(const std::string_view message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void Expect(const bool condition, const std::string_view message) {
    if (!condition) Fail(message);
}

void TestSpatialIndexAgainstBruteForce() {
    std::vector<SpatialEntry> entries;
    entries.reserve(10'000);
    for (std::uint64_t index = 0; index < 10'000; ++index) {
        const float x = static_cast<float>(index % 100) * 37.0f;
        const float y = static_cast<float>(index / 100) * 29.0f;
        entries.push_back(SpatialEntry{
            .bounds = {{x, y}, {x + 23.0f, y + 17.0f}},
            .kind = static_cast<SpatialKind>(index % 5),
            .id = index + 1,
            .sub_index = static_cast<std::uint32_t>(index % 7),
        });
    }

    SpatialIndex index;
    Expect(index.Build(entries).has_value() && index.Size() == entries.size(),
        "Spatial index must accept a large deterministic data set");
    for (std::uint64_t query_index = 0; query_index < 200; ++query_index) {
        const float x = static_cast<float>((query_index * 43) % 3600);
        const float y = static_cast<float>((query_index * 61) % 2800);
        const GraphRect query{{x, y}, {x + 130.0f, y + 95.0f}};
        auto actual = index.Query(query);
        std::vector<SpatialEntry> expected;
        for (const auto& entry : entries) {
            if (Overlaps(entry.bounds, query)) expected.push_back(entry);
        }
        const auto order = [](const SpatialEntry& first, const SpatialEntry& second) {
            if (first.kind != second.kind) return first.kind < second.kind;
            if (first.id != second.id) return first.id < second.id;
            return first.sub_index < second.sub_index;
        };
        std::ranges::sort(expected, order);
        Expect(actual.size() == expected.size(), "BVH query must match brute-force candidate count");
        for (std::size_t candidate = 0; candidate < expected.size(); ++candidate) {
            Expect(actual[candidate].kind == expected[candidate].kind &&
                    actual[candidate].id == expected[candidate].id &&
                    actual[candidate].sub_index == expected[candidate].sub_index,
                "BVH query must return deterministic brute-force-equivalent entries");
        }
    }

    auto invalid = entries;
    invalid.front().bounds.min.x = std::numeric_limits<float>::quiet_NaN();
    Expect(!index.Build(invalid) && index.Size() == entries.size(),
        "A failed spatial rebuild must preserve the previous index transactionally");
}

void TestAdaptiveLinkGeometry() {
    const LinkPathSegment flat{
        .primitive = CubicPathSegment{{0.0f, 0.0f}, {30.0f, 0.0f}, {70.0f, 0.0f}, {100.0f, 0.0f}},
    };
    const LinkPathSegment curved{
        .primitive = CubicPathSegment{{0.0f, 0.0f}, {0.0f, 150.0f}, {100.0f, -150.0f}, {100.0f, 0.0f}},
    };
    const auto flat_points = FlattenPathSegmentAdaptive(flat, 0.25f);
    const auto curved_points = FlattenPathSegmentAdaptive(curved, 0.25f);
    Expect(flat_points.size() == 2 && curved_points.size() > flat_points.size(),
        "Adaptive subdivision must avoid fixed tessellation and refine curved cubics");
    Expect(DistanceToPathSegmentAdaptive({50.0f, 0.0f}, flat, 0.25f, 2.0f) < 0.001f,
        "Adaptive hit testing must hit a point on a cubic");
    Expect(DistanceToPathSegmentAdaptive({50.0f, 10.0f}, flat, 0.25f, 2.0f) ==
            std::numeric_limits<float>::max(),
        "Adaptive hit testing must reject a point outside the broad-phase radius");

    const LinkPath path{{flat, curved}};
    Expect(!ValidateLinkPath(path, {0.0f, 0.0f}, {100.0f, 0.0f}, 8),
        "Discontinuous custom paths must be rejected");
    const LinkPath unbounded{{LinkPathSegment{
        LinePathSegment{{0.0f, 0.0f}, {std::numeric_limits<float>::max(), 0.0f}}, 0}}};
    Expect(!ValidateLinkPath(
            unbounded, {0.0f, 0.0f}, {std::numeric_limits<float>::max(), 0.0f}, 8),
        "Router paths outside editor coordinate bounds must be rejected");
}

void TestBuiltInAndCustomRouters() {
    LinkRouterRegistry routers;
    Link link{.id = LinkId{1}, .output = PinId{1}, .input = PinId{2}};
    const std::vector<RoutePoint> points{{RoutePointId{1}, {80.0f, 40.0f}}};
    const LinkRoutingContext context{
        .graph = GraphId{1},
        .link = link,
        .output = {PinId{1}, NodeId{1}, {0.0f, 0.0f}, {0.0f, -1.0f}},
        .input = {PinId{2}, NodeId{2}, {160.0f, 100.0f}, {0.0f, 1.0f}},
        .route_points = PersistentRoutePointSequence{points},
    };

    for (const TypeId* type : {
             &BezierLinkRouterType(),
             &StraightLinkRouterType(),
             &OrthogonalLinkRouterType(),
         }) {
        const auto* descriptor = routers.Find(*type);
        Expect(descriptor != nullptr, "Every built-in router must be registered");
        auto path = descriptor->callback(context);
        Expect(path && ValidateLinkPath(*path, context.output.position, context.input.position, 64),
            "Every built-in router must return a finite continuous endpoint-preserving path");
        Expect(std::ranges::all_of(path->segments, [&](const LinkPathSegment& segment) {
            return segment.route_point_insert_index <= points.size();
        }), "Every built-in segment must map to a route-point insertion interval");
        if (*type == OrthogonalLinkRouterType()) {
            Expect(std::ranges::all_of(path->segments, [](const LinkPathSegment& segment) {
                const auto* line = std::get_if<LinePathSegment>(&segment.primitive);
                return line != nullptr && (line->start.x == line->end.x || line->start.y == line->end.y);
            }), "Orthogonal routing must contain only axis-aligned line segments");
        }
    }

    const std::uint64_t before = routers.Revision();
    const TypeId custom{"test.router.step"};
    Expect(routers.Register(LinkRouterDescriptor{
               .type = custom,
               .callback = [](const LinkRoutingContext& route) -> Result<LinkPath> {
                   return LinkPath{{
                       LinkPathSegment{LinePathSegment{route.output.position,
                           {route.input.position.x, route.output.position.y}}, 0},
                       LinkPathSegment{LinePathSegment{{route.input.position.x, route.output.position.y},
                           route.input.position}, route.route_points.size()},
                   }};
               },
           }).has_value() && routers.Revision() == before + 1,
        "Custom router registration must update registry revision");
    Expect(routers.Unregister(custom) && routers.Revision() == before + 2,
        "Custom router removal must update registry revision");
}

void TestEditorViewScaling() {
    for (const float ui_scale : {1.0f, 1.25f, 2.0f}) {
        for (const float zoom : {0.5f, 1.0f, 2.0f}) {
            const EditorViewTransform transform{
                .canvas_origin = {17.0f, 23.0f},
                .pan = {31.0f, -19.0f},
                .ui_scale = ui_scale,
                .zoom = zoom,
            };
            const Vec2 graph{143.0f, -57.0f};
            const Vec2 screen = transform.ToScreen(graph);
            const Vec2 restored = transform.ToGraph(screen);
            Expect(std::abs(restored.x - graph.x) < 0.001f &&
                       std::abs(restored.y - graph.y) < 0.001f,
                   "Graph and screen transforms must round-trip at every UI scale and zoom");
            Expect(std::abs(transform.GraphScale() - ui_scale * zoom) < 0.001f,
                   "Graph geometry must compose UI scale and editor zoom exactly once");
        }
    }

    const EditorViewTransform first{
        .canvas_origin = {10.0f, 20.0f},
        .pan = {40.0f, -25.0f},
        .ui_scale = 1.0f,
        .zoom = 1.5f,
    };
    const EditorViewTransform second{
        .canvas_origin = first.canvas_origin,
        .pan = first.pan,
        .ui_scale = 2.0f,
        .zoom = first.zoom,
    };
    const Vec2 graph{80.0f, 60.0f};
    const Vec2 first_offset = first.ToScreen(graph) - first.canvas_origin;
    const Vec2 second_offset = second.ToScreen(graph) - second.canvas_origin;
    Expect(std::abs(second_offset.x - first_offset.x * 2.0f) < 0.001f &&
               std::abs(second_offset.y - first_offset.y * 2.0f) < 0.001f &&
               first.ToGraph(first.canvas_origin) == second.ToGraph(second.canvas_origin),
           "A DPI transition must scale view geometry while preserving reference-unit pan semantics");
}

void TestMeasuredHeaderLayout() {
    NodeHeaderLayout single_line;
    const float single_height = MeasureNodeHeaderHeight(16.0f, single_line);
    NodeHeaderLayout two_lines = single_line;
    two_lines.maximum_text_lines = 2;
    const float two_line_height = MeasureNodeHeaderHeight(16.0f, two_lines);
    Expect(single_height == 28.0f && two_line_height > single_height,
           "Header height must reserve configured text lines from reference font metrics");
    Expect(MeasureNodeHeaderHeight(16.0f, two_lines) ==
               MeasureNodeHeaderHeight(32.0f / 2.0f, two_lines),
           "Normalized font metrics must keep graph header height invariant across DPI scales");
}

} // namespace

int main() {
    TestSpatialIndexAgainstBruteForce();
    TestAdaptiveLinkGeometry();
    TestBuiltInAndCustomRouters();
    TestEditorViewScaling();
    TestMeasuredHeaderLayout();
    return EXIT_SUCCESS;
}
