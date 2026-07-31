# Hierarchy

A document can contain local reusable graphs, uniquely owned child graphs, external graph-asset references, graph interfaces, and explicit intergraph proxy links. Hierarchy remains semantic authoring data; it does not imply runtime call semantics.

## Graph Interfaces

`GraphInterface` is a versioned ordered list of semantic pins. Each `GraphInterfacePin` declares a stable key, label, type, direction, kind, caller cardinality, and boundary cardinality.

Apply interfaces through `SetGraphInterfaceCommand`:

```cpp
using namespace Uni::GUI::Nodes;

GraphInterface interface{
    .version = 1,
    .pins = {
        GraphInterfacePin{
            .key = "input",
            .label = "Input",
            .type = TypeId{"number"},
            .direction = PinDirection::Input,
        },
        GraphInterfacePin{
            .key = "output",
            .label = "Output",
            .type = TypeId{"number"},
            .direction = PinDirection::Output,
            .caller_cardinality = PinCardinality::Multiple,
            .boundary_cardinality = PinCardinality::Single,
        },
    },
};

auto changed = commands.Execute(
    std::make_unique<SetGraphInterfaceCommand>(child, interface),
    document, presentation, registry);
```

The command ensures exactly one `BoundaryInput` and one `BoundaryOutput` node, including when the interface pin list is empty, and synchronizes dynamic projections:

- A call-site `Subgraph` node receives all interface pins with the interface direction and caller cardinality.
- `BoundaryInput` receives interface inputs as output pins with boundary cardinality.
- `BoundaryOutput` receives interface outputs as input pins with boundary cardinality.

Projection pins are matched by semantic key. Their order follows the interface and cannot be manually reordered. Changing or removing connected structural projections is rejected, as is synchronization through a read-only call site. Interface updates touch every local call site atomically and undo as one command.

## Owned Local Graphs

An owned graph has `GraphLifetime::Owned` and exactly one `SubgraphOwnership::Owned` local owner. It cannot be the document root or be targeted by a referenced call site.

Create the graph, bind its owner, and establish its interface in one compound command:

```cpp
const GraphId child = document.AllocateGraphId();
std::vector<std::unique_ptr<Command>> create;
create.push_back(std::make_unique<AddGraphCommand>(Graph{
    .id = child,
    .display_name = "Filter body",
    .lifetime = GraphLifetime::Owned,
}));
create.push_back(std::make_unique<SetNodeSubgraphCommand>(
    parent,
    owner_node,
    SubgraphReference{
        .ownership = SubgraphOwnership::Owned,
        .target = DocumentGraphTarget{child},
    }));
create.push_back(std::make_unique<SetGraphInterfaceCommand>(child, interface));

auto result = commands.Execute(
    std::make_unique<CompoundCommand>("Create filter subgraph", std::move(create)),
    document, presentation, registry);
```

Deleting the owner recursively deletes its owned graph closure and associated presentation/intergraph data. Undo restores the closure. Capturing an owned call-site includes the closure; paste deep-copies it and remaps every identity rather than aliasing the original.

An existing owned binding cannot simply be retargeted or cleared. Remove it with its owner, or use an application operation that explicitly promotes/restructures the graph while preserving a valid transaction.

## Referenced Local Graphs

A reusable graph has `GraphLifetime::Reusable`, has no owner, and may have multiple referenced call sites:

```cpp
const SubgraphReference reference{
    .ownership = SubgraphOwnership::Referenced,
    .target = DocumentGraphTarget{library_graph},
};

auto bound = commands.Execute(
    std::make_unique<SetNodeSubgraphCommand>(parent, call_node, reference),
    document, presentation, registry);
```

Deleting a referenced call site does not delete its target. Removing a reusable graph explicitly cascades into any graphs it owns, but the resulting document must not retain references to the removed graph.

Owned and referenced local edges must not create cycles. `ValidateStructure()` also requires every owned graph to have exactly one owner and every reusable graph to have none.

## External Graph Assets

External targets store a stable asset ID plus a snapshot of the expected interface:

```cpp
SubgraphReference external{
    .ownership = SubgraphOwnership::Referenced,
    .target = GraphAssetTarget{
        .asset = GraphAssetId{"filters.low_pass"},
        .interface = expected_interface,
    },
};
```

External assets can only be referenced, never owned directly. The interface snapshot keeps the document self-describing and allows call-site pins to persist even when the asset is unavailable. `ValidateGraphDependencies()` reports missing assets and stale interface snapshots. Registry lifecycle and resolver integration are in [graph assets](graph_assets.md).

## Intergraph Links

`IntergraphLink` connects explicit proxy endpoints in two different graphs:

- The source node role is `IntergraphOutput`, and its endpoint pin direction is `Input`.
- The destination node role is `IntergraphInput`, and its endpoint pin direction is `Output`.
- Endpoint type and `PinKind` must match exactly.
- Each source and destination endpoint participates in at most one intergraph link.
- Intergraph dependencies must remain acyclic.

```cpp
const IntergraphLinkId id = document.AllocateIntergraphLinkId();
auto connected = commands.Execute(
    std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
        .id = id,
        .source = {producer_graph, output_proxy, proxy_input_pin},
        .destination = {consumer_graph, input_proxy, proxy_output_pin},
    }),
    document, presentation, registry);
```

Intergraph links do not use `RegistryCatalog` conversion recipes. Insert explicit conversion semantics in a graph if endpoint types differ. Connect/disconnect are first-class commands and pass through the generic [policy](policies.md) intent.

## Local Navigation

`EditorContext` stores active graph, breadcrumbs, and a per-graph pan/zoom/selection view:

```cpp
EditorContext editor;
auto initialized = editor.ResetNavigation(document);
auto entered = editor.EnterSubgraph(document, owner_node);
const std::vector<Breadcrumb> path = editor.Breadcrumbs();
const bool returned = editor.NavigateBack();
```

`EnterSubgraph()` only follows `DocumentGraphTarget`. Entering saves the current view and restores an earlier child view when available. Back and breadcrumb navigation restore parent view state. `DrawEditor()` normalizes a path if document edits remove one of its graphs or call-site edges.

An external `GraphAssetTarget` must be opened as a separate document through `EditorCallbacks::resolve_graph_asset`; it is never entered inside the current document's breadcrumb stack.
