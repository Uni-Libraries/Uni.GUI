# Custom Nodes

Custom nodes are assembled from independent semantic, UI, type-conversion, and validation registrations. A semantic node can exist and round-trip without a UI descriptor, and a document can load unknown node types without registering their plugin.

## Semantic Descriptor

Register one `NodeTypeDescriptor` for each stable node type:

```cpp
#include <uni/gui/nodes/nodes.h>

using namespace Uni::GUI::Nodes;

Result<void> RegisterGain(RegistryCatalog& registry) {
    return registry.RegisterNodeType(NodeTypeDescriptor{
        .type = TypeId{"audio.gain"},
        .display_name = "Gain",
        .category = "Audio",
        .version = 2,
        .pin_schema = {
            PinDescriptor{
                .key = "input",
                .label = "Input",
                .type = TypeId{"audio.buffer"},
                .direction = PinDirection::Input,
            },
            PinDescriptor{
                .key = "output",
                .label = "Output",
                .type = TypeId{"audio.buffer"},
                .direction = PinDirection::Output,
                .cardinality = PinCardinality::Multiple,
            },
        },
        .default_properties = {{"gain_db", 0.0}},
        .property_impacts = {{"gain_db", PropertyImpact::RuntimeOnly}},
        .behavior = std::make_shared<const NodeBehavior>(NodeBehavior{
            .validate = [](const NodeInstance& node, std::span<const PinInstance>) {
                const auto found = node.properties.find("gain_db");
                if (found == node.properties.end() ||
                    !std::holds_alternative<double>(found->second)) {
                    return std::vector<std::string>{"gain_db must be a number"};
                }
                return std::vector<std::string>{};
            },
        }),
    });
}
```

The registry rejects empty types and names, version zero, duplicate type IDs, duplicate or empty pin keys, invalid pin metadata, non-finite property defaults, and invalid impact declarations. An empty pin label is normalized to its semantic key.

`NodePinSchema` is the single source of descriptor-owned pin metadata. A braced list creates a fixed schema. `Instantiate()` copies defaults, applies `NodeInstantiationOptions::property_overrides`, resolves the schema, allocates node and pin IDs, marks descriptor-owned pins `PinStorage::Static`, and sets `type_version` to the descriptor version. `PinStorage::Static` describes ownership, not lifetime: a configurable descriptor may reconcile those pins when a declared dependency changes. Instance-owned dynamic pins retain `PinStorage::Dynamic` and must have unique semantic keys.

### Configurable Pin Schemas

Use a configurable `NodePinSchema` when properties determine pin count, order, labels, types, directions, kinds, or cardinalities:

```cpp
descriptor.pin_schema = NodePinSchema{
    {"input_count"},
    [](const PropertyBag& properties) -> Result<std::vector<PinDescriptor>> {
        const auto found = properties.find("input_count");
        const auto* count = found == properties.end()
            ? nullptr
            : std::get_if<std::int64_t>(&found->second);
        if (count == nullptr || *count < 0 || *count > 64) {
            return std::unexpected(Error{
                ErrorCode::InvalidArgument,
                "input_count must be between 0 and 64",
            });
        }

        std::vector<PinDescriptor> pins;
        pins.reserve(static_cast<std::size_t>(*count) + 1);
        for (std::int64_t index = 0; index < *count; ++index) {
            const std::string key = "input." + std::to_string(index);
            pins.push_back(PinDescriptor{
                .key = key,
                .label = "Input " + std::to_string(index + 1),
                .type = TypeId{"number"},
            });
        }
        pins.push_back(PinDescriptor{
            .key = "result",
            .label = "Result",
            .type = TypeId{"number"},
            .direction = PinDirection::Output,
            .cardinality = PinCardinality::Multiple,
        });
        return pins;
    },
};
descriptor.default_properties = {{"input_count", std::int64_t{2}}};
descriptor.property_impacts = {{"input_count", PropertyImpact::Geometry}};
```

Dependencies are explicit and independent of `PropertyImpact`: every property read by the resolver must be listed, regardless of its revision impact. A dependent edit resolves the complete schema once and reconciles descriptor-owned pins by semantic key. Retained keys retain `PinId`; removed keys do not leave dormant connections; newly reintroduced keys receive new IDs. Instance-owned pin order is preserved around the descriptor subsequence.

The resolver must be pure, deterministic for an equal `PropertyBag`, and depend only on its argument and immutable captured configuration. Registration resolves and validates the default schema once. `DefaultPinSchema()` returns that materialized schema without invoking application code, which keeps converter construction and the editor palette out of the callback path. `ResolvePinSchema()` explicitly resolves non-default properties and validates unique non-empty keys and pin metadata.

Use `InvalidConnectionPolicy::Reject` on `SetNodePropertyCommand` to reject a schema change that would invalidate links. The default `Disconnect` policy removes only invalid local or intergraph links and their presentation, and undo restores exact IDs and routes. Direct custom-command calls to `GraphTransaction::SetNodeProperty()` preserve the same schema invariant but reject changes that require implicit disconnection; remove and capture those links explicitly when custom undo semantics require it.

`NodeBehavior::validate` adds application messages to `ValidateGraph()`. `NodeBehaviorPtr` is an explicit immutable behavior identity: reusing the handle makes behavior equality honest, while a different handle is a descriptor change. Validation does not replace structural or connection validation, and it does not mutate the node.

## Property Impact

Declare the least expensive correct `PropertyImpact`:

- `RuntimeOnly`: runtime data changed; cached editor geometry remains valid.
- `Rendering`: node drawing may change, but size and pin placement remain valid.
- `Geometry`: body size or pin layout can change.
- `Topology`: application semantics beyond pin-schema dependencies can change connectivity.

Both `RuntimeOnly` and `Rendering` advance the value revision only. A resolved pin-schema delta independently adds layout and topology revisions, so a schema dependency does not need to be mislabeled `Topology`. Undeclared properties default to `Geometry`; set `undeclared_property_impact` explicitly if a plugin has a different conservative default. See [semantic revisions](semantic_model.md#revisions-and-identity) and [performance](performance.md).

## Node Body And Inspector

Register UI independently by the same `TypeId`. Node body and inspector callbacks may issue ImGui items and queue commands through `NodeUiContext`:

```cpp
#include <imgui.h>
#include <uni/gui/nodes/nodes.h>

using namespace Uni::GUI::Nodes;

Result<void> RegisterGainUi(NodeUiRegistry& ui) {
    if (auto glyph = ui.RegisterHeaderGlyph(NodeHeaderGlyphDescriptor{
            .id = "audio.meter",
            .aspect_ratio = 1.0f,
            .draw = [](const NodeHeaderGlyphDrawContext& context) {
                const ImVec2 min{context.min.x, context.min.y};
                const ImVec2 max{context.max.x, context.max.y};
                const ImVec2 center{(min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f};
                context.draw_list.AddCircleFilled(
                    center,
                    (max.y - min.y) * 0.35f,
                    context.color);
            },
        }); !glyph) {
        return glyph;
    }
    return ui.Register(NodeUiDescriptor{
        .type = TypeId{"audio.gain"},
        .draw_body = [](NodeUiContext& context) {
            double value = 0.0;
            if (const PropertyValue* property = context.FindProperty("gain_db")) {
                if (const auto* stored = std::get_if<double>(property)) value = *stored;
            }

            float edited = static_cast<float>(value);
            const bool changed = ImGui::SliderFloat("Gain", &edited, -60.0f, 12.0f);
            context.EditProperty("gain_db", PropertyValue{static_cast<double>(edited)}, changed);
        },
        .draw_inspector = [](NodeUiContext& context) {
            ImGui::Text("Node type: %s", context.Node().type.Value().c_str());
        },
        .default_size = {240.0f, 110.0f},
        .header_color = 0xFF76512EU,
        .resolve_header = [](const NodeHeaderContext&) {
            return NodeHeaderPresentation{
                .lines = {"Gain", "channel 1"},
                .items = {
                    NodeHeaderItem{
                        .id = "meter",
                        .content = NodeHeaderGlyph{"audio.meter"},
                        .active = true,
                        .action = "open-meter",
                        .tooltip = "Open meter",
                    },
                    NodeHeaderItem{
                        .id = "domain",
                        .content = NodeHeaderBadge{"AUDIO"},
                    },
                },
            };
        },
    });
}
```

Call `EditProperty()` immediately after the ImGui editing item. It uses item activation/deactivation to merge a continuous gesture into one undo record. `SetProperty()` queues an ordinary property command. `Submit()` accepts any custom `Command`.

`resolve_header` returns owned text plus generic trailing glyphs or badges. With no explicit text, the editor uses the instance display name, then the semantic type display name, then the `TypeId`. Set `EditorConfig::node_header.maximum_text_lines` to `2` to reserve a measured two-line header for every node. Header height is derived from normalized font metrics, padding, item height, and `minimum_height`; transient subtitle presence never moves pins or routing geometry.

Glyph identifiers are application-defined and registered once in `NodeUiRegistry`. Header item IDs must be non-empty and unique within one node. An item with a non-empty `action` is interactive; activation is returned in `EditorResult::header_actions` with the graph, node, item ID, and action string. Rendering callbacks do not mutate the graph. Badges are informational text items. Colors, active/disabled state, tooltips, ordering, clipping, and narrow-node overflow are handled by the same item model, without runtime or playback semantics in the core library.

The resolver is rendering-only, runs for each visible node, returns owning strings, and must not mutate editor state or the UI registry. Set `EditorConfig::enable_node_collapse` to `false` when nodes in an editor must always remain expanded; the collapse control and context-menu action are then omitted and persisted collapsed state is ignored while rendering.

Set `EditorCallbacks::duplicate_selection` to delegate the built-in `Duplicate` action to an application-owned model. The callback receives the stable editor selection and replaces the generic graph-fragment paste path. It must only enqueue application work; process that work after `DrawEditor()` returns before mutating the document or rebuilding the editor.

`NodeUiContext` also exposes the current graph, node, ordered pins, `UiScale()`, editor `Zoom()`, combined `ScreenScale()`, logical available size, read-only state, and logical-to-screen conversion. Persisted node geometry stays in graph units; `ToScreen()` composes UI scale and zoom exactly once. Its dynamic pin methods queue add/remove/update/reorder commands and maintain a callback-local shadow so later operations in the same callback see earlier queued pin changes.

Callbacks must balance every ImGui stack they modify. They must not execute a command stack, reserve IDs directly, or mutate the document, presentation, or UI registry. Revision/identity-changing direct mutations are rejected; unsupported side effects must not be used even if a particular one is not observable by the callback guard. Queued commands run after callback rendering, as one `CompoundCommand` when more than one is submitted. Read-only contexts ignore submissions and dynamic-pin changes. Full callback rules are in [threading and callbacks](threading_and_callbacks.md).

### Editor Units And Scaling

Node positions, node sizes, routes, pin placement, header layout, node rounding, pin radius, and resize-handle size use persistent graph units. The editor transforms them with `ui_scale * editor_zoom`. Font measurements are normalized back to graph units before they affect label gutters or routing bounds, so moving a window between displays does not change graph geometry.

Pan, link width and hit radius, route-point radius, overlays, minimap size, and explicit editor control sizes use reference UI units. They follow UI scale but remain stable when editor zoom changes. `EditorContext::Pan()` therefore remains display-independent. Dear ImGui framebuffer scaling is renderer-owned and must not be applied to graph or UI geometry again.

## Arbitrary Pin Layout

Without a custom layout, inputs are placed on the left, outputs on the right, and the body is placed between label gutters. `NodeUiDescriptor::layout` can replace this geometry in node-local logical coordinates:

```cpp
descriptor.layout = [](const NodeUiLayoutContext& context) -> Result<NodeUiLayout> {
    NodeUiLayout result;
    result.body = GraphRect{
        {8.0f, context.header_height + 8.0f},
        {context.node_size.x - 8.0f, context.node_size.y - 8.0f},
    };
    for (const PinId id : context.node.pins) {
        const PinInstance& pin = context.graph.pins.at(id);
        const bool output = pin.direction == PinDirection::Output;
        result.pins.push_back(PinPlacement{
            .pin = id,
            .position = {context.node_size.x * 0.5f,
                         output ? 0.0f : context.node_size.y},
            .outward_normal = {0.0f, output ? -1.0f : 1.0f},
            .label = PinLabelPlacement{
                .offset = {0.0f, output ? 9.0f : -9.0f},
                .pivot = {0.5f, output ? 0.0f : 1.0f},
            },
        });
    }
    return result;
};
```

The result must contain every node pin exactly once. Positions, body bounds, label offsets/pivots, and normals must be finite and bounded; normals must be non-zero and are normalized by the editor. A body rectangle is optional. Layout callbacks are pure and cached, so call `EditorContext::InvalidateGeometry()` if external captured state changes without a registry or model revision.

## Pin Styles

Pin styles are registered by pin value type, not node type:

```cpp
auto registered = ui.RegisterPinStyle(
    TypeId{"audio.buffer"},
    [](const PinStyleContext& context) {
        return PinStyle{
            .color = context.connected ? 0xFF65C987U : 0xFFB9B9B9U,
            .radius = context.hovered ? 7.0f : 5.0f,
            .shape = PinShape::Diamond,
        };
    });
```

Missing optionals retain the editor style. Radius is clamped to the supported range. Style callbacks are pure; changing only a style registration advances `NodeUiRegistry::Revision()` but not `LayoutRevision()`, so geometry remains cached.

## Dynamic Pins

Use context methods from UI, or the corresponding command classes elsewhere:

```cpp
const PinId pin = context.AddDynamicPin(PinDescriptor{
    .key = "sidechain",
    .label = "Sidechain",
    .type = TypeId{"audio.buffer"},
    .direction = PinDirection::Input,
});
```

The ID is returned immediately when reservation succeeds, while insertion remains deferred. Only dynamic pins can be removed or updated. Their ID, owner, semantic key, and `PinStorage::Dynamic` identity are immutable during update.

## Converters

`RegistryCatalog` owns both node descriptors and conversions. It allows equal types and the wildcard type `"*"`; a registered conversion describes a concrete node with one semantic input key and one semantic output key:

```cpp
auto converted = registry.RegisterConversion(ConversionDescriptor{
    .key = ConversionKey{
        .source_type = TypeId{"int"},
        .destination_type = TypeId{"float"},
        .kind = PinKind::Data,
    },
    .node_type = TypeId{"convert.int_to_float"},
    .input_pin = "input",
    .output_pin = "output",
});
```

The conversion node must already be registered. The named pins in its materialized default schema must have input/output directions and must exactly match all three fields of `ConversionKey`. Data and execution conversions for the same type pair are independent. Only one live conversion may exist for one exact key.

Applications with type families can install one compatibility policy:

```cpp
registry.SetTypeCompatibility(
    [](const TypeId& output, const TypeId& input, PinKind kind) {
        return kind == PinKind::Data &&
            output == TypeId{"float"} && input == TypeId{"number"};
    });
```

Exact and wildcard compatibility remains built in. The policy may also match the source and destination sides of registered converters; an exact conversion key takes precedence, while several generalized matches are rejected as ambiguous. Policy exceptions fail closed. `SetTypeCompatibility()` and `ClearTypeCompatibility()` are available on `RegistryCatalog`, `RegistryUpdate`, and `NodeEditorWorkspace`; staging the policy in `RegistryUpdate` publishes it atomically with descriptor and conversion changes. Existing snapshots keep the policy generation they captured.

Every semantic conversion change publishes one immutable catalog generation and increments `RegistryCatalog::ConversionRevision()`; a failed or identical registration replacement leaves revisions and generation unchanged. Application code may capture one passive, copyable owning snapshot without dependency-recording side effects:

```cpp
const RegistrySnapshot generation = registry.Snapshot();
const std::uint64_t revision = generation.ConversionRevision();
const ConnectionResult compatibility =
    generation.Check(TypeId{"int"}, TypeId{"float"}, PinKind::Data);
```

When `Check()` returns `RequiresConversion`, `ConnectionResult::recipe` is an exact owning `ConversionRecipe` handle. The handle can instantiate its captured node schema directly with `recipe->Instantiate(document)`; there is no descriptor-based lookup or kind fallback. Snapshots and recipe handles remain valid after live unregister/replace, registry destruction, or removal of the original node descriptor. Applications use public `Snapshot()`; callback-scoped invocation leases are private implementation details.

`RegistryCatalog::Check()` and `RegistrySnapshot::Check()` return `Allowed`, `RequiresConversion`, or `Rejected`. `PrepareConnectionCommand()` performs complete graph/cardinality/protection validation against one pinned generation, then returns either a direct connection/reconnect command or one atomic `InsertConversionCommand`. A recipe from another catalog fails with `RegistryMismatch`. Executing the command adds the converter and two links, and one undo removes the entire insertion.

`RegistrySnapshot::Instantiate()` embeds immutable descriptor provenance in the returned `NodeCreation`. `AddNodeCommand::Apply()` records the current descriptor dependency and verifies descriptor version plus the resolved ordered pin schema. Extra intentional `PinStorage::Dynamic` pins are allowed. A descriptor removal or replacement between preparation and apply fails closed; schema-aware undo/redo also requires the exact descriptor identity captured on first apply. A related replacement while authorization is deferred makes resume return `RevisionConflict`.

`RegisterConversion()` returns a `ConversionRegistrationToken`. For reload, call `RegistryCatalog::BeginUpdate()` (or `NodeEditorWorkspace::BeginUpdate()`) and stage node plus conversion changes before one `Commit()`. Descriptors, conversions, registration indexes, and reverse indexes are persistent AVL roots: snapshots are O(1), point updates path-copy O(log N), and dependent recipe fanout is O(D). Commit validates the final schema, preserves replacement tokens, and publishes at most one generation; identical replacements and net no-op batches retain baseline roots, revisions, and recipe identities. Old snapshots remain usable. `RegistrationsForNodeType()` and `HasConversionsForNodeType()` provide lifecycle queries. Node removal returns `TypeInUse` until referencing conversions are removed, unless recipes and node are removed in the same update.

Conversions and catalog revisions are runtime-only application configuration. They are not part of document, fragment, or graph-asset JSON and must be registered again when creating a process/workspace. No serialization migration can add a conversion registration.

## Node Migrations

When a loaded node has an older `type_version`, `NodeBehavior::migrate` is called one version step at a time. It may edit `NodeCreation`, allocate replacement pin IDs, remap link endpoints, or remove links. The complete process is documented in [persistence](persistence.md#node-migrations).
