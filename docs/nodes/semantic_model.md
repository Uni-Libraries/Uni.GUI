# Semantic Model

The Nodes model is retained and split into semantic and presentation state. This separation lets headless code serialize and validate `GraphDocument` without drawing an editor, while `GraphPresentation` can change independently.

## IDs

`GraphId`, `NodeId`, `PinId`, `LinkId`, `GroupId`, `RoutePointId`, and `IntergraphLinkId` are distinct `StrongId` types backed by `std::uint64_t`. Zero is invalid and converts to `false`; comparison and `IdHash` are provided.

Allocate IDs through their owner:

```cpp
using namespace Uni::GUI::Nodes;

GraphDocument document;
GraphPresentation presentation;

const NodeId node = document.AllocateNodeId();
const PinId pin = document.AllocatePinId();
const GroupId group = presentation.AllocateGroupId();
const RoutePointId route_point = presentation.AllocateRoutePointId();
```

Graph, node, pin, link, and intergraph-link IDs are unique within a document. Group and route-point IDs are unique within a presentation. Allocation reserves identity space and advances `AllocationEpoch()`, but does not advance a persisted revision. Exhaustion returns a zero ID and never wraps. Deserialization observes every stored ID so later allocations remain greater than imported IDs.

`TypeId` and `GraphAssetId` are owning string IDs. Their values are persisted and must be stable; display names are not identifiers.

## Documents And Graphs

A fresh `GraphDocument` is move-only and contains one `GraphLifetime::Reusable` root graph. Public lookup is read-only:

```cpp
const GraphId root = document.RootGraph();
const Graph* graph = document.FindGraph(root);
const NodeInstance* value = document.FindNode(root, NodeId{42});
```

Treat returned pointers, references, and reverse-index ranges as borrowed until the next model mutation or owner move. Structural mutation is private to `GraphTransaction`, so normal application code changes the model through `CommandStack`.

`FindNodeGraph()`, `FindPinOwner()`, and `FindLinkGraph()` use bounded-shard indexed lookup; `FindLinkBetween()` adds an O(log output-degree) persistent-tree lookup. `IncidentLinks()`, `SubgraphCallers()`, `IntergraphLinksForGraph()`, `BoundaryNodes()`, `GroupsForNode()`, and `GroupsForGraph()` return const iterable COW ranges. A miss returns an empty range. Copying a range is cheap and keeps its immutable snapshot alive, while a reference returned directly by an owner is invalidated by the same mutations/moves as other borrowed model references. Range iteration is key ordered; callers must not depend on insertion order.

A `Graph` contains:

- Stable identity, display name, lifetime, interface, and read-only state.
- `NodeMap`, `PinMap`, and `LinkMap` semantic entities.

Per-graph `SemanticRevisionSet` values are stored separately in the document's sharded `GraphRevisionMap`, not inside the heavier `Graph` record.

A `NodeInstance` stores its `TypeId`, persisted descriptor version, display name, typed properties, ordered pin IDs, optional subgraph binding, role, and read-only flag. A `PinInstance` stores a semantic key, label, value type, direction, data/execution kind, cardinality, descriptor/instance ownership through `PinStorage`, and protection. `PinStorage::Static` means descriptor-owned; a configurable descriptor can reconcile those pins over time. A `Link` always names an output and input pin in the same graph.

`GraphDocument::ValidateStructure()` checks document-wide identity, ownership, pin ownership and order, link endpoints, hierarchy, graph interfaces, intergraph links, and dependency cycles. Registry-aware checks are added by `ValidateGraph()`.

## Properties

`PropertyValue` is exactly this closed variant:

- `bool`
- `std::int64_t`
- `double`
- `std::string`
- `Vec2`
- `AssetReference`
- `OpaqueJsonProperty`

Property keys must be non-empty. Doubles and vectors must be finite. `OpaqueJsonProperty` contains a canonical JSON object for an unknown plugin property kind; it is primarily produced by persistence so unknown data can round-trip. It is not a general extension map for arbitrary future document fields.

## Presentation

`GraphPresentation` is also move-only and contains editor state keyed by semantic IDs:

- `NodePresentation`: position, size, collapsed state, z-order, optional color, and lock.
- `LinkPresentation`: immutable `LinkStyle` (`router`, optional `color`, `locked`) plus a chunked `PersistentRoutePointSequence`, exposed by `Style()` and `Route()`.
- `GroupPresentation`: identity/graph plus `GroupGeometry`, immutable shared `GroupStyleHandle`, persistent `GroupMemberSet`, and `GroupProtection`.

Create a complete link presentation with `LinkPresentation{LinkStyle{...}, PersistentRoutePointSequence{...}}`. Link style-only edits retain the route root and do not rewrite route-point ownership indexes. A route sequence is copyable, forward-iterable, indexed by position and `RoutePointId`, and provides `ToVector()` only when contiguous application storage is actually needed.

`GroupGeometry` owns position, size, collapse, and z-order. `GroupStyle` owns title, body, color, and kind; create its non-null immutable handle with `MakeGroupStyle()` (default groups use `DefaultGroupStyle()`). `GroupMemberSet` supplies ordered persistent membership with `contains()`, `Insert()`, and `Erase()`. `GroupProtection` owns `locked`. Geometry, style, membership, and protection can therefore change without copying the unrelated submodels.

Not every semantic node or link needs a presentation entry; the editor supplies fallback geometry. Presentation entries that do exist must not be orphaned. Use `ValidateGraphPresentation()` to check IDs, finite geometry, groups, and globally unique route-point IDs.

Read-only flags are semantic protection. Presentation `locked` flags prevent editor geometry operations. Their command behavior is detailed in [commands and transactions](commands_and_transactions.md).

## Revisions And Identity

`GraphDocument::ModelRevision()` advances once for every successful semantic transaction, regardless of the number of touched entities. `SemanticRevisions()` and `GraphRevisions()` expose:

- `serial`: the model commit sequence for the document or last affecting commit for a graph.
- `topology`: nodes, pins, links, interfaces, hierarchy, or topology-impact properties changed.
- `value`: semantic values changed.
- `layout`: semantic state that can affect node geometry changed.

`NodeTypeDescriptor::property_impacts` maps property keys to revision domains:

| Impact | Revisions advanced |
|---|---|
| `RuntimeOnly` | value |
| `Rendering` | value |
| `Geometry` | value, layout |
| `Topology` | value, layout, topology |

An undeclared property uses `undeclared_property_impact`, which defaults to `Geometry`. The impact selected on the first apply is retained by property undo and redo.

Configurable pin schemas declare their property dependencies separately from this table. If resolving a dependency changes descriptor-owned pin metadata or order, the transaction also advances the required layout/topology domains. A label-only schema change advances layout; structural pin or connection changes advance layout and topology.

`GraphPresentation::PresentationRevision()` advances for any persisted presentation change. `GeometryRevision()` advances only when geometry-relevant fields change; colors and locks do not invalidate geometry by themselves.

`Identity()` distinguishes different document, presentation, and registry instances even when their counters happen to match. `AllocationEpoch()` detects ID reservations while a transaction or pure editor callback is active. Counters are monotonic; a commit fails rather than wrapping an exhausted revision.

## Copy-On-Write

Transactions take shallow snapshots. On first write they copy only the required layers:

1. Graph records use a 4096-shard `GraphMap`; one graph edit clones one root/directory/shard/value path, independent of graph count.
2. Per-graph revisions use a separate sharded `GraphRevisionMap`, so intergraph revision updates do not clone graph records.
3. `NodeMap`, `PinMap`, and `LinkMap` use independent 4096-shard stores and clone only one root/directory/shard path on a single edit.
4. Replacing an entity publishes one new immutable shared value.
5. Node/link/group presentation maps use the same independent sharded COW storage.
6. Intergraph links use the same sharded primary store rather than a whole-map COW page.
7. Derived owner, incidence, hierarchy, dependency, endpoint-pair, route-point, and group indexes are COW-shared and updated atomically with primary state.
8. High-fanout adjacency values and `GroupMemberSet` use immutable persistent trees, so one insertion/removal path-copies O(log degree) records rather than copying a complete vector.
9. `PersistentRoutePointSequence` stores at most 256 points per immutable chunk with a sharded ID-to-chunk index. Moving one point clones the sequence root and one bounded chunk, while insert/remove update only affected chunks and ID/owner index deltas. Removal reaches a fixed point where every adjacent pair exceeds one chunk's capacity, preventing edit-history fragmentation.
10. `GroupStyleHandle` shares immutable title/body storage. Geometry, membership, and protection edits retain the same style generation; style undo/redo can restore retained handles without copying strings.

Consequently, a property edit does not copy pin or link stores, and a presentation-only edit does not copy semantic stores or complete presentation maps. At one million entities a single edit copies a small shard rather than the complete table. Journal commits validate only changed records and affected reverse-index closures. `ValidateStructure()` remains a linear explicit/import audit and verifies primary state against derived indexes.

## Runtime Boundary

The model describes authoring state, not executable state. Revisions can tell an application runtime what category changed, but Nodes defines no evaluator, scheduler, value transport, cancellation, or execution-state snapshot.

`CaptureGraphDocumentSnapshot()` captures the semantic authoring document on the UI thread using shallow COW ownership. `GraphDocumentSnapshot` is copyable, const-only, and may be read by workers after capture while the live document changes. It records source identity/revisions and includes graphs, hierarchy, properties, and intergraph links, but not presentation or runtime state. See [threading and callbacks](threading_and_callbacks.md).
