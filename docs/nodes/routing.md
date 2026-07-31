# Routing

Links store semantic endpoints in `GraphDocument`; their visual router and manual route points live in `GraphPresentation`. `LinkRouterRegistry` maps stable `TypeId` values to pure path callbacks.

## Built-In Routers

A new registry contains three descriptors:

- `BezierLinkRouterType()`: cubic segments using endpoint normals and smooth tangents through route points.
- `StraightLinkRouterType()`: one line segment between each adjacent endpoint/route-point anchor.
- `OrthogonalLinkRouterType()`: horizontal/vertical line segments with endpoint stubs.

The persisted router for one link is selected with an undoable command:

```cpp
auto changed = commands.Execute(
    std::make_unique<SetLinkRouterCommand>(
        link, OrthogonalLinkRouterType()),
    document, presentation, types);
```

`LinkPresentation::Style()` exposes the selected router. If its `router` is empty, `EditorConfig::default_link_router` is used. The default configuration names the built-in Bezier router. An unknown selected router reports an editor error and attempts Bezier fallback. Router, color, and lock edits use `SetLinkRouterCommand`, `SetLinkColorCommand`, and `SetLinkLockedCommand`; these retain the persistent route and leave its ownership index untouched.

## Custom Router

```cpp
using namespace Uni::GUI::Nodes;

const TypeId step_router{"example.router.step"};
auto registered = routers.Register(LinkRouterDescriptor{
    .type = step_router,
    .callback = [](const LinkRoutingContext& context) -> Result<LinkPath> {
        const Vec2 corner{context.output.position.x, context.input.position.y};
        return LinkPath{
            .segments = {
                LinkPathSegment{
                    .primitive = LinePathSegment{context.output.position, corner},
                    .route_point_insert_index = 0,
                },
                LinkPathSegment{
                    .primitive = LinePathSegment{corner, context.input.position},
                    .route_point_insert_index = 0,
                },
            },
        };
    },
});
```

`LinkRoutingContext` supplies the graph/link, output and input pin/node IDs, graph-coordinate endpoint positions, normalized outward normals from node UI layout, a copyable `PersistentRoutePointSequence`, and all visible-node obstacle bounds. Set `obstacle_aware = true` when moving any node can change a route, not only movement of the link's endpoints.

Registration rejects an empty type, empty callback, or duplicate type. Registering/unregistering advances registry `Revision()`; each registry instance also has a unique `Identity()` for cache safety.

## Path Contract

A router returns ordered `LinePathSegment` and/or `CubicPathSegment` primitives. The editor validates every path before caching or drawing it:

- At least one segment and no more than `EditorConfig::maximum_router_segments`.
- Every coordinate and control point is finite and inside editor coordinate bounds.
- The first primitive starts at `context.output.position`.
- The last primitive ends at `context.input.position`.
- Adjacent primitives are continuous within the editor epsilon.
- Every `route_point_insert_index` is at most `context.route_points.size()`.

`route_point_insert_index` tells editor interaction where a newly inserted manual route point belongs when that path segment is targeted. It is not a segment ID and need not be unique.

Router exceptions are contained and reported through `EditorContext::LastError()`. A failed custom router falls back to Bezier when possible. A callback must not mutate the document, presentation, allocate IDs, or register/unregister routers; such mutation is detected and fails closed with `RevisionConflict`.

## Route Points

Manual route points are presentation entities with globally unique `RoutePointId` values:

```cpp
RoutePoint point{
    .id = presentation.AllocateRoutePointId(),
    .position = {220.0f, 160.0f},
};

auto inserted = commands.Execute(
    std::make_unique<InsertRoutePointCommand>(link, point, 0),
    document, presentation, types);
```

Use `SetLinkRoutePointsCommand`, `InsertRoutePointCommand`, `MoveRoutePointCommand`, and `RemoveRoutePointsCommand` for undoable edits. The sequence stores at most `PersistentRoutePointSequence::ChunkCapacity` (256) points in each immutable chunk and maintains a sharded ID lookup. Moving one point path-copies one root/chunk path. Insert/remove update only affected chunks and route-point owner deltas; removal also coalesces adjacent chunks whose combined payload fits, without changing route-point IDs/order or the global owner index. `StorageStatistics()` reports occupancy and `ValidateStructure()` is the explicit internal-index oracle. `ReconnectLinkCommand(..., preserve_route)` controls whether reconnect retains route state.

The built-in routers treat route points as ordered anchors. Custom routers may choose how to pass through them, but still must return a continuous endpoint-to-endpoint path and valid insertion indexes.

## Custom Pin Placement

Router endpoints are derived from `NodeUiLayout`. Each `PinPlacement` supplies a node-local position and outward normal; these are transformed to graph coordinates before routing. This allows top/bottom ports, radial layouts, or domain-specific shapes without changing semantic pin data. See [custom nodes](custom_nodes.md#arbitrary-pin-layout).

## Geometry Cache

`EditorContext` caches logical node geometry, routed paths, path bounds, and a spatial index. A warm frame reuses the cache when all relevant keys match:

- Active graph and document/presentation/UI/router identities.
- Active graph topology and layout revisions.
- Presentation geometry revision.
- Node UI layout revision and router registry revision.
- Manual invalidation revision.
- ImGui font pointer/size and layout-related editor configuration/style fields.

Value-only properties, rendering-only properties, colors, pin-style changes, pan, and zoom do not rebuild logical geometry. Geometry/topology properties, node movement/size, routes, UI layout registration, router registration, font/layout configuration, or `InvalidateGeometry()` do.

During an active drag, incident links are rerouted transiently. If the selected router is `obstacle_aware`, moving any obstacle reroutes its links for that interaction. The persistent cache is rebuilt after committed geometry changes.

The spatial index culls drawing and hit-test candidates. Cubic picking uses adaptive subdivision controlled by `link_flatten_tolerance` rather than a fixed segment count. Cache counters are documented in [performance](performance.md).

## Flow And Debug Visualization

Flow is an explicit transient visual pulse; it does not imply evaluation and never changes document, presentation, revisions, or history:

```cpp
auto started = editor.TriggerLinkFlow(
    document, graph, link, LinkFlowDirection::OutputToInput);
```

Repeated triggers restart the pulse. `EditorConfig` controls duration, speed, and spacing, while `EditorStyle` controls marker colors and radius. Markers sample the current `LinkPath`, including transient drag/reroute geometry. Offscreen paths are rejected before flattening; visible paths have hard adaptive-work, sampled-point, and marker caps derived from `maximum_router_segments` and the internal 4096-marker ceiling. Keep drawing frames while `EditorResult::animation_active` is true. `ClearLinkFlow()` and `ClearLinkFlows()` remove transient state.

`EditorConfig::debug_overlays` accepts `EditorDebugOverlay` flags for node/body/link bounds, pin normals, route primitives, entity IDs, world bounds, and metrics. These overlays are rendering-only and do not invalidate the geometry cache.
