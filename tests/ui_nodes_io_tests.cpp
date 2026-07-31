#include <uni/gui/nodes/nodes.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using namespace Uni::GUI::Nodes;

static_assert(!std::is_move_constructible_v<NodeEditorWorkspace> &&
              !std::is_move_assignable_v<NodeEditorWorkspace>);

std::string ClipboardText;

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

NodeTypeDescriptor SourceDescriptor(const std::uint32_t version = 1, MigrateNodeFn migrate = {}) {
    return NodeTypeDescriptor{
        .type = TypeId{"io.source"},
        .display_name = "Source",
        .category = "IO",
        .version = version,
        .static_pins =
            {
                PinDescriptor{
                    .key = "value",
                    .label = "Value",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Output,
                    .cardinality = PinCardinality::Multiple,
                },
            },
        .behavior = migrate
            ? std::make_shared<const NodeBehavior>(NodeBehavior{.migrate = std::move(migrate)})
            : nullptr,
    };
}

NodeTypeDescriptor SinkDescriptor() {
    return NodeTypeDescriptor{
        .type = TypeId{"io.sink"},
        .display_name = "Sink",
        .category = "IO",
        .static_pins =
            {
                PinDescriptor{
                    .key = "value",
                    .label = "Value",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Input,
                },
            },
    };
}

void RegisterBase(RegistryCatalog& registry) {
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value(), "Source descriptor must register");
    Expect(registry.RegisterNodeType(SinkDescriptor()).has_value(), "Sink descriptor must register");
}

void Execute(CommandStack& commands, std::unique_ptr<Command> command, GraphDocument& document,
             GraphPresentation& presentation, const RegistryCatalog& types, const char* message) {
    auto result = commands.Execute(std::move(command), document, presentation, types);
    if (!result) {
        std::cerr << message << ": " << result.error().message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Fixture final {
    explicit Fixture(RegistryCatalog& registry);

    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    RegistryCatalog& types;
    GraphId graph;
    NodeId source;
    NodeId sink;
    PinId output;
    PinId input;
    LinkId link;
    GroupId group;
};

Fixture::Fixture(RegistryCatalog& registry) : types(registry) {
    auto& fixture = *this;
    fixture.graph = fixture.document.RootGraph();
    auto source = registry.Instantiate(fixture.document, TypeId{"io.source"});
    auto sink = registry.Instantiate(fixture.document, TypeId{"io.sink"});
    Expect(source && sink, "Fixture nodes must instantiate");
    source->node.properties = {
        {"asset", AssetReference{std::numeric_limits<std::uint64_t>::max()}},
        {"bool", true},
        {"double", 0.125},
        {"int", std::numeric_limits<std::int64_t>::min()},
        {"opaque", OpaqueJsonProperty{
            "{\"kind\":\"future.vector\",\"value\":{"
            "\"decimal\":0.12345678901234567890123456789,\"huge\":1e400,"
            "\"precise\":9007199254740993,\"tiny\":1e-400,\"w\":4,\"x\":1}}"}},
        {"string", std::string{"UTF-8: \xD0\xB3\xD1\x80\xD0\xB0\xD1\x84"}},
        {"vec2", Vec2{-0.0f, 12.5f}},
    };
    fixture.source = source->node.id;
    fixture.sink = sink->node.id;
    fixture.output = source->pins.front().id;
    fixture.input = sink->pins.front().id;
    Execute(fixture.commands,
            std::make_unique<AddNodeCommand>(fixture.graph, std::move(*source),
                                             NodePresentation{
                                                 .position = {-25.5f, 10.0f},
                                                 .size = {220.0f, 120.0f},
                                                 .z_order = std::numeric_limits<std::uint64_t>::max(),
                                                 .color = 0xFF102030U,
                                             }),
            fixture.document, fixture.presentation, fixture.types, "Source must be added");
    Execute(fixture.commands,
            std::make_unique<AddNodeCommand>(fixture.graph, std::move(*sink),
                                             NodePresentation{.position = {300.0f, 20.0f}, .collapsed = true}),
            fixture.document, fixture.presentation, fixture.types, "Sink must be added");
    fixture.link = fixture.document.AllocateLinkId();
    Execute(fixture.commands,
            std::make_unique<ConnectPinsCommand>(
                fixture.graph, Link{.id = fixture.link, .output = fixture.output, .input = fixture.input}),
            fixture.document, fixture.presentation, fixture.types, "Fixture link must connect");
    const RoutePointId point = fixture.presentation.AllocateRoutePointId();
    Execute(
        fixture.commands,
        std::make_unique<SetLinkPresentationCommand>(fixture.link,
                                                     LinkPresentation{
                                                         LinkStyle{
                                                             .router = TypeId{"test.router"},
                                                             .color = 0xFFABCDEFU,
                                                         },
                                                         PersistentRoutePointSequence{{
                                                             .id = point,
                                                             .position = {150.0f, 55.0f},
                                                         }}}),
        fixture.document, fixture.presentation, fixture.types, "Link presentation must be set");
    fixture.group = fixture.presentation.AllocateGroupId();
    Execute(fixture.commands,
            std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = fixture.group,
                .graph = fixture.graph,
                .geometry = GroupGeometry{
                    .position = {-50.0f, -20.0f},
                    .size = {600.0f, 260.0f},
                    .z_order = 42,
                },
                .style = MakeGroupStyle(GroupStyle{
                    .title = "Persistent group",
                    .body = "Round-trip body",
                    .color = 0x80402010U,
                    .kind = GroupKind::Comment,
                }),
                .members = {fixture.source, fixture.sink},
            }),
            fixture.document, fixture.presentation, fixture.types, "Fixture group must be added");
}

Fixture MakeFixture(RegistryCatalog& registry) { return Fixture{registry}; }

std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
}

void TestDocumentRoundTripAndUnknownData() {
    RegistryCatalog registry;
    RegisterBase(registry);
    auto fixture = MakeFixture(registry);
    auto first = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
    auto second = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
    Expect(first && second && *first == *second && first->back() == '\n',
           "Document JSON must be deterministic and newline-terminated");
    Expect(first->find("\"asset\":{\"kind\":\"asset\",\"value\":"
                       "\"18446744073709551615\"}") != std::string::npos,
           "64-bit values must be encoded without JSON precision loss");
    Expect(first->find("future.vector") != std::string::npos, "Opaque property JSON must be retained");
    Expect(first->find("\"router\":\"test.router\"") != std::string::npos,
           "Opaque link router IDs must be encoded as stable strings");
    Expect(first->find("9007199254740993") != std::string::npos,
            "Opaque property numbers must not pass through lossy floating-point conversion");
    Expect(first->find("0.12345678901234567890123456789") != std::string::npos &&
               first->find("1e400") != std::string::npos && first->find("1e-400") != std::string::npos,
           "Opaque property numbers must preserve precision and values outside the double range");

    auto loaded = DeserializeGraphDocumentJson(*first, registry);
    Expect(loaded && loaded->warnings.empty(), "Known document must load without warnings");
    Expect(loaded->document.SchemaVersion() == 1 && loaded->document.ModelRevision() == 0 &&
               loaded->presentation.PresentationRevision() == 0,
           "Loaded state must start with persisted schema and fresh revisions");
    Expect(*loaded->document.FindGraph(fixture.graph) == *fixture.document.FindGraph(fixture.graph) &&
               loaded->presentation.Nodes() == fixture.presentation.Nodes() &&
               loaded->presentation.Links() == fixture.presentation.Links() &&
               loaded->presentation.Groups() == fixture.presentation.Groups(),
           "Document and presentation must round-trip exactly");
    auto canonical = SerializeGraphDocumentJson(loaded->document, loaded->presentation);
    Expect(canonical && *canonical == *first, "Deserialize-serialize must retain canonical document bytes");
    Expect(loaded->document.AllocateNodeId().Value() > fixture.sink.Value() &&
               loaded->presentation.AllocateGroupId().Value() > fixture.group.Value(),
           "Imported ID generators must observe every persisted ID");

    RegistryCatalog missing_plugins;
    auto unknown = DeserializeGraphDocumentJson(*first, missing_plugins);
    Expect(unknown && unknown->warnings.size() == 2,
           "Unregistered node types must load as preserved unknown nodes with "
           "warnings");
    auto unknown_round_trip = SerializeGraphDocumentJson(unknown->document, unknown->presentation);
    Expect(unknown_round_trip && *unknown_round_trip == *first,
            "Unknown nodes and properties must round-trip without data loss");

    Execute(
        fixture.commands,
        std::make_unique<SetLinkPresentationCommand>(fixture.link, LinkPresentation{}),
        fixture.document,
        fixture.presentation,
        fixture.types,
        "Empty present link presentation must be stored");
    auto empty_json = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
    auto empty_loaded = empty_json
        ? DeserializeGraphDocumentJson(*empty_json, registry)
        : Result<LoadedGraphDocument>{std::unexpected(Error{
              ErrorCode::InvalidFormat,
              "Empty presentation serialization failed",
          })};
    Expect(empty_loaded && empty_loaded->presentation.FindLink(fixture.link) != nullptr &&
               *empty_loaded->presentation.FindLink(fixture.link) == LinkPresentation{},
        "Serialized empty/default link presentation presence must round-trip exactly");
    auto empty_canonical = empty_loaded
        ? SerializeGraphDocumentJson(empty_loaded->document, empty_loaded->presentation)
        : Result<std::string>{std::unexpected(empty_loaded.error())};
    Expect(empty_canonical && empty_json && *empty_canonical == *empty_json,
        "Empty present link presentation must retain canonical serialized bytes");
}

void TestMigrationsAndFailures() {
    RegistryCatalog old_registry;
    RegisterBase(old_registry);
    auto fixture = MakeFixture(old_registry);
    auto json = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
    Expect(json.has_value(), "Migration fixture must serialize");

    int document_steps = 0;
    DocumentMigrationRegistry document_migrations{3};
    Expect(document_migrations
               .Register(1,
                         [&](DocumentMigrationContext& context) -> Result<void> {
                              ++document_steps;
                              Expect(context.from_version == 1 && context.to_version == 2,
                                     "Document migration context must describe one step");
                              for (auto& graph : context.archive.graphs) {
                                 std::vector<NodeId> node_ids;
                                 node_ids.reserve(graph.nodes.size());
                                 for (const auto& [id, node] : graph.nodes) {
                                     (void)node;
                                     node_ids.push_back(id);
                                 }
                                 for (const NodeId id : node_ids) {
                                     auto node = graph.nodes.at(id);
                                     node.properties["document_v2"] = true;
                                     graph.nodes.insert_or_assign(id, std::move(node));
                                 }
                              }
                             return {};
                         })
               .has_value(),
           "Document migration 1->2 must register");
    Expect(document_migrations
               .Register(2,
                         [&](DocumentMigrationContext&) -> Result<void> {
                             ++document_steps;
                             return {};
                         })
               .has_value(),
           "Document migration 2->3 must register");

    int node_steps = 0;
    RegistryCatalog current_registry;
    Expect(current_registry
               .RegisterNodeType(SourceDescriptor(
                   3,
                   [&](NodeMigrationContext& context) -> Result<void> {
                       ++node_steps;
                       if (context.from_version == 1) {
                           const PinId previous = context.creation.pins.front().id;
                           const PinId replacement = context.allocate_pin_id();
                           if (!replacement) {
                               return std::unexpected(Error{ErrorCode::MigrationFailed, "Pin IDs are exhausted"});
                           }
                           context.creation.pins.front().id = replacement;
                           context.creation.node.pins.front() = replacement;
                           context.remap_links(previous, replacement);
                       }
                       context.creation.node.properties[context.from_version == 1 ? "node_v2" : "node_v3"] = true;
                       return {};
                   }))
               .has_value(),
           "Migrating source descriptor must register");
    Expect(current_registry.RegisterNodeType(SinkDescriptor()).has_value(), "Current sink descriptor must register");

    auto loaded = DeserializeGraphDocumentJson(*json, current_registry, &document_migrations);
    Expect(loaded && document_steps == 2 && node_steps == 2 && loaded->document.SchemaVersion() == 3,
           "Document and node migrations must run as ordered contiguous chains");
    const auto* source = loaded->document.FindNode(fixture.graph, fixture.source);
    Expect(source != nullptr && source->type_version == 3 && source->properties.contains("document_v2") &&
               source->properties.contains("node_v2") && source->properties.contains("node_v3"),
           "Migration callbacks must update temporary archive state before "
            "hydration");
    Expect(source->pins.front() != fixture.output &&
               loaded->document.FindLink(fixture.graph, fixture.link)->output == source->pins.front() &&
               loaded->document.FindPin(fixture.graph, fixture.output) == nullptr,
            "Node migrations must atomically remap links when replacing connected pins");

    RegistryCatalog self_mutating_registry;
    std::size_t guarded_node_steps = 0;
    std::optional<ErrorCode> node_registry_error;
    Expect(self_mutating_registry.RegisterNodeType(SourceDescriptor(
               3,
               [&](NodeMigrationContext&) -> Result<void> {
                   ++guarded_node_steps;
                   auto removed = self_mutating_registry.UnregisterNodeType(TypeId{"io.source"});
                   if (!removed) node_registry_error = removed.error().code;
                   return {};
               })).has_value() &&
               self_mutating_registry.RegisterNodeType(SinkDescriptor()).has_value(),
           "Self-mutation migration descriptors must register");
    auto guarded_nodes = DeserializeGraphDocumentJson(*json, self_mutating_registry);
    Expect(guarded_nodes && guarded_node_steps == 2 && node_registry_error == ErrorCode::CommandFailed &&
               self_mutating_registry.Find(TypeId{"io.source"}),
           "Node migrations must pin descriptors and reject self-unregister");

    DocumentMigrationRegistry self_mutating_migrations{2};
    std::optional<ErrorCode> document_registry_error;
    Expect(self_mutating_migrations.Register(1, [&](DocumentMigrationContext&) -> Result<void> {
        auto registered = self_mutating_migrations.Register(
            2,
            [](DocumentMigrationContext&) -> Result<void> { return {}; });
        if (!registered) document_registry_error = registered.error().code;
        return {};
    }).has_value(), "Self-mutation document migration must register");
    auto guarded_document = DeserializeGraphDocumentJson(*json, old_registry, &self_mutating_migrations);
    Expect(guarded_document && document_registry_error == ErrorCode::CommandFailed,
           "Document migration callbacks must not mutate their active registry");

    std::unique_ptr<RegistryCatalog> destroyed_owner = std::make_unique<RegistryCatalog>();
    NodeTypeDescriptor destroying_descriptor = SourceDescriptor();
    destroying_descriptor.behavior = std::make_shared<const NodeBehavior>(NodeBehavior{
        .validate = [&](const NodeInstance&, std::span<const PinInstance>) {
        destroyed_owner.reset();
        return std::vector<std::string>{};
    }});
    Expect(destroyed_owner->RegisterNodeType(std::move(destroying_descriptor)).has_value() &&
               destroyed_owner->RegisterNodeType(SinkDescriptor()).has_value(),
           "Owner-destruction descriptors must register");
    auto owner_destroyed = DeserializeGraphDocumentJson(*json, *destroyed_owner);
    Expect(owner_destroyed && !destroyed_owner,
           "Registry invocation snapshots must survive destruction of the external owner");
    GraphIoLimits post_migration_limits;
    post_migration_limits.max_properties = 7;
    auto expanded_past_limit = DeserializeGraphDocumentJson(
        *json,
        current_registry,
        &document_migrations,
        post_migration_limits);
    Expect(!expanded_past_limit && expanded_past_limit.error().code == ErrorCode::SizeLimitExceeded,
           "Configured limits must be re-applied after document and node migrations");

    DocumentMigrationRegistry missing_document_step{3};
    Expect(missing_document_step.Register(1, [](DocumentMigrationContext&) -> Result<void> { return {}; }).has_value(),
           "Partial migration registry must accept its first step");
    auto missing_document = DeserializeGraphDocumentJson(*json, old_registry, &missing_document_step);
    Expect(!missing_document && missing_document.error().code == ErrorCode::MigrationMissing,
           "A missing document migration step must reject the complete load");

    RegistryCatalog missing_node_migration;
    Expect(missing_node_migration.RegisterNodeType(SourceDescriptor(2)).has_value(),
           "Descriptor without required migration must register");
    Expect(missing_node_migration.RegisterNodeType(SinkDescriptor()).has_value(),
           "Sink descriptor for missing migration test must register");
    auto missing_node = DeserializeGraphDocumentJson(*json, missing_node_migration);
    Expect(!missing_node && missing_node.error().code == ErrorCode::MigrationMissing,
           "A missing node migration must reject the complete load");

    std::string future = *json;
    const std::string version = "\"type_version\":1";
    const auto position = future.find(version);
    Expect(position != std::string::npos, "Serialized node version must be present");
    future.replace(position, version.size(), "\"type_version\":9");
    auto preserved_future = DeserializeGraphDocumentJson(future, current_registry);
    Expect(preserved_future && !preserved_future->warnings.empty() &&
               preserved_future->document.FindNode(fixture.graph, fixture.source)->type_version == 9,
           "Future node versions must be preserved without downgrade or old "
           "validation");
}

void TestMalformedLimitsAndFiles() {
    RegistryCatalog registry;
    RegisterBase(registry);
    auto fixture = MakeFixture(registry);
    auto json = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
    Expect(json.has_value(), "Malformed-input fixture must serialize");

    Expect(!DeserializeGraphDocumentJson("{}", registry), "Missing envelope fields must be rejected");
    Expect(!DeserializeGraphDocumentJson("{\"format\":\"uni.gui.nodes\",\"format\":\"duplicate\"}", registry),
           "Duplicate JSON keys must be rejected");
    Expect(!DeserializeGraphDocumentJson("{\"a\":1,\"\\u0061\":2}", registry),
           "Duplicate JSON keys must be detected after escape decoding");
    const std::string invalid_utf8{"{\"\xFF\":1}", 7};
    Expect(!DeserializeGraphDocumentJson(invalid_utf8, registry), "Malformed UTF-8 JSON must be rejected");

    std::string escaped_unicode = *json;
    const std::string source_name = "\"display_name\":\"Source\"";
    const auto source_name_position = escaped_unicode.find(source_name);
    Expect(source_name_position != std::string::npos, "Serialized source display name must be present");
    escaped_unicode.replace(
        source_name_position,
        source_name.size(),
        "\"display_name\":\"\\ud83d\\ude00\"");
    auto decoded_unicode = DeserializeGraphDocumentJson(escaped_unicode, registry);
    Expect(decoded_unicode && decoded_unicode->document.FindNode(fixture.graph, fixture.source)->display_name == "\xF0\x9F\x98\x80",
           "The JSON parser must decode valid surrogate pairs to UTF-8");
    std::string future_format = *json;
    const std::string format = "\"format_version\":4";
    future_format.replace(future_format.find(format), format.size(), "\"format_version\":5");
    auto unsupported = DeserializeGraphDocumentJson(future_format, registry);
    Expect(!unsupported && unsupported.error().code == ErrorCode::UnsupportedVersion,
           "Unsupported envelope versions must fail rather than discard data");
    std::string future_schema = *json;
    const std::string schema = "\"schema_version\":1";
    future_schema.replace(future_schema.find(schema), schema.size(), "\"schema_version\":2");
    auto unsupported_schema = DeserializeGraphDocumentJson(future_schema, registry);
    Expect(!unsupported_schema && unsupported_schema.error().code == ErrorCode::UnsupportedVersion,
           "A non-default document schema must require an explicit migration target");
    GraphIoLimits small;
    small.max_bytes = json->size() - 1;
    auto oversized = DeserializeGraphDocumentJson(*json, registry, nullptr, small);
    Expect(!oversized && oversized.error().code == ErrorCode::SizeLimitExceeded,
           "Configured JSON byte limits must be enforced before parsing");
    auto oversized_save = SerializeGraphDocumentJson(fixture.document, fixture.presentation, small);
    Expect(!oversized_save && oversized_save.error().code == ErrorCode::SizeLimitExceeded,
           "Serialization limits must prevent files that default loading would reject");
    GraphIoLimits small_dom;
    small_dom.max_json_values = 5;
    auto wide = DeserializeGraphDocumentJson(*json, registry, nullptr, small_dom);
    Expect(!wide && wide.error().code == ErrorCode::SizeLimitExceeded,
           "JSON DOM value limits must be enforced during parsing");
    GraphIoLimits small_string;
    small_string.max_string_bytes = 4;
    auto long_string = DeserializeGraphDocumentJson(*json, registry, nullptr, small_string);
    Expect(!long_string && long_string.error().code == ErrorCode::SizeLimitExceeded,
           "JSON string limits must be enforced while strings are built");
    GraphIoLimits escaped_string;
    escaped_string.max_string_bytes = 3;
    auto escaped_too_long = DeserializeGraphDocumentJson("\"\\ud83d\\ude00\"", registry, nullptr, escaped_string);
    Expect(!escaped_too_long && escaped_too_long.error().code == ErrorCode::SizeLimitExceeded,
           "JSON string limits must count decoded UTF-8 bytes before SAX materialization");
    GraphIoLimits zero_depth;
    zero_depth.max_depth = 0;
    auto root_past_depth = DeserializeGraphDocumentJson("null", registry, nullptr, zero_depth);
    Expect(!root_past_depth && root_past_depth.error().code == ErrorCode::SizeLimitExceeded,
           "Root scalar values must respect max_depth");
    GraphIoLimits one_depth;
    one_depth.max_depth = 1;
    auto scalar_past_depth = DeserializeGraphDocumentJson("{\"value\":0}", registry, nullptr, one_depth);
    Expect(!scalar_past_depth && scalar_past_depth.error().code == ErrorCode::SizeLimitExceeded,
           "Nested scalar values must respect max_depth");

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() / ("unigui-nodes-" + std::to_string(stamp) + ".json");
    Expect(SaveGraphDocumentJson(path.string(), fixture.document, fixture.presentation).has_value(),
           "Document save must atomically create a JSON file");
    const std::string saved = ReadBytes(path);
    Expect(saved == *json, "Saved bytes must match deterministic serialization");
#ifndef _WIN32
    std::error_code permission_error;
    const auto private_permissions = std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
    std::filesystem::permissions(path, private_permissions, std::filesystem::perm_options::replace, permission_error);
    Expect(!permission_error && SaveGraphDocumentJson(path.string(), fixture.document, fixture.presentation).has_value() &&
               std::filesystem::status(path, permission_error).permissions() == private_permissions,
           "Atomic replacement must preserve existing POSIX file permissions");
#endif
    auto from_file = LoadGraphDocumentJson(path.string(), registry);
    Expect(from_file && *from_file->document.FindGraph(fixture.graph) == *fixture.document.FindGraph(fixture.graph),
           "Saved document must load from disk");
    std::string nul_path = path.string();
    nul_path.append("\0ignored", 8);
    auto rejected_path = SaveGraphDocumentJson(nul_path, fixture.document, fixture.presentation);
    Expect(!rejected_path && rejected_path.error().code == ErrorCode::InvalidArgument && ReadBytes(path) == saved,
           "Embedded NUL paths must be rejected before opening a temporary file");

    std::string deep_opaque = "{\"kind\":\"future.deep\",\"value\":";
    deep_opaque.append(140, '[');
    deep_opaque += '0';
    deep_opaque.append(140, ']');
    deep_opaque += '}';
    Execute(fixture.commands,
            std::make_unique<SetNodePropertyCommand>(fixture.graph, fixture.source, "deep", OpaqueJsonProperty{deep_opaque}),
            fixture.document, fixture.presentation, fixture.types,
            "Opaque property depth must be validated independently of default IO limits");
    auto default_depth = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
    GraphIoLimits deep_limits;
    deep_limits.max_depth = 256;
    auto custom_depth = SerializeGraphDocumentJson(fixture.document, fixture.presentation, deep_limits);
    Expect(!default_depth && default_depth.error().code == ErrorCode::SizeLimitExceeded && custom_depth,
           "Explicitly raised IO limits must support deeper opaque future properties");
    Expect(fixture.commands.Undo(fixture.document, fixture.presentation, fixture.types).has_value(),
           "Deep opaque property test edit must undo");

    Execute(fixture.commands,
            std::make_unique<SetNodeDisplayNameCommand>(fixture.graph, fixture.source,
                                                        std::string{"invalid-utf8-\xFF", 14}),
            fixture.document, fixture.presentation, fixture.types,
            "Model strings are checked by persistence validation");
    auto failed_save = SaveGraphDocumentJson(path.string(), fixture.document, fixture.presentation);
    Expect(!failed_save && ReadBytes(path) == saved, "Serialization failure must leave the previous destination file "
                                                     "unchanged");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void TestFragmentSerializationAndPaste() {
    RegistryCatalog registry;
    RegisterBase(registry);
    auto fixture = MakeFixture(registry);
    auto fragment = CaptureGraphFragment(fixture.document, fixture.presentation,
                                         GraphSelection{.graph = fixture.graph, .groups = {fixture.group}});
    Expect(fragment && fragment->nodes.size() == 2 && fragment->links.size() == 1,
           "Fixture selection must capture a complete fragment");
    auto serialized = SerializeGraphFragmentJson(*fragment);
    Expect(serialized.has_value(), "Graph fragment must serialize");
    auto local_subgraph = *fragment;
    local_subgraph.nodes.front().creation.node.subgraph = SubgraphReference{
        .ownership = SubgraphOwnership::Referenced,
        .target = DocumentGraphTarget{GraphId{999}},
    };
    local_subgraph.nodes.front().creation.node.role = NodeRole::Subgraph;
    auto rejected_subgraph = SerializeGraphFragmentJson(local_subgraph);
    Expect(!rejected_subgraph && rejected_subgraph.error().code == ErrorCode::InvalidGraph,
           "Serialized fragments must reject ambiguous document-local subgraph references");
    auto reordered = *fragment;
    std::ranges::reverse(reordered.nodes);
    std::ranges::reverse(reordered.links);
    std::ranges::reverse(reordered.groups);
    auto deterministic = SerializeGraphFragmentJson(reordered);
    Expect(deterministic && *deterministic == *serialized, "Fragment serialization must sort identity-keyed records "
                                                           "deterministically");

    auto decoded = DeserializeGraphFragmentJson(*serialized, registry);
    Expect(decoded && *SerializeGraphFragmentJson(*decoded) == *serialized,
           "Graph fragment must have a canonical JSON round-trip");

    int fragment_migration_calls = 0;
    RegistryCatalog migrated_registry;
    Expect(migrated_registry.RegisterNodeType(SourceDescriptor(
        2,
        [&](NodeMigrationContext& context) -> Result<void> {
            ++fragment_migration_calls;
            const PinId previous = context.creation.pins.front().id;
            const PinId replacement = context.allocate_pin_id();
            if (!replacement) {
                return std::unexpected(Error{ErrorCode::MigrationFailed, "Pin IDs are exhausted"});
            }
            context.creation.pins.front().id = replacement;
            context.creation.node.pins.front() = replacement;
            context.remap_links(previous, replacement);
            context.creation.node.properties["fragment_migrated"] = true;
            return {};
        })).has_value(), "Fragment migration descriptor must register");
    Expect(migrated_registry.RegisterNodeType(SinkDescriptor()).has_value(),
           "Fragment migration sink descriptor must register");
    std::string malformed_role = *serialized;
    const std::string regular_role = "\"role\":\"regular\"";
    const auto role_position = malformed_role.find(regular_role);
    Expect(role_position != std::string::npos, "Serialized fragment must contain a regular node role");
    malformed_role.replace(role_position, regular_role.size(), "\"role\":\"unsupported\"");
    auto rejected_before_migration = DeserializeGraphFragmentJson(malformed_role, migrated_registry);
    Expect(!rejected_before_migration && rejected_before_migration.error().code == ErrorCode::InvalidFormat &&
               fragment_migration_calls == 0,
            "Malformed structural fields must be rejected before application migration callbacks");
    auto migrated_fragment = DeserializeGraphFragmentJson(*serialized, migrated_registry);
    Expect(migrated_fragment.has_value(), "Migrated fragment must deserialize");
    const auto migrated_source = std::ranges::find_if(migrated_fragment->nodes, [](const GraphFragmentNode& node) {
        return node.creation.node.type == TypeId{"io.source"};
    });
    Expect(migrated_source != migrated_fragment->nodes.end() &&
               migrated_fragment->links.front().link.output == migrated_source->creation.node.pins.front() &&
               migrated_fragment->links.front().link.output != fixture.output,
           "Fragment node migrations must remap connected link endpoints");
    GraphIoLimits fragment_migration_limits;
    fragment_migration_limits.max_properties = 7;
    auto expanded_fragment = DeserializeGraphFragmentJson(
        *serialized,
        migrated_registry,
        fragment_migration_limits);
    Expect(!expanded_fragment && expanded_fragment.error().code == ErrorCode::SizeLimitExceeded,
           "Configured limits must be re-applied after fragment node migrations");

    auto prepared = PrepareGraphFragmentPaste(fixture.document, fixture.presentation, fixture.types, *decoded,
                                              fixture.graph, {800.0f, 300.0f});
    Expect(prepared && prepared->remap.nodes.size() == 2 && prepared->remap.links.size() == 1 &&
               prepared->remap.groups.size() == 1 && prepared->remap.route_points.size() == 1,
           "Deserialized fragment must pass target-aware preflight with complete "
           "ID remapping");
    const auto pasted_source = prepared->remap.nodes.at(fixture.source);
    Execute(fixture.commands, std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared)), fixture.document,
            fixture.presentation, fixture.types, "Deserialized fragment must paste atomically");
    Expect(fixture.document.FindNode(fixture.graph, pasted_source) != nullptr &&
               fixture.commands.Undo(fixture.document, fixture.presentation, fixture.types).has_value() &&
               fixture.document.FindNode(fixture.graph, pasted_source) == nullptr,
           "Serialized fragment paste must remain one undoable command");
    Expect(!DeserializeGraphFragmentJson(*SerializeGraphDocumentJson(fixture.document, fixture.presentation), registry),
           "Document and fragment envelopes must not be interchangeable");
}

void TestEditorOsClipboard() {
    RegistryCatalog registry;
    RegisterBase(registry);
    auto fixture = MakeFixture(registry);
    NodeUiRegistry ui_registry;
    LinkRouterRegistry routers;

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {900.0f, 700.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Expect(pixels != nullptr, "Clipboard test font atlas must build");
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
        ClipboardText = text != nullptr ? text : "";
    };
    ImGui::GetPlatformIO().Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
        return ClipboardText.c_str();
    };

    const auto draw = [&](EditorContext& editor, const char* window) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoDecoration);
        const auto result = DrawEditor(editor, fixture.document, fixture.presentation, fixture.commands, registry,
                                       ui_registry, routers, {700.0f, 520.0f});
        ImGui::End();
        ImGui::Render();
        return result;
    };

    EditorContext source_editor;
    source_editor.SetSelection(GraphSelection{.graph = fixture.graph, .nodes = {fixture.source}});
    source_editor.CopySelection();
    (void)draw(source_editor, "Clipboard source");
    Expect(ClipboardText.find("\"kind\":\"fragment\"") != std::string::npos,
           "Editor copy must place a serialized graph fragment in the platform "
           "clipboard");

    EditorContext target_editor;
    target_editor.PasteAt({600.0f, 100.0f});
    const auto pasted = draw(target_editor, "Clipboard target");
    Expect(pasted.model_changed && fixture.document.FindGraph(fixture.graph)->nodes.size() == 3,
           "A second editor context must paste from the shared OS clipboard");

    ClipboardText = "unrelated clipboard text";
    target_editor.PasteAt({700.0f, 100.0f});
    const auto rejected = draw(target_editor, "Clipboard invalid");
    Expect(!rejected.model_changed && fixture.document.FindGraph(fixture.graph)->nodes.size() == 3 &&
               !target_editor.LastError().empty(),
           "Invalid clipboard text must not fall back to stale graph data");

    source_editor.DuplicateSelection();
    const auto duplicated = draw(source_editor, "Clipboard duplicate");
    Expect(duplicated.model_changed && fixture.document.FindGraph(fixture.graph)->nodes.size() == 4 &&
               ClipboardText == "unrelated clipboard text",
           "Duplicate must use an in-process fragment without overwriting the OS "
           "clipboard");
    ImGui::DestroyContext();
}

void TestProtectionRoundTrip() {
    RegistryCatalog registry;
    RegisterBase(registry);
    auto fixture = MakeFixture(registry);
    fixture.commands.Clear();
    Execute(fixture.commands, std::make_unique<SetPinReadOnlyCommand>(
                fixture.graph, fixture.output, true),
            fixture.document, fixture.presentation, fixture.types, "Pin protection must be set");
    Execute(fixture.commands, std::make_unique<SetLinkReadOnlyCommand>(
                fixture.graph, fixture.link, true),
            fixture.document, fixture.presentation, fixture.types, "Link protection must be set");
    Execute(fixture.commands, std::make_unique<SetNodeLockedCommand>(fixture.source, true),
            fixture.document, fixture.presentation, fixture.types, "Node lock must be set");
    Execute(fixture.commands, std::make_unique<SetLinkLockedCommand>(fixture.link, true),
            fixture.document, fixture.presentation, fixture.types, "Link lock must be set");
    Execute(fixture.commands, std::make_unique<SetGroupLockedCommand>(fixture.group, true),
            fixture.document, fixture.presentation, fixture.types, "Group lock must be set");
    Execute(fixture.commands, std::make_unique<SetNodeReadOnlyCommand>(
                fixture.graph, fixture.source, true),
            fixture.document, fixture.presentation, fixture.types, "Node protection must be set");

    auto protected_fragment = CaptureGraphFragment(
        fixture.document,
        fixture.presentation,
        GraphSelection{.graph = fixture.graph, .groups = {fixture.group}});
    Expect(protected_fragment.has_value(), "Protected fragment must be captured");
    auto protected_fragment_json = SerializeGraphFragmentJson(*protected_fragment);
    Expect(protected_fragment_json.has_value(), "Protected fragment must serialize");
    auto decoded_fragment = DeserializeGraphFragmentJson(*protected_fragment_json, registry);
    Expect(decoded_fragment.has_value(), "Protected fragment must deserialize");
    auto prepared_fragment = PrepareGraphFragmentPaste(
        fixture.document,
        fixture.presentation,
        fixture.types,
        *decoded_fragment,
        fixture.graph,
        Vec2{700.0f, 300.0f});
    Expect(prepared_fragment.has_value(), "Protected fragment paste must prepare");
    const auto protected_remap = prepared_fragment->remap;
    Execute(fixture.commands, std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared_fragment)),
            fixture.document, fixture.presentation, fixture.types, "Protected fragment must paste");
    Expect(fixture.document.FindNode(fixture.graph, protected_remap.nodes.at(fixture.source))->read_only &&
               fixture.document.FindPin(fixture.graph, protected_remap.pins.at(fixture.output))->read_only &&
               fixture.document.FindLink(fixture.graph, protected_remap.links.at(fixture.link))->read_only &&
               fixture.presentation.FindNode(protected_remap.nodes.at(fixture.source))->locked &&
               fixture.presentation.FindLink(protected_remap.links.at(fixture.link))->Style().locked &&
               fixture.presentation.FindGroup(protected_remap.groups.at(fixture.group))->protection.locked,
           "Protected fragment paste must restore topology before applying all protection flags");
    Execute(fixture.commands, std::make_unique<SetGraphReadOnlyCommand>(fixture.graph, true),
            fixture.document, fixture.presentation, fixture.types, "Graph protection must be set");

    auto json = SerializeGraphDocumentJson(fixture.document, fixture.presentation);
    Expect(json && json->find("\"format_version\":4") != std::string::npos &&
               json->find("\"read_only\":true") != std::string::npos &&
               json->find("\"locked\":true") != std::string::npos,
            "Protection state must be encoded by the current wire format");
    auto loaded = DeserializeGraphDocumentJson(*json, registry);
    Expect(loaded.has_value(), "Protected document must load");
    const auto* graph = loaded->document.FindGraph(fixture.graph);
    Expect(graph != nullptr && graph->read_only &&
               loaded->document.FindNode(fixture.graph, fixture.source)->read_only &&
               loaded->document.FindPin(fixture.graph, fixture.output)->read_only &&
               loaded->document.FindLink(fixture.graph, fixture.link)->read_only &&
               loaded->presentation.FindNode(fixture.source)->locked &&
               loaded->presentation.FindLink(fixture.link)->Style().locked &&
               loaded->presentation.FindGroup(fixture.group)->protection.locked,
           "All semantic and presentation protection flags must round-trip exactly");
    CommandStack commands;
    auto blocked = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(
            fixture.graph,
            fixture.source,
            "blocked_after_load",
            PropertyValue{std::int64_t{1}}),
        loaded->document,
        loaded->presentation,
        fixture.types);
    Expect(!blocked && blocked.error().code == ErrorCode::ReadOnly,
           "Loaded protection state must be enforced by normal commands");
}

void TestGraphAssetsAndStageFourPersistence() {
    RegistryCatalog registry;
    RegisterBase(registry);
    RegistryCatalog& types = registry;

    GraphAsset asset;
    asset.id = GraphAssetId{"test.asset.filter"};
    CommandStack asset_commands;
    GraphInterface interface{
        .version = 2,
        .pins = {
            GraphInterfacePin{
                .key = "signal",
                .label = "Signal",
                .type = TypeId{"float"},
                .direction = PinDirection::Input,
            },
        },
    };
    Execute(
        asset_commands,
        std::make_unique<SetGraphInterfaceCommand>(asset.document.RootGraph(), interface),
        asset.document,
        asset.presentation,
        types,
        "Graph asset interface must be created");

    auto serialized = SerializeGraphAssetJson(asset);
    Expect(serialized && serialized->find("\"kind\":\"graph_asset\"") != std::string::npos &&
               serialized->find("\"format_version\":4") != std::string::npos,
            "Graph assets must use their own deterministic version-four envelope");
    auto loaded = DeserializeGraphAssetJson(*serialized, registry);
    Expect(loaded && loaded->asset.id == asset.id &&
               *SerializeGraphAssetJson(loaded->asset) == *serialized,
           "Graph assets must round-trip canonically with their interface contract");

    GraphAssetRegistry assets;
    Expect(assets.Write(std::move(loaded->asset), GraphAssetWriteMode::Insert).has_value(),
           "A valid graph asset must register");
    const auto registered = assets.Find(GraphAssetId{"test.asset.filter"});
    Expect(registered != nullptr, "Registered graph assets must resolve by stable ID");

    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    const GraphId root = document.RootGraph();
    const NodeId call = document.AllocateNodeId();
    Execute(commands, std::make_unique<AddNodeCommand>(
        root,
        NodeCreation{.node = NodeInstance{.id = call, .type = TypeId{"asset.call"}}}),
        document, presentation, types, "Graph asset call-site must be added");
    Execute(commands, std::make_unique<SetNodeSubgraphCommand>(
        root,
        call,
        SubgraphReference{
            .ownership = SubgraphOwnership::Referenced,
            .target = GraphAssetTarget{
                .asset = GraphAssetId{"test.asset.filter"},
                .interface = interface,
            },
        }), document, presentation, types, "Graph asset call-site must bind");
    Expect(ValidateGraphDependencies(document, assets).empty() &&
               document.FindNode(root, call)->pins.size() == 1,
           "Resolved graph asset references must validate and project interface pins");

    auto document_json = SerializeGraphDocumentJson(document, presentation);
    auto loaded_document = DeserializeGraphDocumentJson(*document_json, registry);
    Expect(loaded_document && !loaded_document->warnings.empty() &&
               loaded_document->document.FindNode(root, call)->subgraph.has_value(),
           "Unresolved asset references must load with a warning and round-trip losslessly");

    auto asset_fragment = CaptureGraphFragment(
        document,
        presentation,
        GraphSelection{.graph = root, .nodes = {call}});
    auto asset_fragment_json = SerializeGraphFragmentJson(*asset_fragment);
    auto loaded_asset_fragment = DeserializeGraphFragmentJson(*asset_fragment_json, registry);
    Expect(loaded_asset_fragment && loaded_asset_fragment->nodes.front().creation.node.subgraph.has_value(),
           "Serialized fragments must preserve portable graph asset references");

    const NodeId owned_call = document.AllocateNodeId();
    Execute(commands, std::make_unique<AddNodeCommand>(
        root,
        NodeCreation{.node = NodeInstance{.id = owned_call, .type = TypeId{"owned.call"}}}),
        document, presentation, types, "Owned fragment call-site must be added");
    const GraphId owned_graph = document.AllocateGraphId();
    std::vector<std::unique_ptr<Command>> create_owned;
    create_owned.push_back(std::make_unique<AddGraphCommand>(Graph{
        .id = owned_graph,
        .lifetime = GraphLifetime::Owned,
    }));
    create_owned.push_back(std::make_unique<SetNodeSubgraphCommand>(
        root,
        owned_call,
        SubgraphReference{
            .ownership = SubgraphOwnership::Owned,
            .target = DocumentGraphTarget{owned_graph},
        }));
    create_owned.push_back(std::make_unique<SetGraphInterfaceCommand>(owned_graph, GraphInterface{}));
    Execute(commands, std::make_unique<CompoundCommand>("Create owned fragment graph", std::move(create_owned)),
            document, presentation, types, "Owned fragment hierarchy must be created");
    auto owned_fragment = CaptureGraphFragment(
        document,
        presentation,
        GraphSelection{.graph = root, .nodes = {owned_call}});
    auto owned_json = SerializeGraphFragmentJson(*owned_fragment);
    auto decoded_owned = DeserializeGraphFragmentJson(*owned_json, registry);
    Expect(decoded_owned && decoded_owned->owned_graphs.size() == 1,
           "Serialized fragments must carry the complete owned graph bundle");
    auto incompatible_owned = *decoded_owned;
    auto& incompatible_graph = incompatible_owned.owned_graphs.front().graph;
    const NodeId incompatible_source{9'100'001};
    const NodeId incompatible_sink{9'100'002};
    const PinId incompatible_output{9'200'001};
    const PinId incompatible_input{9'200'002};
    const LinkId incompatible_link{9'300'001};
    incompatible_graph.nodes.emplace(incompatible_source, NodeInstance{
        .id = incompatible_source,
        .type = TypeId{"fragment.incompatible.source"},
        .pins = {incompatible_output},
    });
    incompatible_graph.nodes.emplace(incompatible_sink, NodeInstance{
        .id = incompatible_sink,
        .type = TypeId{"fragment.incompatible.sink"},
        .pins = {incompatible_input},
    });
    incompatible_graph.pins.emplace(incompatible_output, PinInstance{
        .id = incompatible_output,
        .node = incompatible_source,
        .key = "output",
        .type = TypeId{"fragment.left"},
        .direction = PinDirection::Output,
    });
    incompatible_graph.pins.emplace(incompatible_input, PinInstance{
        .id = incompatible_input,
        .node = incompatible_sink,
        .key = "input",
        .type = TypeId{"fragment.right"},
    });
    incompatible_graph.links.emplace(incompatible_link, Link{
        .id = incompatible_link,
        .output = incompatible_output,
        .input = incompatible_input,
    });
    auto rejected_incompatible_owned = PrepareGraphFragmentPaste(
        document,
        presentation,
        types,
        incompatible_owned,
        root,
        Vec2{650.0f, 300.0f});
    Expect(!rejected_incompatible_owned && rejected_incompatible_owned.error().code == ErrorCode::InvalidGraph,
           "Owned fragment links must enforce the same type compatibility as root fragment links");
    auto malformed_owned = *decoded_owned;
    auto& malformed_graph = malformed_owned.owned_graphs.front().graph;
    const NodeId malformed_node_id = malformed_graph.nodes.begin()->first;
    auto malformed_node = malformed_graph.nodes.at(malformed_node_id);
    malformed_node.pins.push_back(PinId{999999});
    malformed_graph.nodes.insert_or_assign(malformed_node_id, std::move(malformed_node));
    auto rejected_owned_json = SerializeGraphFragmentJson(malformed_owned);
    auto rejected_owned_paste = PrepareGraphFragmentPaste(
        document,
        presentation,
        types,
        malformed_owned,
        root,
        Vec2{650.0f, 300.0f});
    Expect(!rejected_owned_json && rejected_owned_json.error().code == ErrorCode::InvalidGraph &&
               !rejected_owned_paste && rejected_owned_paste.error().code == ErrorCode::InvalidGraph,
           "Malformed owned graph topology must return a structured error before migration or remapping");
    auto prepared_owned = PrepareGraphFragmentPaste(
        document,
        presentation,
        types,
        *decoded_owned,
        root,
        Vec2{600.0f, 250.0f});
    Expect(prepared_owned && prepared_owned->remap.graphs.size() == 1,
           "Deserialized owned graph bundles must receive fresh graph IDs");
    const GraphId pasted_owned_graph = prepared_owned->remap.graphs.at(owned_graph);
    Execute(commands, std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared_owned)),
            document, presentation, types, "Deserialized owned graph bundle must paste");
    Expect(document.FindGraph(pasted_owned_graph) != nullptr,
           "Owned graph fragment paste must restore its remapped graph closure");

    const auto make_dependent_asset = [&](const char* id, const char* dependency) {
        GraphAsset value;
        value.id = GraphAssetId{id};
        const NodeId node = value.document.AllocateNodeId();
        CommandStack stack;
        Execute(stack, std::make_unique<AddNodeCommand>(
            value.document.RootGraph(),
            NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"asset.call"}}}),
            value.document, value.presentation, types, "Asset dependency node must be added");
        Execute(stack, std::make_unique<SetNodeSubgraphCommand>(
            value.document.RootGraph(),
            node,
            SubgraphReference{
                .ownership = SubgraphOwnership::Referenced,
                .target = GraphAssetTarget{
                    .asset = GraphAssetId{dependency},
                    .interface = GraphInterface{},
                },
            }), value.document, value.presentation, types, "Asset dependency must bind");
        return value;
    };
    auto asset_a = make_dependent_asset("test.asset.a", "test.asset.b");
    auto asset_b = make_dependent_asset("test.asset.b", "test.asset.a");
    auto unresolved = assets.Write(std::move(asset_a), GraphAssetWriteMode::Insert);
    Expect(!unresolved && unresolved.error().code == ErrorCode::AssetNotFound,
            "Assets with unresolved dependencies must be rejected atomically");
    auto recursive = assets.Write(std::move(asset_b), GraphAssetWriteMode::Insert);
    Expect(!recursive && recursive.error().code == ErrorCode::AssetNotFound,
            "A dependency must exist before an asset can be inserted");
}

void TestWorkspacePersistenceFacade() {
    NodeEditorWorkspace workspace;
    const GraphId graph = workspace.document.RootGraph();
    const NodeId saved_node = workspace.document.AllocateNodeId();
    Expect(workspace.Execute(std::make_unique<AddNodeCommand>(
               graph,
               NodeCreation{.node = NodeInstance{.id = saved_node, .type = TypeId{"workspace.io"}}})).has_value(),
           "Workspace persistence fixture node must execute");

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path() /
        ("unigui-workspace-" + std::to_string(stamp) + ".json");
    Expect(workspace.Save(path.string()).has_value(),
           "Workspace Save must persist its retained document and presentation");

    const NodeId transient = workspace.document.AllocateNodeId();
    Expect(workspace.Execute(std::make_unique<AddNodeCommand>(
               graph,
               NodeCreation{.node = NodeInstance{.id = transient, .type = TypeId{"workspace.transient"}}})).has_value(),
           "Workspace transient node must execute");
    auto loaded = workspace.Load(path.string());
    Expect(loaded && workspace.document.FindNode(graph, saved_node) != nullptr &&
               workspace.document.FindNode(graph, transient) == nullptr && !workspace.commands.CanUndo(),
           "Workspace Load must atomically replace retained state and clear command history");

    std::optional<ErrorCode> reentrant_load_error;
    GraphPolicy reentrant_load;
    reentrant_load.evaluate_operation = [&](const OperationPolicyContext&, const OperationIntent&)
        -> OperationPolicyDecision {
        auto nested = workspace.Load(path.string());
        if (!nested) reentrant_load_error = nested.error().code;
        return AllowOperation{};
    };
    auto outer = workspace.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, saved_node, "outer", PropertyValue{true}),
        reentrant_load);
    Expect(outer && reentrant_load_error == ErrorCode::CommandFailed && workspace.commands.CanUndo() &&
                workspace.document.FindNode(graph, saved_node)->properties.contains("outer"),
            "Workspace Load must reject reentrant replacement without clearing active history");
    Expect(workspace.Undo().has_value(), "Workspace history must remain valid after rejected reentrant load");

    Expect(workspace.Execute(std::make_unique<SetNodePropertyCommand>(
               graph, saved_node, "migration-history", PropertyValue{true})).has_value() &&
               workspace.commands.CanUndo(),
           "Workspace migration guard fixture must create history");
    Expect(workspace.RegisterNodeType(NodeTypeDescriptor{
               .type = TypeId{"workspace.converter"},
               .display_name = "Workspace converter",
               .static_pins = {
                   PinDescriptor{.key = "input", .type = TypeId{"workspace.left"}},
                   PinDescriptor{
                       .key = "output",
                       .type = TypeId{"workspace.right"},
                       .direction = PinDirection::Output,
                   },
               },
           }).has_value(),
           "Workspace migration conversion descriptor must register");
    const ConversionDescriptor workspace_conversion{
        .key = ConversionKey{
            .source_type = TypeId{"workspace.left"},
            .destination_type = TypeId{"workspace.right"},
            .kind = PinKind::Data,
        },
        .node_type = TypeId{"workspace.converter"},
        .input_pin = "input",
        .output_pin = "output",
    };
    std::optional<ErrorCode> direct_stack_error;
    std::optional<ErrorCode> workspace_registry_error;
    std::optional<ErrorCode> workspace_type_registry_error;
    DocumentMigrationRegistry guarded_migration{2};
    Expect(guarded_migration.Register(1, [&](DocumentMigrationContext&) -> Result<void> {
        workspace.commands.Clear();
        auto registered = workspace.RegisterNodeType(NodeTypeDescriptor{
            .type = TypeId{"workspace.reentrant"},
            .display_name = "Reentrant",
        });
        if (!registered) workspace_registry_error = registered.error().code;
        auto conversion = workspace.RegisterConversion(workspace_conversion);
        if (!conversion) workspace_type_registry_error = conversion.error().code;
        auto direct = workspace.commands.Execute(
            std::make_unique<SetNodePropertyCommand>(
                graph, saved_node, "migration-direct", PropertyValue{true}),
            workspace.document,
            workspace.presentation,
            workspace.Registry());
        if (!direct) direct_stack_error = direct.error().code;
        return std::unexpected(Error{ErrorCode::CommandFailed, "Intentional migration failure"});
    }).has_value(), "Workspace migration guard fixture must register");
    auto guarded_failure = workspace.Load(path.string(), &guarded_migration);
    Expect(!guarded_failure && guarded_failure.error().code == ErrorCode::MigrationFailed &&
               direct_stack_error == ErrorCode::CommandFailed &&
               workspace_registry_error == ErrorCode::CommandFailed &&
               workspace_type_registry_error == ErrorCode::CommandFailed && workspace.commands.CanUndo() &&
               workspace.document.FindNode(graph, saved_node)->properties.contains("migration-history") &&
               !workspace.document.FindNode(graph, saved_node)->properties.contains("migration-direct"),
           "Migration callbacks must not mutate or clear the live command stack during Load");

    GraphPolicy defer;
    defer.evaluate_operation = [](const OperationPolicyContext&, const OperationIntent& operation)
        -> OperationPolicyDecision {
        return operation.kind == OperationKind::SetNodeProperty
            ? OperationPolicyDecision{DeferOperation{std::uint64_t{1}}}
            : OperationPolicyDecision{AllowOperation{}};
    };
    auto pending = workspace.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, saved_node, "pending", PropertyValue{true}),
        defer);
    Expect(pending && pending->deferred, "Workspace load-pending fixture must defer");
    const auto pending_path = std::filesystem::path{path.string() + ".pending"};
    Expect(workspace.Save(pending_path.string()).has_value() && workspace.HasPending(),
           "Workspace Save must remain available while a deferred transaction is retained");
    auto pending_saved = LoadGraphDocumentJson(pending_path.string(), workspace.Registry());
    const NodeInstance* pending_saved_node = pending_saved
        ? pending_saved->document.FindNode(graph, saved_node)
        : nullptr;
    Expect(pending_saved && pending_saved_node != nullptr &&
               pending_saved_node->properties.contains("migration-history") &&
               !pending_saved_node->properties.contains("pending") &&
               workspace.HasPending(),
           "Save during pending must persist committed state only and retain pending work in memory");
    auto blocked = workspace.Load(path.string());
    Expect(!blocked && blocked.error().code == ErrorCode::OperationPending && workspace.HasPending(),
           "Workspace Load must reject replacement while a deferred transaction is retained");
    Expect(workspace.Cancel(pending->deferred->id).has_value(),
           "Workspace deferred fixture must cancel");

    const auto identity = workspace.document.Identity();
    auto failed = workspace.Load((path.string() + ".missing"));
    Expect(!failed && workspace.document.Identity() == identity &&
               workspace.document.FindNode(graph, saved_node) != nullptr,
           "Failed workspace Load must preserve the complete live workspace state");
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    std::filesystem::remove(pending_path, cleanup_error);
}

} // namespace

int main() {
    TestDocumentRoundTripAndUnknownData();
    TestMigrationsAndFailures();
    TestMalformedLimitsAndFiles();
    TestFragmentSerializationAndPaste();
    TestEditorOsClipboard();
    TestProtectionRoundTrip();
    TestGraphAssetsAndStageFourPersistence();
    TestWorkspacePersistenceFacade();
    return EXIT_SUCCESS;
}
