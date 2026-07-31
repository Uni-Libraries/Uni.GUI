# Getting Started

This guide creates a semantic document, adds two registered node types through undoable commands, and renders the editor. Read [the semantic model](semantic_model.md) before adding an application runtime; Nodes itself does not evaluate the graph.

## State To Own

A typical editor workspace keeps these objects together on the UI thread. The public retained facade already owns this exact set:

```cpp
#include <uni/gui/nodes/nodes.h>

using NodeWorkspace = Uni::GUI::Nodes::NodeEditorWorkspace;
```

`GraphDocument` starts with one reusable root graph. The private combined `RegistryCatalog` starts empty, and the router registry starts with the three built-in link routers. Use workspace wrappers to mutate node descriptors/conversions and `Registry()` for const catalog access.

## Register Semantic Types

Type strings and pin keys are persisted contracts. Choose names owned by the application or plugin rather than display labels.

```cpp
using namespace Uni::GUI::Nodes;

Result<void> RegisterTypes(NodeWorkspace& workspace) {
    auto source = workspace.RegisterNodeType(NodeTypeDescriptor{
        .type = TypeId{"example.number"},
        .display_name = "Number",
        .category = "Values",
        .static_pins = {
            PinDescriptor{
                .key = "value",
                .label = "Value",
                .type = TypeId{"number"},
                .direction = PinDirection::Output,
                .cardinality = PinCardinality::Multiple,
            },
        },
        .default_properties = {{"value", 0.0}},
        .property_impacts = {{"value", PropertyImpact::Rendering}},
    });
    if (!source) return source;

    return workspace.RegisterNodeType(NodeTypeDescriptor{
        .type = TypeId{"example.print"},
        .display_name = "Print",
        .category = "Output",
        .static_pins = {
            PinDescriptor{
                .key = "value",
                .label = "Value",
                .type = TypeId{"number"},
                .direction = PinDirection::Input,
            },
        },
    });
}
```

Registration validates non-empty stable IDs, descriptor versions, unique semantic pin keys, finite defaults, and pin metadata. Type conversions are in-memory application configuration rather than persisted graph data, so register them when constructing each workspace. See [custom nodes](custom_nodes.md) for UI and converters.

## Add Nodes Through Commands

`NodeEditorWorkspace::InstantiateNode()` reserves IDs and builds static pins through the private registry, but does not insert anything or advance the model revision. Execute an `AddNodeCommand` to commit the creation.

```cpp
Result<void> Populate(NodeWorkspace& workspace) {
    using namespace Uni::GUI::Nodes;

    const GraphId graph = workspace.document.RootGraph();
    auto number = workspace.InstantiateNode(TypeId{"example.number"});
    if (!number) return std::unexpected(number.error());

    auto print = workspace.InstantiateNode(TypeId{"example.print"});
    if (!print) return std::unexpected(print.error());

    auto added_number = workspace.Execute(
        std::make_unique<AddNodeCommand>(
            graph, std::move(*number), NodePresentation{.position = {40.0f, 80.0f}}),
        {});
    if (!added_number) return std::unexpected(added_number.error());

    auto added_print = workspace.Execute(
        std::make_unique<AddNodeCommand>(
            graph, std::move(*print), NodePresentation{.position = {360.0f, 80.0f}}),
        {});
    if (!added_print) return std::unexpected(added_print.error());
    return {};
}
```

`NodeEditorWorkspace` passes its catalog automatically and provides `RegisterNodeType`, `ReplaceNodeType`, guarded `UnregisterNodeType`, `InstantiateNode`, `RegisterConversion`, `UnregisterConversion`, `ReplaceConversion`, `BeginUpdate`, `Execute`, `Undo`, `Redo`, deferred `Resume`/`Cancel`/inspection, `CaptureSnapshot`, `Save`, `Load`, and `Draw`. For plugin reload, stage related descriptor/conversion changes through one `BeginUpdate()` when possible; old snapshots keep the old generation. `UnregisterNodeType` returns `TypeInUse` while conversion registrations reference that plugin type. `Registry()` is const-only, so mutation remains behind workspace guards. `Load` is atomic, rejects a pending or busy command stack, blocks live stack and catalog wrappers throughout migration callbacks, clears history/editor transient state on success, and retains the catalog.

## Render In An ImGui Frame

Call the workspace `Draw()` facade only while an ImGui window and frame are active, normally from `UiElement::Update()`:

```cpp
const auto result = workspace.Draw(Uni::GUI::Nodes::Vec2{0.0f, 0.0f});

if (result.model_changed) {
    QueueRuntimeRebuild();
}
```

A zero size uses the available ImGui region. `EditorResult` reports semantic, presentation, selection, active-graph, external-asset navigation, and transient animation state. Rendering missing presentation entries uses deterministic fallback geometry without persisting normalization changes. Continue frames while `animation_active` is true after `EditorContext::TriggerLinkFlow()`. Code that calls `DrawEditor()` directly passes one `RegistryCatalog` plus the independent node-UI and router registries.

## Validate And Save

Before consuming a graph in an application runtime, validate structure and registered node semantics:

```cpp
if (auto structure = workspace.document.ValidateStructure(); !structure) {
    Report(structure.error().message);
}

const auto issues = Uni::GUI::Nodes::ValidateGraph(
    workspace.document,
    workspace.document.RootGraph(),
    workspace.Registry());

auto saved = workspace.Save("project.graph.json");
auto loaded = workspace.Load("project.graph.json");
if (loaded) {
    for (const GraphIoWarning& warning : *loaded) Report(warning.message);
}
```

`ValidateGraph()` returns warnings and errors; it does not mutate the graph. If authorization is deferred, `Save()` serializes only the currently committed document/presentation; the staged operation is not included. `Load()` returns `OperationPending` until the operation is resumed or cancelled. Save and load behavior is covered in [persistence](persistence.md).

## Next Steps

- Add widgets and arbitrary pin placement with [custom nodes](custom_nodes.md).
- Connect pins and understand atomic history in [commands and transactions](commands_and_transactions.md).
- Control application-specific edits with [policies](policies.md).
- Add nested graphs with [hierarchy](hierarchy.md) and reusable documents with [graph assets](graph_assets.md).
- Follow the [threading and callback contracts](threading_and_callbacks.md) before integrating background work.
