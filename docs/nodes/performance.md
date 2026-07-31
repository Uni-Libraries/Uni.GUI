# Performance

Nodes exposes metrics for transaction copying and editor cache behavior, plus opt-in regression benchmark suites. Logical-byte metrics are exact under the deterministic contract below, but they are not allocator, heap, RSS, or stable timing guarantees.

## Transaction Metrics

Reset counters around the operation being measured:

```cpp
using namespace Uni::GUI::Nodes;

ResetTransactionMetrics();
auto changed = commands.Execute(
    std::make_unique<SetNodePropertyCommand>(
        graph, node, "value", PropertyValue{std::int64_t{42}}),
    document, presentation, registry);
const TransactionMetrics metrics = GetTransactionMetrics();
```

`TransactionMetrics` reports `CopyDomainMetrics` for these exact domains:

- `graphs` and `graph_revisions`;
- `node_maps`, `pin_maps`, `link_maps`, and `intergraph_links`;
- `node_presentations`, `link_presentations`, and `groups`;
- `group_styles`, `group_memberships`, and `route_point_sequences`;
- `semantic_indexes` and `presentation_indexes`.

Semantic and presentation entity stores use the same sharded COW shape; high-fanout index values and group membership use persistent path-copy trees. Each domain contains:

- `root_clones`, `directory_clones`, `shard_clones`: cloned levels in a sharded entity store.
- `page_clones`: reserved legacy whole-page accounting; current Nodes primary/index stores do not require whole-map COW pages.
- `value_clones`: individually replaced immutable entity values or graph shells.
- `copied_handles`: shared handles traversed by those clones.
- `logical_bytes`: bytes copied or allocated under the logical-byte contract, attributed exactly once to this domain.

`journal_entries` counts actual changed entries finalized by transaction journals. `operation_intents` and `command_paths` expose policy recording work; both remain zero on the empty-policy fast path. `incremental_records_validated` counts the commit journal validation set, while `full_structure_validations` counts explicit/import full audits. `ownership_summary_lookups`, `dependency_searches`, and `dependency_vertices_visited` distinguish constant-time ownership/existing-edge admission from a real dependency traversal. `route_chunk_merges` and `route_points_reindexed` expose representation compaction work separately from logical owner deltas. Normal command commits keep full audits at zero. `copied_logical_bytes` sums all copy domains.

## Logical-Byte Contract

`logical_bytes` is deterministic for one build and operation. Every recorded COW root, directory, shard, value,
persistent-tree node, route chunk/index shard, or immutable style allocation contributes its fixed object shell plus
the logical handles/entries and explicitly owned payload copied or allocated by that event. Selected semantic
payloads include node names/pins/properties and their owned strings, pin keys/labels, graph names/interface pins,
and their owned strings. It deliberately excludes allocator headers, `shared_ptr` control blocks, hash bucket
overhead, spare capacity not covered by the model, RSS/page effects, and immutable payloads whose handles were
only shared.

The presentation payload domains make large-data assertions exact rather than hiding bytes in an outer entity
estimate:

- `route_point_sequences` counts sequence-root handle arrays, cloned/merged route chunks as
  `sizeof(chunk) + point_count * sizeof(RoutePoint)`, and cloned route ID-index shards. Style-only edits must
  report zero in this domain.
- `group_styles` counts each `MakeGroupStyle()` immutable allocation as `sizeof(GroupStyle) + title.size() +
  body.size()`. Geometry, membership, protection, and retained style undo/redo must report zero.
- `groups` and `link_presentations` count outer sharded entity paths/shells; shared route/style payload bytes are
  counted only in their dedicated domain when actually copied or allocated.
- `presentation_indexes` counts route-owner and group reverse-index path copies. Moving an existing route point or
  changing link style must not touch it; inserting/removing a route point records only the owner delta.

`copied_logical_bytes` is the exact sum of all 14 domain `logical_bytes` fields. Values can differ across
toolchains or ABIs because `sizeof` is part of the contract; use them for same-configuration regression gates,
not cross-platform heap comparisons.

Counters are process-wide atomics. `ResetTransactionMetrics()` affects all users in the process, so isolate measurements and do not reset concurrently. Normal Nodes operations remain main-thread-only despite atomic instrumentation.

`RegistryCatalog` persistent AVL path copies are intentionally excluded from `TransactionMetrics`; they are runtime catalog maintenance, not graph transaction copying. `RegistryUpdateResult::statistics.path_copies` is the dedicated catalog counter.

Expected examples:

- A node property edit clones one graph root/directory/shard/value path, one graph-revision path, one node root/directory/shard path, and one node value, but not unrelated graphs or pin/link/presentation stores.
- A presentation-only node move clones one node-presentation root/directory/shard/value path and no semantic entity map.
- A no-op records no journal entry and advances no revision.

Graph records, graph revisions, semantic node/pin/link/intergraph records, and presentation node/link/group records use 4096 lazy flat shards. Global owner/incidence/dependency/endpoint-pair/route/group indexes are retained in COW snapshots and updated from the mutation journal; adjacency and group-membership mutations path-copy O(log degree). Routes use 256-point immutable chunks plus a sharded ID index. Removal merges adjacent chunks whenever their combined payload fits, so no adjacent mergeable pair remains and historical split fragmentation cannot grow without bound. `StorageStatistics()` exposes occupancy diagnostics and `ValidateStructure()` verifies chunk/index invariants. Group title/body data uses immutable shared style handles. Geometry/membership/protection changes share a group's style generation, while geometry/style/protection changes share its membership root and do not revalidate or rebuild membership indexes. A normal move, route, local/intergraph link, reconnect, node, hierarchy, interface, or group commit validates only affected records and does not call the full document `ValidateStructure()`.

## Editor Metrics

`EditorContext::Metrics()` returns counters accumulated since construction or `ResetMetrics()`:

```cpp
editor.ResetMetrics();
const EditorResult result = DrawEditor(
    editor, document, presentation, commands,
    registry, node_ui, routers);
const EditorMetrics metrics = editor.Metrics();
```

Fields are:

- `geometry_rebuilds`: complete logical geometry-cache rebuilds.
- `routed_links`: router callback invocations, including fallback attempts.
- `spatial_queries`: BVH/spatial index queries.
- `spatial_candidates`: entries returned for filtering by those queries.
- `adaptive_segments`: line pieces generated while adaptively flattening paths.
- `visible_nodes`: nodes processed as visible.
- `visible_link_segments`: link path segments processed as visible.

A cold frame should rebuild geometry and route links. An unchanged warm frame should report zero rebuilds and zero routes. Pan and value/rendering-only properties preserve geometry; graph layout/topology revisions, presentation geometry, layout/router registry revisions, and explicit invalidation rebuild it. The full key is listed in [routing](routing.md#geometry-cache).

Use metrics to assert behavior, not exact wall time. Counts can vary with viewport, interactions, fallback routers, and editor configuration.

## Build Benchmarks

Benchmarks are disabled by default:

```sh
cmake -S . -B build-bench \
  -DCMAKE_BUILD_TYPE=Release \
  -DUNIGUI_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel
ctest --test-dir build-bench -L nodes-performance --output-on-failure
```

`UNIGUI_BUILD_BENCHMARKS` enters the tests directory even when ordinary tests were not requested, builds `unigui_nodes_benchmarks`, and registers each suite as a serial CTest test with a 300-second timeout.

Run one suite directly when investigating output:

```sh
./build-bench/tests/unigui_nodes_benchmarks --suite frames-10k
```

On multi-config generators, use the configuration-specific executable path and `ctest -C Release`.

## Suites And Gates

The executable accepts exactly these suites:

| Suite | Fixture and checks |
|---|---|
| `frames-10k` | 10,000-node chain; cold/warm p50 and p95, spatial culling, cache reuse, peak memory |
| `property-10k` | 200 property edits in a 10,000-node chain; p50/p95, COW domains, journal, copied bytes per edit, peak memory |
| `property-1m` | 50 single-entity edits in a semantic-only one-million-node store; bounded shard handles/logical bytes, p50/p95, peak memory |
| `sparse-100k` | 100,000-node sparse chain; cold/warm p50 and p95, cache/culling, peak memory |
| `dense-links` | 512 nodes with dense fanout; cold/warm p50 and p95, cache/culling, peak memory |
| `io` | Canonical 5,000-node serialize/deserialize and atomic save/load distributions, peak memory |
| `migrations` | 2,000-node baseline, document migration, and node pin-remap migration distributions, peak memory |
| `graph-assets` | leaf/common-dependency replacement, no-op upsert, dependency depth 10/100/1000, callback fanout, record identity and atomic failure |
| `mutations` | move/route/presentation undo-redo at 100k; 400k-point link style/route/history gates; adversarial split/remove route compaction and post-compaction move latency; add/remove/reconnect topology at 100k; subgraph/interface replacement at 100k; 100k caller fanout; 100k-member groups with 16 MiB style bodies; 10k-deep dependency cycles; intergraph fanout; empty/allow policy batches; journal-cardinality, traversal, timing, and exact logical-copy assertions |
| `conversion-catalog` | 10,000 conversion recipes; explicit single/100-record no-op replacements, unregister/register plugin cycle, retained snapshots, path-copy/touched/no-op/publication counters, p50/p95, peak memory |

Frame suites use repeated distributions rather than one sample. Warm gates require no geometry rebuild or rerouting. Property and mutation suites verify bounded COW and journal work; mutation scenarios fail if a hot commit invokes full structural validation. The 400k-route gates cover recolor, lock, router change, one-point move, insert/remove, and route-only undo/redo: style edits require zero `route_point_sequences` and `presentation_indexes` bytes, while route edits have bounded chunk/index deltas. A separate adversarial gate splits many full chunks, removes almost all points, requires a compact one-chunk result, and verifies that subsequent move latency/copy bytes depend on live occupancy rather than edit history. The 16 MiB group/comment-style gate exercises geometry, collapse, z-order, lock, and membership deltas while retaining one large immutable body, then installs a 16 MiB `GroupKind::Comment` replacement and checks exact style allocation plus zero-copy undo/redo; only creation of that replacement is charged to `group_styles`. Asset gates verify immutable record identity, incremental dependency behavior, exact event counts, and atomic rejection. Migration gates include relative overhead and absolute p95 ceilings. IO verifies canonical round-trip before timing.

The thresholds are regression ceilings, not a promise that every supported graph is interactive at a fixed frame rate. Run Release builds on a quiet, representative machine; sanitizer, debug, virtualized, and heavily loaded environments can exceed timing or resident-memory gates without a code regression.

## Application Guidance

- Assign accurate `PropertyImpact` values so runtime/rendering edits do not invalidate geometry.
- Use `NodeUiContext::EditProperty()` to merge continuous gestures.
- Prefer one compound operation to many separately committed edits when they are logically atomic.
- Keep layout, pin-style, router, and policy callbacks deterministic and allocation-conscious.
- Mark routers `obstacle_aware` only when they actually depend on all obstacles.
- Avoid calling `InvalidateGeometry()` every frame; use it only for external layout state not represented by revisions.
- Keep runtime data outside `PropertyBag` when it changes at frame frequency and does not need authoring persistence.

The underlying COW/revision model is described in [semantic model](semantic_model.md#copy-on-write).
