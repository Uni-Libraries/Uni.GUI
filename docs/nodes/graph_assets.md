# Graph Assets

`GraphAsset` packages a stable `GraphAssetId`, an independent `GraphDocument`, its `GraphPresentation`, and application metadata `source_uri`. `GraphAssetRegistry` publishes immutable owning records and enforces dependency integrity across the complete registry.

## Insert, Replace, And Upsert

```cpp
using namespace Uni::GUI::Nodes;

GraphAsset asset;
asset.id = GraphAssetId{"filters.low_pass"};
asset.source_uri = "file:///project/filters/low_pass.graph.json";

GraphAssetRegistry assets;
auto written = assets.Write(std::move(asset), GraphAssetWriteMode::Insert);
if (!written) Report(written.error().message);
```

Modes are strict:

- `Insert` fails with `DuplicateId` if the ID exists.
- `Replace` fails with `AssetNotFound` if the ID does not exist.
- `Upsert` accepts either state.

Before a successful mutation, the registry validates the incoming document/presentation, root graph, dependency uses, cycles containing the changed record, and the affected reverse dependency closure. Referenced dependencies must already be registered. `ValidateAll()` remains the explicit full diagnostic audit. Failed writes leave all records, generations, hashes, and reverse indexes unchanged.

`Unregister()` fails with `AssetInUse` while any registered asset depends on the target. Remove dependents first. `ValidateAll()` repeats complete registry validation.

## Generation And Content Hash

The first insert has generation 1. A successful content or `source_uri` change increments generation; overflow returns `GenerationOverflow`. An upsert/replace with the same canonical content and source URI returns `GraphAssetWriteStatus::Unchanged`, preserves generation, and emits no event. `GraphAssetChangeFlags` distinguishes content, source URI, dependency-list, and root-interface changes.

`GraphAssetContentHash` is SHA-256 over canonical `SerializeGraphAssetJson()` output:

```cpp
const GraphAssetRecordPtr record = assets.Find(GraphAssetId{"filters.low_pass"});
if (record) {
    Log(record->Generation(), record->ContentHash().ToHex());
}
```

`source_uri` is not serialized and is not part of the hash. A URI-only replacement therefore advances generation while retaining the same content hash.

`GraphAssetRecordPtr` owns an immutable snapshot. A record and its `Asset()` remain valid across replacement, unregister, registry move, and registry destruction. Unchanged records retain pointer identity across writes because candidate states shallow-copy record pointers.

## Dependencies

Dependencies are collected from every `GraphAssetTarget` in the asset document. The registry exposes stable, sorted queries:

```cpp
auto direct = assets.DirectDependencies(id);
auto closure = assets.DependencyClosure(id);
auto users = assets.DirectDependents(id);
auto all_users = assets.DependentClosure(id);
```

Every query reports `AssetNotFound` for an unregistered starting ID. Closures are iterative and do not depend on recursion depth.

Replacing a dependency must leave every registered dependent's stored interface snapshot valid. An incompatible interface replacement is rejected atomically; update the dependent documents and dependency in an order that always forms a valid registry, or rebuild a fresh registry and swap it at the application level.

For an ordinary working document that is not itself in the registry, use:

```cpp
const std::vector<ValidationIssue> issues =
    ValidateGraphDependencies(document, assets);
```

This checks direct call-site snapshots and recursively confirms that referenced registered dependencies exist.

## Change Subscriptions

Subscriptions are move-only RAII handles:

```cpp
auto subscription = assets.Subscribe(
    [](const GraphAssetChange& change) {
        QueueAssetRefresh(
            change.asset,
            change.after ? change.after->Generation() : 0,
            change.changes);
    });

// Later, or automatically in the handle destructor:
subscription.Reset();
```

After a successful content write, callbacks receive `Inserted` or `Replaced`, followed by `DependencyChanged` for transitive dependents. Removal emits `Removed`. Events own `before`/`after` record snapshots; dependency events also own the changed dependency snapshots. A URI-only replacement emits one `Replaced` event and does not invalidate dependents.

Callbacks run synchronously on the thread performing `Write()` or `Unregister()`, after the registry has committed. Their exceptions are swallowed so one subscriber cannot unwind a registry mutation. Mutating the same logical registry implementation from a change callback is rejected with `CommandFailed`; schedule a later main-thread action instead. A strong dispatch guard makes moving, move-assigning, or destroying the registry owner from a callback memory-safe. The callback snapshot for the current event batch is fixed before dispatch, so subscription changes affect later batches.

## Persistence Lifecycle

Load, then register:

```cpp
auto loaded = LoadGraphAssetJson(path, node_registry, &migrations, limits);
if (!loaded) return std::unexpected(loaded.error());

loaded->asset.source_uri = path;
auto stored = assets.Write(std::move(loaded->asset), GraphAssetWriteMode::Upsert);
```

JSON loading validates the asset's local document/presentation and runs migrations. Registry insertion is the step that validates dependency availability and the complete transitive closure. Save/load details are in [persistence](persistence.md#graph-assets).

## Editor Resolver

`EditorContext::EnterSubgraph()` handles only local graphs. When the user activates an external call site, `DrawEditor()` invokes `EditorCallbacks::resolve_graph_asset` with the asset ID and call-site interface snapshot:

```cpp
EditorCallbacks callbacks;
callbacks.resolve_graph_asset =
    [&assets](const GraphAssetId& id,
              const GraphInterface& expected) -> Result<ResolvedGraphAsset> {
        const auto record = assets.Find(id);
        if (!record) {
            return std::unexpected(Error{ErrorCode::AssetNotFound, "Asset is missing"});
        }
        const GraphAsset& asset = record->Asset();
        const GraphId root_id = asset.document.RootGraph();
        const Graph* root = asset.document.FindGraph(root_id);
        if (root == nullptr || root->interface != expected) {
            return std::unexpected(Error{
                ErrorCode::InvalidGraph, "Asset interface changed"});
        }
        return ResolvedGraphAsset{
            .asset = id,
            .generation = record->Generation(),
            .content_hash = record->ContentHash(),
            .root_graph = root_id,
        };
    };
```

The resolver identifies and validates a location; it does not transfer the asset document into the current editor. A successful result appears as `EditorResult::open_graph_asset`, containing the expected interface, generation, hash, and root graph. The application owns tabs/windows, editable copies, save/replace behavior, and stale-generation handling. The resolver result must use the requested asset ID, a non-zero generation, and a valid root ID.

Keep resolver callbacks pure with respect to the active document/editor and follow the [main-thread callback contract](threading_and_callbacks.md).
