#include <uni/gui/nodes/nodes.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Uni::GUI::Nodes;

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void Execute(CommandStack& commands, std::unique_ptr<Command> command, GraphDocument& document, GraphPresentation& presentation,
             const RegistryCatalog& registry, const char* message) {
    Expect(commands.Execute(std::move(command), document, presentation, registry).has_value(), message);
}

[[nodiscard]] const PinInstance* FindPin(const GraphDocument& document, const GraphId graph, const NodeId node, const std::string_view key) {
    const auto* instance = document.FindNode(graph, node);
    if (instance == nullptr)
        return nullptr;
    for (const PinId id : instance->pins) {
        const auto* pin = document.FindPin(graph, id);
        if (pin != nullptr && pin->key == key)
            return pin;
    }
    return nullptr;
}

class TransactionPropertyCommand final : public Command {
  public:
    TransactionPropertyCommand(GraphId graph, NodeId node, std::string key,
                               std::optional<PropertyValue> value)
        : m_graph{graph}, m_node{node}, m_key{std::move(key)}, m_value{std::move(value)} {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Set property through transaction";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction,
                                     const RegistrySnapshot&) override {
        const auto* node = transaction.Document().FindNode(m_graph, m_node);
        if (node == nullptr)
            return std::unexpected(Error{ErrorCode::NodeNotFound, "Node does not exist"});
        if (!m_captured) {
            const auto found = node->properties.find(m_key);
            if (found != node->properties.end())
                m_previous = found->second;
            m_captured = true;
        }
        return transaction.SetNodeProperty(m_graph, m_node, m_key, m_value);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        return transaction.SetNodeProperty(m_graph, m_node, m_key, m_previous);
    }

    GraphId m_graph;
    NodeId m_node;
    std::string m_key;
    std::optional<PropertyValue> m_value;
    std::optional<PropertyValue> m_previous;
    bool m_captured{false};
};

[[nodiscard]] std::string Mode(const PropertyBag& properties) {
    const auto found = properties.find("mode");
    if (found == properties.end())
        return "source_sink";
    const auto* mode = std::get_if<std::string>(&found->second);
    return mode != nullptr ? *mode : "invalid";
}

[[nodiscard]] PinDescriptor SourcePin(const std::string& label) {
    return PinDescriptor{
        .key = "source",
        .label = label,
        .type = TypeId{"float"},
        .direction = PinDirection::Output,
        .cardinality = PinCardinality::Multiple,
    };
}

[[nodiscard]] PinDescriptor SinkPin() {
    return PinDescriptor{
        .key = "sink",
        .label = "Sink input",
        .type = TypeId{"float"},
        .direction = PinDirection::Input,
        .cardinality = PinCardinality::Multiple,
    };
}

[[nodiscard]] NodeTypeDescriptor ProjectedDescriptor() {
    return NodeTypeDescriptor{
        .type = TypeId{"projection.configurable"},
        .display_name = "Configurable",
        .category = "Tests",
        .pin_schema = NodePinSchema{{"mode"}, [](const PropertyBag& properties) -> Result<std::vector<PinDescriptor>> {
                const std::string mode = Mode(properties);
                if (mode == "invalid") {
                    return std::vector<PinDescriptor>{SourcePin("Duplicate A"), SourcePin("Duplicate B")};
                }
                if (mode == "failure") {
                    return std::unexpected(Error{ErrorCode::CommandFailed, "Requested projection failure"});
                }
                std::vector<PinDescriptor> pins;
                if (mode == "source_sink" || mode == "source")
                    pins.push_back(SourcePin(mode == "source" ? "Source output" : "Source/sink output"));
                if (mode == "source_sink" || mode == "sink")
                    pins.push_back(SinkPin());
                return pins;
            }},
        .default_properties = {{"mode", std::string{"source_sink"}}},
        .property_impacts = {{"mode", PropertyImpact::RuntimeOnly}},
    };
}

[[nodiscard]] NodeTypeDescriptor EmitterDescriptor() {
    return NodeTypeDescriptor{
        .type = TypeId{"projection.emitter"},
        .display_name = "Emitter",
        .pin_schema = {PinDescriptor{
            .key = "value",
            .label = "Value",
            .type = TypeId{"float"},
            .direction = PinDirection::Output,
            .cardinality = PinCardinality::Multiple,
        }},
    };
}

void TestRegistrationAndConfiguredCreation() {
    RegistryCatalog registry;
    Expect(registry.RegisterNodeType(ProjectedDescriptor()).has_value(), "Property-projected descriptor must register");

    GraphDocument document;
    auto creation = registry.Instantiate(document, TypeId{"projection.configurable"});
    Expect(creation && creation->pins.size() == 2 && creation->pins[0].key == "source" && creation->pins[1].key == "sink",
           "Instantiation must project default properties");
    auto configured = registry.Instantiate(
        document, TypeId{"projection.configurable"},
        {.property_overrides = {{"mode", std::string{"source"}}}});
    Expect(configured && configured->pins.size() == 1 &&
               configured->pins.front().key == "source" &&
               configured->pins.front().label == "Source output",
           "Instantiation options must resolve pins from property overrides atomically");

    NodeTypeDescriptor duplicate = ProjectedDescriptor();
    duplicate.type = TypeId{"projection.invalid-default"};
    duplicate.default_properties.insert_or_assign("mode", std::string{"invalid"});
    auto invalid = registry.RegisterNodeType(std::move(duplicate));
    Expect(!invalid && invalid.error().code == ErrorCode::DuplicateId, "Registry must reject duplicate default projection keys");

    const auto register_invalid_metadata = [&](std::string suffix, PinDescriptor pin) {
        NodeTypeDescriptor descriptor{
            .type = TypeId{"projection.invalid-" + suffix},
            .display_name = "Invalid projected pin",
        };
        descriptor.pin_schema = NodePinSchema{{}, [pin = std::move(pin)](const PropertyBag&) {
            return Result<std::vector<PinDescriptor>>{std::vector<PinDescriptor>{pin}};
        }};
        return registry.RegisterNodeType(std::move(descriptor));
    };
    Expect(!register_invalid_metadata("empty-key", PinDescriptor{.type = TypeId{"float"}}).has_value(), "Registry must reject an empty projected semantic key");
    Expect(!register_invalid_metadata("empty-type", PinDescriptor{.key = "pin"}).has_value(), "Registry must reject an empty projected pin type");
    Expect(
        !register_invalid_metadata("direction", PinDescriptor{.key = "pin", .type = TypeId{"float"}, .direction = static_cast<PinDirection>(0x7F)}).has_value(),
        "Registry must reject an invalid projected direction");
    Expect(!register_invalid_metadata("kind", PinDescriptor{.key = "pin", .type = TypeId{"float"}, .kind = static_cast<PinKind>(0x7F)}).has_value(),
           "Registry must reject an invalid projected kind");
    Expect(!register_invalid_metadata("cardinality", PinDescriptor{.key = "pin", .type = TypeId{"float"}, .cardinality = static_cast<PinCardinality>(0x7F)})
                .has_value(),
           "Registry must reject an invalid projected cardinality");

    auto resolution_calls = std::make_shared<int>(0);
    NodeTypeDescriptor cached{
        .type = TypeId{"projection.cached-default"},
        .display_name = "Cached default schema",
        .pin_schema = NodePinSchema{{}, [resolution_calls](const PropertyBag&) -> Result<std::vector<PinDescriptor>> {
            ++*resolution_calls;
            return std::vector<PinDescriptor>{SourcePin("Cached")};
        }},
    };
    Expect(registry.RegisterNodeType(std::move(cached)).has_value() && *resolution_calls == 1,
           "Registration must materialize a configurable default schema once");
    Expect(registry.DefaultPinSchema(TypeId{"projection.cached-default"}).size() == 1 &&
               registry.DefaultPinSchema(TypeId{"projection.cached-default"}).size() == 1 &&
               *resolution_calls == 1,
           "Default schema reads must not invoke application resolvers");
}

void TestTopologyProjectionLocalLinksAndHistory() {
    RegistryCatalog registry;
    Expect(registry.RegisterNodeType(ProjectedDescriptor()).has_value() && registry.RegisterNodeType(EmitterDescriptor()).has_value(),
           "Projection topology fixtures must register");

    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    auto configurable = registry.Instantiate(document, TypeId{"projection.configurable"});
    auto emitter = registry.Instantiate(document, TypeId{"projection.emitter"});
    Expect(configurable && emitter, "Projection topology nodes must instantiate");
    const NodeId configurable_id = configurable->node.id;
    const NodeId emitter_id = emitter->node.id;
    const PinId original_source = configurable->pins[0].id;
    const PinId original_sink = configurable->pins[1].id;
    const PinId emitter_output = emitter->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*configurable)), document, presentation, registry, "Configurable node must be added");
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*emitter)), document, presentation, registry, "Emitter node must be added");
    const PinInstance dynamic_pin{
        .id = document.AllocatePinId(),
        .node = configurable_id,
        .key = "instance",
        .label = "Instance pin",
        .type = TypeId{"float"},
        .direction = PinDirection::Input,
        .storage = PinStorage::Dynamic,
    };
    Execute(commands, std::make_unique<AddDynamicPinCommand>(graph, dynamic_pin, 1), document,
            presentation, registry, "Dynamic projection fixture pin must be added");

    const LinkId link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = emitter_output, .input = original_sink}), document, presentation,
            registry, "Sink fixture link must connect");
    const RoutePointId route_point = presentation.AllocateRoutePointId();
    const LinkPresentation link_presentation{LinkStyle{.router = TypeId{"projection.router"}, .color = 0xAABBCCDDU},
                                             PersistentRoutePointSequence{{RoutePoint{.id = route_point, .position = {42.0f, 24.0f}}}}};
    Execute(commands, std::make_unique<SetLinkPresentationCommand>(link, link_presentation), document, presentation, registry,
            "Sink fixture route must be stored");
    commands.Clear();

    Execute(commands, std::make_unique<SetNodePropertyCommand>(graph, configurable_id, "mode", std::string{"source"}), document, presentation, registry,
            "Topology property projection must execute");
    const auto* retained = FindPin(document, graph, configurable_id, "source");
    Expect(retained != nullptr && retained->id == original_source && retained->label == "Source output" &&
               FindPin(document, graph, configurable_id, "sink") == nullptr && document.FindLink(graph, link) == nullptr &&
               presentation.FindLink(link) == nullptr && !presentation.RoutePointOwner(route_point) &&
               *FindPin(document, graph, configurable_id, "instance") == dynamic_pin &&
               document.FindNode(graph, configurable_id)->pins == std::vector<PinId>{original_source, dynamic_pin.id},
           "Projection must retain semantic IDs and dynamic pins while removing sink links and presentation state");

    Expect(commands.Undo(document, presentation, registry).has_value(), "Topology projection undo must succeed");
    const auto* restored_sink = FindPin(document, graph, configurable_id, "sink");
    Expect(restored_sink != nullptr && restored_sink->id == original_sink && FindPin(document, graph, configurable_id, "source")->id == original_source &&
               std::get<std::string>(document.FindNode(graph, configurable_id)->properties.at("mode")) == "source_sink" &&
               *FindPin(document, graph, configurable_id, "instance") == dynamic_pin &&
               document.FindNode(graph, configurable_id)->pins ==
                   std::vector<PinId>{original_source, dynamic_pin.id, original_sink} &&
               *document.FindLink(graph, link) == Link{.id = link, .output = emitter_output, .input = original_sink} &&
               presentation.FindLink(link) != nullptr && *presentation.FindLink(link) == link_presentation && presentation.RoutePointOwner(route_point) == link,
           "One undo must restore exact pins, local link, presentation, and route IDs");

    Expect(commands.Redo(document, presentation, registry).has_value() && FindPin(document, graph, configurable_id, "sink") == nullptr &&
               document.FindLink(graph, link) == nullptr &&
               std::get<std::string>(document.FindNode(graph, configurable_id)->properties.at("mode")) == "source" &&
               *FindPin(document, graph, configurable_id, "instance") == dynamic_pin,
           "Topology projection redo must remove the same topology again");
    Execute(commands, std::make_unique<SetNodePropertyCommand>(graph, configurable_id, "mode", std::string{"source_sink"}), document, presentation, registry,
            "Re-adding projected sink must succeed");
    const auto* new_sink = FindPin(document, graph, configurable_id, "sink");
    Expect(new_sink != nullptr && new_sink->id != original_sink && document.IncidentLinks(new_sink->id).empty(),
           "A newly projected semantic pin must not inherit a dormant connection");
    const PinId new_sink_id = new_sink->id;

    const std::uint64_t revision_before_invalid = document.ModelRevision();
    const std::uint64_t presentation_revision_before_invalid = presentation.PresentationRevision();
    const std::vector<PinId> pins_before_invalid = document.FindNode(graph, configurable_id)->pins;
    const PropertyBag properties_before_invalid = document.FindNode(graph, configurable_id)->properties;
    auto invalid =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(graph, configurable_id, "mode", std::string{"invalid"}), document, presentation, registry);
    Expect(!invalid && invalid.error().code == ErrorCode::DuplicateId && document.ModelRevision() == revision_before_invalid &&
               presentation.PresentationRevision() == presentation_revision_before_invalid &&
               document.FindNode(graph, configurable_id)->pins == pins_before_invalid &&
               document.FindNode(graph, configurable_id)->properties == properties_before_invalid,
           "Invalid runtime projection must leave the document unchanged");

    auto failure =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(graph, configurable_id, "mode", std::string{"failure"}), document, presentation, registry);
    Expect(!failure && failure.error().code == ErrorCode::CommandFailed && document.ModelRevision() == revision_before_invalid &&
               presentation.PresentationRevision() == presentation_revision_before_invalid &&
               document.FindNode(graph, configurable_id)->pins == pins_before_invalid &&
               document.FindNode(graph, configurable_id)->properties == properties_before_invalid,
           "Projection callback failure must leave the document unchanged");

    const LinkId protected_link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = protected_link, .output = emitter_output, .input = new_sink_id}), document,
            presentation, registry, "Protected projection fixture link must connect");
    Execute(commands, std::make_unique<SetLinkPresentationCommand>(protected_link, LinkPresentation{LinkStyle{.router = TypeId{"projection.router"}}, {}}),
             document, presentation, registry, "Protected projection fixture presentation must be stored");
    const auto rejected = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(
            graph, configurable_id, "mode", std::string{"source"},
            InvalidConnectionPolicy::Reject),
        document, presentation, registry);
    Expect(!rejected && rejected.error().code == ErrorCode::IncompatiblePins &&
               document.FindLink(graph, protected_link) != nullptr &&
               FindPin(document, graph, configurable_id, "sink") != nullptr,
           "Reject policy must preserve connections and schema atomically");
    Execute(commands, std::make_unique<SetLinkLockedCommand>(protected_link, true), document, presentation, registry,
            "Protected projection fixture presentation must be locked");
    const std::uint64_t revision_before_locked = document.ModelRevision();
    const std::uint64_t presentation_revision_before_locked = presentation.PresentationRevision();
    const PropertyBag properties_before_locked = document.FindNode(graph, configurable_id)->properties;
    const LinkPresentation presentation_before_locked = *presentation.FindLink(protected_link);
    auto locked_removal =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(graph, configurable_id, "mode", std::string{"source"}), document, presentation, registry);
    Expect(!locked_removal && locked_removal.error().code == ErrorCode::Locked && document.ModelRevision() == revision_before_locked &&
               document.FindLink(graph, protected_link) != nullptr && presentation.PresentationRevision() == presentation_revision_before_locked &&
               document.FindNode(graph, configurable_id)->properties == properties_before_locked && presentation.FindLink(protected_link) != nullptr &&
               *presentation.FindLink(protected_link) == presentation_before_locked && FindPin(document, graph, configurable_id, "sink"),
           "Locked incident presentation must make projection removal fail atomically");
    Execute(commands, std::make_unique<SetLinkLockedCommand>(protected_link, false), document, presentation, registry,
            "Protected projection fixture presentation must be unlocked");

    Execute(commands, std::make_unique<SetPinReadOnlyCommand>(graph, new_sink_id, true), document, presentation, registry,
            "Projected sink must become read-only");
    const std::uint64_t revision_before_protected = document.ModelRevision();
    const std::uint64_t presentation_revision_before_protected = presentation.PresentationRevision();
    const PropertyBag properties_before_protected = document.FindNode(graph, configurable_id)->properties;
    const LinkPresentation presentation_before_protected = *presentation.FindLink(protected_link);
    auto protected_removal =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(graph, configurable_id, "mode", std::string{"source"}), document, presentation, registry);
    Expect(!protected_removal && protected_removal.error().code == ErrorCode::ReadOnly && document.ModelRevision() == revision_before_protected &&
               presentation.PresentationRevision() == presentation_revision_before_protected &&
               document.FindNode(graph, configurable_id)->properties == properties_before_protected &&
               FindPin(document, graph, configurable_id, "sink") != nullptr && document.FindLink(graph, protected_link) != nullptr &&
               presentation.FindLink(protected_link) != nullptr && *presentation.FindLink(protected_link) == presentation_before_protected,
           "Read-only descriptor pin removal must fail atomically");
    Expect(ValidateGraph(document, graph, registry).empty(), "Property-projected graph must pass descriptor-aware validation");
    (void)emitter_id;
}

void TestTransactionMutationBoundary() {
    RegistryCatalog registry;
    Expect(registry.RegisterNodeType(ProjectedDescriptor()).has_value(),
           "Transaction schema fixture must register");
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    auto creation = registry.Instantiate(document, TypeId{"projection.configurable"});
    Expect(creation.has_value(), "Transaction schema fixture must instantiate");
    const NodeId node = creation->node.id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*creation)), document,
            presentation, registry, "Transaction schema fixture must be added");
    Execute(commands,
            std::make_unique<TransactionPropertyCommand>(
                graph, node, "mode", PropertyValue{std::string{"source"}}),
            document, presentation, registry,
            "Public transaction property mutation must resolve dependent schema");
    Expect(FindPin(document, graph, node, "source") != nullptr &&
               FindPin(document, graph, node, "sink") == nullptr &&
               ValidateGraph(document, graph, registry).empty(),
           "Transaction mutation boundary must preserve descriptor invariants");
}

void TestHistoryRejectsSchemaReplacement() {
    RegistryCatalog registry;
    Expect(registry.RegisterNodeType(ProjectedDescriptor()).has_value(),
           "History schema fixture must register");
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    auto creation = registry.Instantiate(document, TypeId{"projection.configurable"});
    Expect(creation.has_value(), "History schema fixture must instantiate");
    const NodeId node = creation->node.id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*creation)), document,
            presentation, registry, "History schema fixture must be added");
    Execute(commands,
            std::make_unique<SetNodePropertyCommand>(
                graph, node, "mode", PropertyValue{std::string{"source"}}),
            document, presentation, registry, "History schema change must execute");
    Expect(commands.Undo(document, presentation, registry).has_value(),
           "History schema change must undo");

    NodeTypeDescriptor replacement = ProjectedDescriptor();
    replacement.display_name = "Replaced configurable";
    Expect(registry.ReplaceNodeType(std::move(replacement)).has_value(),
           "History schema descriptor must be replaceable after undo");
    const auto redo = commands.Redo(document, presentation, registry);
    Expect(!redo && redo.error().code == ErrorCode::RevisionConflict &&
               FindPin(document, graph, node, "source") != nullptr &&
               FindPin(document, graph, node, "sink") != nullptr,
           "Redo must fail closed after descriptor identity changes");
}

[[nodiscard]] NodeTypeDescriptor IntergraphProjectionDescriptor() {
    return NodeTypeDescriptor{
        .type = TypeId{"projection.intergraph-output"},
        .display_name = "Projected intergraph output",
        .pin_schema = NodePinSchema{{"enabled"}, [](const PropertyBag& properties) -> Result<std::vector<PinDescriptor>> {
                const auto found = properties.find("enabled");
                const bool enabled = found != properties.end() && std::get<bool>(found->second);
                if (!enabled)
                    return std::vector<PinDescriptor>{};
                return std::vector<PinDescriptor>{PinDescriptor{
                    .key = "channel",
                    .label = "Channel",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Input,
                }};
            }},
        .default_properties = {{"enabled", true}},
        .property_impacts = {{"enabled", PropertyImpact::Rendering}},
    };
}

void TestIntergraphProjectionCleanup() {
    RegistryCatalog registry;
    Expect(registry.RegisterNodeType(IntergraphProjectionDescriptor()).has_value(), "Intergraph projection descriptor must register");
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    const GraphId left = document.RootGraph();
    const GraphId right = document.AllocateGraphId();
    Execute(commands, std::make_unique<AddGraphCommand>(right), document, presentation, registry, "Intergraph destination graph must be added");

    auto sender = registry.Instantiate(document, TypeId{"projection.intergraph-output"});
    Expect(sender && sender->pins.size() == 1, "Projected intergraph sender must instantiate");
    sender->node.role = NodeRole::IntergraphOutput;
    const NodeId sender_id = sender->node.id;
    const PinId sender_pin = sender->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(left, std::move(*sender)), document, presentation, registry,
            "Projected intergraph sender must be added");

    const NodeId receiver_id = document.AllocateNodeId();
    const PinId receiver_pin = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                right,
                NodeCreation{
                    .node = NodeInstance{.id = receiver_id, .type = TypeId{"projection.intergraph-input"}, .role = NodeRole::IntergraphInput},
                    .pins = {PinInstance{
                        .id = receiver_pin,
                        .node = receiver_id,
                        .key = "channel",
                        .label = "Channel",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                        .storage = PinStorage::Dynamic,
                    }},
                }),
            document, presentation, registry, "Intergraph receiver must be added");
    const IntergraphLinkId intergraph = document.AllocateIntergraphLinkId();
    const IntergraphLink expected{
        .id = intergraph,
        .source = {left, sender_id, sender_pin},
        .destination = {right, receiver_id, receiver_pin},
    };
    Execute(commands, std::make_unique<ConnectIntergraphCommand>(expected), document, presentation, registry, "Projected intergraph pin must connect");
    commands.Clear();

    Execute(commands, std::make_unique<SetNodePropertyCommand>(left, sender_id, "enabled", false), document, presentation, registry,
            "Intergraph endpoint projection removal must execute");
    Expect(document.FindPin(left, sender_pin) == nullptr && document.FindIntergraphLink(intergraph) == nullptr,
           "Removing a projected endpoint must remove its intergraph link");
    Expect(commands.Undo(document, presentation, registry).has_value() && document.FindPin(left, sender_pin) != nullptr &&
               *document.FindIntergraphLink(intergraph) == expected,
           "Intergraph projection undo must restore the exact endpoint and link IDs");
    Expect(commands.Redo(document, presentation, registry).has_value() && document.FindPin(left, sender_pin) == nullptr &&
               document.FindIntergraphLink(intergraph) == nullptr,
           "Intergraph projection redo must remove the endpoint link again");
}

} // namespace

int main() {
    TestRegistrationAndConfiguredCreation();
    TestTopologyProjectionLocalLinksAndHistory();
    TestTransactionMutationBoundary();
    TestHistoryRejectsSchemaReplacement();
    TestIntergraphProjectionCleanup();
    return EXIT_SUCCESS;
}
