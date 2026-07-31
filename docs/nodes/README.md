# UniGUI Nodes

UniGUI Nodes is a C++23 framework for building retained, persistent node editors on Dear ImGui. It separates a semantic `GraphDocument` from `GraphPresentation`, applies changes through atomic commands, and supplies editor interaction, custom UI, validation, persistence, hierarchy, graph assets, and routing.

Nodes has no evaluation runtime. It does not execute links, propagate values, schedule nodes, own runtime values, or define an execution order. An application runtime can read stable IDs and semantic snapshots, but synchronization with that runtime is application-owned.

## Include And Link

Use the umbrella header for the complete API:

```cpp
#include <uni/gui/nodes/nodes.h>
```

Link the regular UniGUI package target:

```cmake
find_package(UniGUI 1.0 CONFIG REQUIRED)
target_link_libraries(my_node_editor PRIVATE UniGUI::UniGUI)
```

Individual headers under `uni/gui/nodes/` are also public. Nodes persistence does not add a nlohmann/json dependency to package consumers.

## Documentation Map

- [Getting started](getting_started.md): registries, a first document, commands, and `DrawEditor()`.
- [Semantic model](semantic_model.md): IDs, graphs, properties, presentation, revisions, and copy-on-write.
- [Custom nodes](custom_nodes.md): semantic descriptors, node bodies, inspectors, pin layouts/styles, validation, and converters.
- [Commands and transactions](commands_and_transactions.md): atomic mutation, custom commands, history, conflicts, and protection.
- [Policies](policies.md): operation intents, allow/deny/replace/defer, preview, undo, and redo behavior.
- [Persistence](persistence.md): canonical JSON, migrations, limits, warnings, fragments, and atomic files.
- [Hierarchy](hierarchy.md): owned and referenced graphs, interfaces, boundary nodes, intergraph links, and navigation.
- [Graph assets](graph_assets.md): registry integrity, generations, hashes, subscriptions, and editor resolution.
- [Routing](routing.md): built-in and custom routers, path contracts, route points, and geometry cache invalidation.
- [Performance](performance.md): COW and editor metrics, cache behavior, benchmark suites, and regression gates.
- [Threading and callbacks](threading_and_callbacks.md): main-thread ownership, callback mutation rules, exception containment, and reentrancy.

The library-wide binary compatibility rules are in the [ABI policy](../ABI_POLICY.md). The project-level build and package instructions are in the [main README](../../README.md).

## Core Boundaries

- `GraphDocument` owns semantic graphs, nodes, pins, links, hierarchy, and intergraph links.
- `GraphPresentation` owns positions, sizes, colors, groups, route points, and other editor-only state.
- `CommandStack` is the supported mutation, mutation-level policy, deferred authorization, and history boundary.
- `RegistryCatalog` is the single owner of node descriptors and conversions; `NodeUiRegistry` and `LinkRouterRegistry` remain independent registries keyed by stable `TypeId` strings. A workspace exposes one const catalog view, mutation wrappers, and persistent `RegistryUpdate` batches.
- `EditorContext` owns transient editor state and geometry caches; `DrawEditor()` renders one editor in an active ImGui frame.
- `GraphAssetRegistry` publishes immutable owning graph-asset records and validates affected dependency closures incrementally.
- `GraphDocumentSnapshot` is a const semantic authoring snapshot that may be handed to workers after UI-thread capture.
- `RegistrySnapshot` is one owning immutable combined generation; callback invocation leases remain private.

Mutable owners remain main-thread-only. `UiDispatcher` is the mutation handoff, while const reads of an already captured `GraphDocumentSnapshot` are worker-safe.
