# Persistence

Nodes provides deterministic JSON for complete documents, clipboard-style fragments, and reusable graph assets. The public API exposes strings and file paths; nlohmann/json remains a private implementation detail.

## Wire Format And Schema

`GraphJsonFormatVersion` is currently `4`. It versions the library-owned JSON envelope and field layout. Readers require the exact supported format version and exact object key sets; missing, duplicate, or unsupported structural fields return `InvalidFormat`, and a newer envelope returns `UnsupportedVersion`.

Version 4 remains flat: link presentation emits `router`, `route_points`, `color`, and `locked`, while groups emit `position`, `size`, `collapsed`, `z_order`, `title`, `body`, `color`, `kind`, `members`, and `locked`. Decode constructs `LinkStyle`/`PersistentRoutePointSequence` and `GroupGeometry`/`GroupStyleHandle`/`GroupMemberSet`/`GroupProtection` internally. Do not write nested C++ model names into JSON.

`GraphDocument::SchemaVersion()` is an application data-schema version stored inside that envelope. It starts at 1 and is advanced through `DocumentMigrationRegistry`. Do not use schema version to describe the Nodes wire layout.

Serialization is canonical and deterministic:

- Maps and IDs are emitted in stable order.
- Output is UTF-8 and newline-terminated.
- 64-bit IDs and integer-like values are encoded without JSON floating-point precision loss.
- Unknown property JSON is retained canonically through `OpaqueJsonProperty`.
- Unknown node types and router IDs retain their persisted strings.

The strict structural policy means arbitrary future graph/node/link fields are not retained. Forward-compatible plugin payloads belong in typed properties with an opaque property kind, not in new unrecognized envelope fields.

## Documents

```cpp
using namespace Uni::GUI::Nodes;

auto json = SerializeGraphDocumentJson(document, presentation);
if (!json) Report(json.error().message);

auto loaded = DeserializeGraphDocumentJson(*json, node_registry);
if (!loaded) {
    Report(loaded.error().message);
} else {
    document = std::move(loaded->document);
    presentation = std::move(loaded->presentation);
    for (const GraphIoWarning& warning : loaded->warnings) {
        Report(warning.message);
    }
}
```

A loaded document has fresh model and presentation revisions at zero. Imported ID generators observe all persisted IDs. Loading does not resolve `GraphAssetTarget` through a registry; unresolved external references and unknown plugins are preserved and reported in `warnings`. When replacing owners directly as above, clear any `CommandStack` bound to the old identities and reset transient `EditorContext` state. Prefer `NodeEditorWorkspace::Load()` when those owners belong to a workspace; it performs guarded atomic swaps and cleanup together.

After load, use `ValidateGraphDependencies()` with a `GraphAssetRegistry` and `ValidateGraph()` for each graph when the application requires all plugins and assets to be present.

`RegistryCatalog` node descriptors, conversions, tokens, and revisions, plus UI descriptors and router callbacks, are runtime-only registrations and are never serialized. A loaded link retains semantic endpoint types and its selected router ID, but conversion lookup exists only after the application registers the corresponding conversion again. Already inserted converter nodes and their two links are ordinary explicit graph entities and do round-trip; replacing a live recipe affects future insertions and does not migrate those nodes.

## Committed-Only Save

Serialization receives live `GraphDocument` and `GraphPresentation` owners and never inspects a command stack. A deferred transaction remains private staged state, so `SerializeGraphDocumentJson()`, `SaveGraphDocumentJson()`, and `NodeEditorWorkspace::Save()` serialize only the last committed state while authorization is pending. Deferred requests, staged mutations, history, and registry snapshots are not persisted.

`NodeEditorWorkspace::Load()` replaces live owners and is therefore blocked with `OperationPending` while a deferred transaction exists; it is also blocked while the command stack is busy. Resume or cancel before loading. A failed or rejected load leaves the complete committed workspace unchanged.

## Document Migrations

Construct a registry for the application's current target schema and register every contiguous `N -> N+1` step:

```cpp
DocumentMigrationRegistry migrations{2};
auto registered = migrations.Register(
    1,
    [](DocumentMigrationContext& context) -> Result<void> {
        for (Graph& graph : context.archive.graphs) {
            std::vector<NodeId> ids;
            for (const auto& [id, node] : graph.nodes) {
                (void)node;
                ids.push_back(id);
            }
            for (const NodeId id : ids) {
                NodeInstance node = graph.nodes.at(id);
                node.properties["migrated"] = true;
                graph.nodes.insert_or_assign(id, std::move(node));
            }
        }
        return {};
    });

auto loaded = DeserializeGraphDocumentJson(
    bytes, node_registry, &migrations);
```

The context identifies `from_version`, `to_version`, and a temporary `GraphArchive`. A missing step returns `MigrationMissing`; callback failure or exception returns `MigrationFailed`. The migration map is pinned for the complete chain, and reentrant registration on that logical registry returns `CommandFailed`. A schema newer than the target is not downgraded. Without a migration registry, only schema 1 is accepted.

Structural JSON decoding happens before application migrations. The migrated archive is structurally validated again during import, and all configured IO limits are re-applied after document and node migrations.

## Node Migrations

`NodeTypeDescriptor::version` versions one node type. When a loaded node is older, the same descriptor migration callback runs once per version step until the descriptor version is reached:

```cpp
descriptor.version = 2;
descriptor.migrate = [](NodeMigrationContext& context) -> Result<void> {
    if (context.from_version != 1) return {};

    PinInstance& output = context.creation.pins.back();
    const PinId old_id = output.id;
    const PinId new_id = context.allocate_pin_id();
    if (!new_id) {
        return std::unexpected(Error{
            ErrorCode::MigrationFailed, "Pin ID space is exhausted"});
    }
    output.id = new_id;
    for (PinId& id : context.creation.node.pins) {
        if (id == old_id) id = new_id;
    }
    context.remap_links(old_id, new_id);
    return {};
};
```

The callback can edit properties and instance/descriptor-owned pin data, allocate IDs, call `remap_links(old, replacement)`, or call `remove_links(pin)`. It must keep `NodeCreation::node.pins` and `creation.pins` consistent. After the final step, descriptor-owned pins must exactly match `ResolvePinSchema()` for the migrated properties. A required callback that is absent returns `MigrationMissing`.

An unregistered node type loads unchanged with a warning. A node version newer than its installed descriptor is also preserved with a warning and is not downgraded or resolved by the older descriptor. Any change to configurable schema dependencies, resolver behavior, or semantic-key meaning requires a descriptor version increment and migration. Deserialization pins one immutable descriptor generation before document migrations; node callbacks cannot self-register/unregister, and active descriptor/function ownership survives external registry move or destruction.

## Limits

`GraphIoLimits` applies to both serialization and deserialization:

| Field | Default |
|---|---:|
| `max_bytes` | 64 MiB |
| `max_depth` | 128 |
| `max_string_bytes` | 16 MiB per decoded string |
| `max_json_values` | 2,000,000 |
| `max_graphs` | 100,000 |
| `max_nodes` | 100,000 |
| `max_pins` | 400,000 |
| `max_links` | 400,000 |
| `max_groups` | 100,000 |
| `max_route_points` | 400,000 |
| `max_properties` | 400,000 |

Limits are security and resource controls, not only performance hints. Byte limits are checked before parsing a file, depth/value/string limits are enforced while parsing, entity limits are counted while decoding, and the final migrated result is serialized/validated against the same limits. Exceeding one returns `SizeLimitExceeded`.

Use lower application-specific limits when opening untrusted content. Raising `max_depth` also permits deeper `OpaqueJsonProperty` values.

## Atomic Files

The file helpers serialize first, write a unique sibling temporary file, synchronize it, and atomically replace the destination:

```cpp
auto saved = SaveGraphDocumentJson(
    "graphs/main.json", document, presentation, limits);
auto loaded = LoadGraphDocumentJson(
    "graphs/main.json", registry, &migrations, limits);
```

Paths must be non-empty and contain no embedded NUL. On POSIX, an existing destination is opened without following symlinks where supported and must be a regular file; permissions and extended attributes are preserved, the temporary file is `fsync()`ed, `rename()` replaces the destination, and the containing directory is synchronized. On Windows, existing ACLs and attributes are preserved, `FlushFileBuffers()` is used, and replacement uses `MoveFileExW` with write-through.

If writing or replacement fails, the helper removes its temporary file where possible and leaves a structured `IoWrite` error. Atomic replacement protects readers from partial file contents; it does not coordinate multiple graph files as one transaction.

## Fragments

`CaptureGraphFragment()` captures selected nodes, internal links, groups, presentation, and recursively owned graph closures. `SerializeGraphFragmentJson()` produces a portable fragment. Loading applies node migrations but has no document-schema migration parameter.

Before paste, allocate and validate every new identity with `PrepareGraphFragmentPaste()`, then execute one command:

```cpp
auto fragment = DeserializeGraphFragmentJson(text, registry, limits);
auto prepared = PrepareGraphFragmentPaste(
    document, presentation, registry, *fragment, graph, Vec2{400.0f, 200.0f});
if (prepared) {
    auto pasted = commands.Execute(
        std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared)),
        document, presentation, registry);
}
```

Preparation remaps graph, node, pin, link, group, route-point, and intergraph-link IDs and captures descriptor schema provenance for registered node types. `PasteGraphFragmentCommand::Apply()` records those descriptor dependencies and rejects removed or schema-incompatible replacements while preserving intentional dynamic pins. Paste is atomic and undoable. Owned hierarchy behavior is covered in [hierarchy](hierarchy.md).

## Graph Assets

`SerializeGraphAssetJson()` and the corresponding file helpers use a `kind: "graph_asset"` envelope and include the asset ID, document, and presentation. `source_uri` is registry/application metadata and is not part of serialized graph-asset content or its SHA-256 content hash.

Deserialization validates the contained semantic and presentation state but does not make unresolved dependencies valid. Insert the loaded asset into `GraphAssetRegistry` to validate its complete dependency closure. See [graph assets](graph_assets.md).
