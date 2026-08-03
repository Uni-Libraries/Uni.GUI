#include <uni/gui/nodes/nodes.h>

#include "ui_nodes_internal.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using namespace Uni::GUI::Nodes;

template<typename Map>
concept HasMutableAt = requires(Map& map, typename Map::key_type id) { map.MutableAt(id); };

template<typename Map>
concept HasFindMutable = requires(Map& map, typename Map::key_type id) { map.FindMutable(id); };

template<typename Registry>
concept HasInvocationSnapshot = requires(const Registry& registry) { registry.InvocationSnapshot(); };

static_assert(!HasMutableAt<NodeMap> && !HasMutableAt<PinMap> && !HasMutableAt<LinkMap>);
static_assert(!HasFindMutable<NodeMap> && !HasFindMutable<PinMap> && !HasFindMutable<LinkMap>);
static_assert(std::is_same_v<decltype(std::declval<NodeMap&>().at(NodeId{})), const NodeInstance&>);
static_assert(!std::is_move_constructible_v<CommandStack> && !std::is_move_assignable_v<CommandStack>);
static_assert(std::is_copy_constructible_v<RegistrySnapshot> && std::is_copy_assignable_v<RegistrySnapshot>);
static_assert(std::is_nothrow_copy_constructible_v<ConversionRecipe>);
static_assert(std::is_nothrow_copy_constructible_v<PersistentRoutePointSequence>);
static_assert(std::ranges::forward_range<PersistentRoutePointSequence>);
static_assert(!HasInvocationSnapshot<RegistryCatalog>);
static_assert(std::is_same_v<decltype(std::declval<NodeEditorWorkspace&>().Registry()), const RegistryCatalog&>);

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

NodeTypeDescriptor SourceDescriptor() {
    return NodeTypeDescriptor{
        .type = TypeId{"test.source"},
        .display_name = "Source",
        .category = "Tests",
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
    };
}

NodeTypeDescriptor SinkDescriptor() {
    return NodeTypeDescriptor{
        .type = TypeId{"test.sink"},
        .display_name = "Sink",
        .category = "Tests",
        .version = 2,
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

CommandResult Execute(CommandStack& stack,
                      std::unique_ptr<Command> command,
                      GraphDocument& document,
                      GraphPresentation& presentation,
                      const RegistryCatalog& registry,
                      const char* message) {
    auto result = stack.Execute(std::move(command), document, presentation, registry);
    Expect(result.has_value(), message);
    return *result;
}

class ModelOnlyNodeCommand final : public Command {
  public:
    ModelOnlyNodeCommand(GraphId graph, NodeInstance node) : m_graph(graph), m_node(std::move(node)) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Model-only node";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        return transaction.AddNode(m_graph, m_node, {});
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        auto removed = transaction.RemoveNode(m_graph, m_node.id);
        return removed ? Result<void>{} : std::unexpected(std::move(removed.error()));
    }

    GraphId m_graph;
    NodeInstance m_node;
};

class AllocateDuringApplyCommand final : public Command {
  public:
    AllocateDuringApplyCommand(GraphDocument& document, LinkId& allocated, GraphId graph, NodeId node)
        : m_document(document), m_allocated(allocated), m_graph(graph), m_node(node) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Allocate during apply";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        m_allocated = m_document.AllocateLinkId();
        return transaction.SetNodeProperty(m_graph, m_node, "allocation", PropertyValue{std::string{"invalid"}});
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        return transaction.SetNodeProperty(m_graph, m_node, "allocation", std::nullopt);
    }

    GraphDocument& m_document;
    LinkId& m_allocated;
    GraphId m_graph;
    NodeId m_node;
};

class UnlockEditRelockCommand final : public Command {
  public:
    UnlockEditRelockCommand(GraphId graph, NodeId node) : m_graph(graph), m_node(node) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Unlock edit relock";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        if (auto unlocked = transaction.SetNodeReadOnly(m_graph, m_node, false); !unlocked) return unlocked;
        if (auto edited = transaction.SetNodeProperty(m_graph, m_node, "bypass", PropertyValue{std::int64_t{1}});
            !edited)
            return edited;
        return transaction.SetNodeReadOnly(m_graph, m_node, true);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction&) override {
        return {};
    }

    GraphId m_graph;
    NodeId m_node;
};

class RemoveGraphThenFailCommand final : public Command {
  public:
    explicit RemoveGraphThenFailCommand(GraphId graph) : m_graph(graph) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Remove graph then fail";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        if (auto removed = transaction.RemoveGraph(m_graph); !removed) {
            return std::unexpected(std::move(removed.error()));
        }
        return std::unexpected(Error{ErrorCode::CommandFailed, "Intentional rollback"});
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction&) override {
        return {};
    }

    GraphId m_graph;
};

class DirectRemoveGraphsCommand final : public Command {
  public:
    explicit DirectRemoveGraphsCommand(std::vector<GraphId> graphs) : m_graphs(std::move(graphs)) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Direct graph removal";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        m_removed.clear();
        for (const GraphId graph : m_graphs) {
            auto removed = transaction.RemoveGraph(graph);
            if (!removed) return std::unexpected(std::move(removed.error()));
            m_removed.push_back(std::move(*removed));
        }
        return {};
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        for (auto graph = m_removed.rbegin(); graph != m_removed.rend(); ++graph) {
            if (auto restored = transaction.RestoreGraph(*graph); !restored) return restored;
        }
        return {};
    }

    std::vector<GraphId> m_graphs;
    std::vector<Graph> m_removed;
};

class CustomPropertyMutationCommand final : public Command {
  public:
    CustomPropertyMutationCommand(GraphId graph, NodeId node, std::string key, PropertyValue value)
        : m_graph(graph), m_node(node), m_key(std::move(key)), m_value(std::move(value)) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Custom mutation";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        return transaction.SetNodeProperty(m_graph, m_node, m_key, m_value);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        return transaction.SetNodeProperty(m_graph, m_node, m_key, std::nullopt);
    }

    GraphId m_graph;
    NodeId m_node;
    std::string m_key;
    PropertyValue m_value;
};

class BudgetProbeCommand final : public Command {
  public:
    BudgetProbeCommand(GraphId graph, NodeId node, std::size_t& attempts)
        : m_graph(graph), m_node(node), m_attempts(attempts) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Budget probe";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        for (std::size_t index = 0; index < 1'000'000; ++index) {
            ++m_attempts;
            auto changed = transaction.SetNodeProperty(m_graph, m_node, "budget-" + std::to_string(index),
                                                       PropertyValue{static_cast<std::int64_t>(index)});
            if (!changed) return changed;
        }
        return {};
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction&) override {
        return {};
    }

    GraphId m_graph;
    NodeId m_node;
    std::size_t& m_attempts;
};

class BulkGraphsCommand final : public Command {
  public:
    explicit BulkGraphsCommand(std::vector<Graph> graphs) : m_graphs(std::move(graphs)) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Bulk graphs";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        for (const Graph& graph : m_graphs) {
            if (auto added = transaction.AddGraph(graph); !added) {
                return std::unexpected(std::move(added.error()));
            }
        }
        return {};
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        for (auto graph = m_graphs.rbegin(); graph != m_graphs.rend(); ++graph) {
            if (auto removed = transaction.RemoveGraph(graph->id); !removed) {
                return std::unexpected(std::move(removed.error()));
            }
        }
        return {};
    }

    std::vector<Graph> m_graphs;
};

class RegistryMutationCommand final : public Command {
  public:
    RegistryMutationCommand(RegistryCatalog& registry,
                            ConversionDescriptor conversion,
                            std::vector<ErrorCode>& errors,
                            std::vector<std::uint64_t>& revisions)
        : m_registry(registry), m_conversion(std::move(conversion)), m_errors(errors), m_revisions(revisions) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Type registry mutation";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot& snapshot) override {
        m_revisions.push_back(snapshot.ConversionRevision());
        auto registered = m_registry.RegisterConversion(m_conversion);
        if (!registered) m_errors.push_back(registered.error().code);
        return transaction.SetSchemaVersion(2);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        return transaction.SetSchemaVersion(1);
    }

    RegistryCatalog& m_registry;
    ConversionDescriptor m_conversion;
    std::vector<ErrorCode>& m_errors;
    std::vector<std::uint64_t>& m_revisions;
};

class RegistryCallbackCommand final : public Command {
  public:
    using Callback = std::function<void()>;

    RegistryCallbackCommand(std::uint32_t version, Callback apply, Callback revert = {})
        : m_version(version), m_apply(std::move(apply)), m_revert(std::move(revert)) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Registry callback";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot&) override {
        if (m_apply) m_apply();
        return transaction.SetSchemaVersion(m_version);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        if (m_revert) m_revert();
        return transaction.SetSchemaVersion(m_version - 1);
    }

    std::uint32_t m_version;
    Callback m_apply;
    Callback m_revert;
};

class RegistryDependencyCommand final : public Command {
  public:
    explicit RegistryDependencyCommand(TypeId node_type, const std::uint32_t version = 2)
        : m_node_type(std::move(node_type)), m_version(version) {}

    explicit RegistryDependencyCommand(ConversionKey conversion, const std::uint32_t version = 2)
        : m_conversion(std::move(conversion)), m_version(version) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Registry dependency";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) override {
        if (m_node_type) (void)registry.Find(*m_node_type);
        if (m_conversion) {
            (void)registry.Check(m_conversion->source_type, m_conversion->destination_type, m_conversion->kind);
        }
        return transaction.SetSchemaVersion(m_version);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        if (m_node_type) (void)transaction.Registry().Find(*m_node_type);
        return transaction.SetSchemaVersion(m_version - 1);
    }

    std::optional<TypeId> m_node_type;
    std::optional<ConversionKey> m_conversion;
    std::uint32_t m_version;
};

class RegistryRevisionDependencyCommand final : public Command {
  public:
    enum class Domain { Nodes, Conversions, Generation };

    RegistryRevisionDependencyCommand(const Domain domain, const std::uint32_t version)
        : m_domain(domain), m_version(version) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Registry revision dependency";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) override {
        switch (m_domain) {
        case Domain::Nodes:
            (void)registry.NodeRevision();
            break;
        case Domain::Conversions:
            (void)registry.ConversionRevision();
            break;
        case Domain::Generation:
            (void)registry.Generation();
            break;
        }
        return transaction.SetSchemaVersion(m_version);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        return transaction.SetSchemaVersion(m_version - 1);
    }

    Domain m_domain;
    std::uint32_t m_version;
};

class EscapedRegistrySnapshotCommand final : public Command {
  public:
    EscapedRegistrySnapshotCommand(std::optional<RegistrySnapshot>& escaped, const std::uint32_t version)
        : m_escaped(escaped), m_version(version) {}

    [[nodiscard]] std::string_view Name() const noexcept override {
        return "Escaped registry snapshot";
    }

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) override {
        m_escaped = registry;
        return transaction.SetSchemaVersion(m_version);
    }

    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override {
        return transaction.SetSchemaVersion(m_version - 1);
    }

    std::optional<RegistrySnapshot>& m_escaped;
    std::uint32_t m_version;
};

struct ThrowingAdjacencyValue final {
    int value{0};
    static inline bool throw_on_copy{false};

    ThrowingAdjacencyValue() = default;
    explicit ThrowingAdjacencyValue(const int value) : value(value) {}
    ThrowingAdjacencyValue(const ThrowingAdjacencyValue& other) : value(other.value) {
        if (throw_on_copy) throw std::runtime_error("adjacency value copy failed");
    }
    ThrowingAdjacencyValue& operator=(const ThrowingAdjacencyValue&) = default;
    ThrowingAdjacencyValue(ThrowingAdjacencyValue&&) noexcept = default;
    ThrowingAdjacencyValue& operator=(ThrowingAdjacencyValue&&) noexcept = default;
    bool operator==(const ThrowingAdjacencyValue&) const = default;
};

static_assert(std::is_same_v<decltype(std::declval<GraphDocument&>().FindGraph(GraphId{})), const Graph*>);
static_assert(
    std::is_same_v<decltype(std::declval<GraphDocument&>().FindNode(GraphId{}, NodeId{})), const NodeInstance*>);
static_assert(std::is_same_v<decltype(std::declval<GraphDocument&>().FindPin(GraphId{}, PinId{})), const PinInstance*>);

void TestCoreCommandsAndRevisions() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    RegistryCatalog& types = registry;
    CommandStack commands;
    Expect(document.ModelRevision() == 0 && presentation.PresentationRevision() == 0,
           "Fresh state must start at revision zero");
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value(), "Source descriptor must register");
    Expect(registry.RegisterNodeType(SinkDescriptor()).has_value(), "Sink descriptor must register");

    auto source = registry.Instantiate(document, TypeId{"test.source"});
    auto sink = registry.Instantiate(document, TypeId{"test.sink"});
    Expect(source && sink, "Registered nodes must instantiate");
    Expect(document.ModelRevision() == 0, "ID reservation must not count as a persisted model change");

    const GraphId graph = document.RootGraph();
    const NodeId source_id = source->node.id;
    const NodeId sink_id = sink->node.id;
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    auto added_source = Execute(
        commands,
        std::make_unique<AddNodeCommand>(graph, std::move(*source), NodePresentation{.position = {10.0f, 20.0f}}),
        document, presentation, types, "Source add must execute");
    Expect(added_source.model_changed && added_source.presentation_changed,
           "Add node must report both changed domains");
    Expect(added_source.revisions == Revisions{1, 1}, "Add node must increment both revisions exactly once");
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*sink), NodePresentation{.position = {300.0f, 20.0f}}),
            document, presentation, types, "Sink add must execute");

    const auto static_revision = document.ModelRevision();
    auto remove_static =
        commands.Execute(std::make_unique<RemoveDynamicPinCommand>(graph, output), document, presentation, types);
    Expect(!remove_static && remove_static.error().code == ErrorCode::InvalidArgument &&
               document.ModelRevision() == static_revision,
           "Dynamic pin commands must not remove descriptor-owned static pins");

    const LinkId link = document.AllocateLinkId();
    auto connected = Execute(
        commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = input}),
        document, presentation, types, "Connect must execute");
    Expect(connected.model_changed && !connected.presentation_changed, "A link without route state must be model-only");
    Expect(ValidateConnection(document, presentation, ConnectionRequest{graph, output, input}, types).status ==
               ConnectionResult::Status::Rejected,
           "Duplicate connection must be rejected");

    auto property = Execute(
        commands, std::make_unique<SetNodePropertyCommand>(graph, source_id, "value", PropertyValue{std::string{"42"}}),
        document, presentation, types, "Property command must execute");
    Expect(property.model_changed && !property.presentation_changed, "Property command must be model-only");
    Expect(std::get<std::string>(document.FindNode(graph, source_id)->properties.at("value")) == "42",
           "Property command must update the node");
    Expect(commands.Undo(document, presentation, types).has_value(), "Property undo must execute");
    Expect(!document.FindNode(graph, source_id)->properties.contains("value"), "Property undo must restore absence");
    Expect(commands.Redo(document, presentation, types).has_value(), "Property redo must execute");

    auto non_finite = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "invalid_double",
                                                 PropertyValue{std::numeric_limits<double>::quiet_NaN()}),
        document, presentation, types);
    Expect(!non_finite && non_finite.error().code == ErrorCode::InvalidArgument &&
               !document.FindNode(graph, source_id)->properties.contains("invalid_double"),
           "Typed properties must reject non-finite floating-point values");

    MoveNodesCommand::Positions before{{source_id, {10.0f, 20.0f}}};
    MoveNodesCommand::Positions after{{source_id, {80.0f, 90.0f}}};
    auto moved = Execute(commands, std::make_unique<MoveNodesCommand>(graph, before, after), document, presentation,
                         types, "Move command must execute");
    Expect(!moved.model_changed && moved.presentation_changed, "Move command must be presentation-only");
    Expect(presentation.FindNode(source_id)->position == Vec2{80.0f, 90.0f}, "Move command must update presentation");
    Expect(commands.Undo(document, presentation, types).has_value(), "Move undo must execute");
    Expect(presentation.FindNode(source_id)->position == Vec2{10.0f, 20.0f}, "Move undo must restore position");

    Execute(commands, std::make_unique<SetNodeDisplayNameCommand>(graph, sink_id, "Renamed sink"), document,
            presentation, types, "Display-name command must execute");
    Expect(document.FindNode(graph, sink_id)->display_name == "Renamed sink",
           "Display-name command must update semantic state");

    Execute(commands, std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{source_id}), document,
            presentation, types, "Delete command must execute");
    Expect(document.FindNode(graph, source_id) == nullptr && document.FindLink(graph, link) == nullptr,
           "Deleting a node must remove incident links");
    Expect(commands.Undo(document, presentation, types).has_value(), "Delete undo must execute");
    Expect(document.FindNode(graph, source_id) != nullptr && document.FindLink(graph, link) != nullptr,
           "Delete undo must restore node and links atomically");
    Expect(ValidateGraph(document, graph, registry).empty(), "Restored graph must validate");
}

void TestGraphAndPinCommands() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands;
    const GraphId root = document.RootGraph();
    const GraphId child = document.AllocateGraphId();

    auto graph_added = Execute(commands, std::make_unique<AddGraphCommand>(child), document, presentation, types,
                               "Child graph must be added");
    Expect(graph_added.model_changed && !graph_added.presentation_changed, "Graph add must be model-only");
    Execute(commands, std::make_unique<SetRootGraphCommand>(child), document, presentation, types,
            "Root graph command must execute");
    Expect(document.RootGraph() == child, "Root graph must change");
    Expect(commands.Undo(document, presentation, types).has_value(), "Root graph undo must execute");
    Expect(document.RootGraph() == root, "Root graph undo must restore root");

    const NodeId node = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(child,
                                             NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"dynamic"}}}),
            document, presentation, types, "Dynamic node must be added");
    const PinId pin = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddDynamicPinCommand>(child,
                                                   PinInstance{
                                                       .id = pin,
                                                       .node = node,
                                                       .key = "dynamic",
                                                       .label = "Dynamic",
                                                       .type = TypeId{"float"},
                                                       .storage = PinStorage::Dynamic,
                                                   },
                                                   0),
            document, presentation, types, "Dynamic pin must be added");
    Expect(document.FindPin(child, pin) != nullptr && document.FindNode(child, node)->pins == std::vector<PinId>{pin},
           "Pin command must maintain node/pin consistency");
    Execute(commands, std::make_unique<RemoveDynamicPinCommand>(child, pin), document, presentation, types,
            "Pin remove must execute");
    Expect(document.FindPin(child, pin) == nullptr, "Pin remove must update graph");
    Expect(commands.Undo(document, presentation, types).has_value(), "Pin remove undo must execute");
    Expect(document.FindPin(child, pin) != nullptr, "Pin remove undo must restore pin");

    PinInstance updated = *document.FindPin(child, pin);
    updated.label = "Renamed dynamic";
    Execute(commands, std::make_unique<UpdateDynamicPinCommand>(child, updated), document, presentation, types,
            "Dynamic pin update must execute");
    Expect(document.FindPin(child, pin)->label == "Renamed dynamic",
           "Dynamic pin update must replace mutable metadata");
    Expect(commands.Undo(document, presentation, types).has_value() && document.FindPin(child, pin)->label == "Dynamic",
           "Dynamic pin update undo must restore metadata");
    Expect(commands.Redo(document, presentation, types).has_value(), "Dynamic pin update redo must execute");

    const PinId second_pin = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddDynamicPinCommand>(child,
                                                   PinInstance{
                                                       .id = second_pin,
                                                       .node = node,
                                                       .key = "second",
                                                       .label = "Second",
                                                       .type = TypeId{"float"},
                                                       .direction = PinDirection::Output,
                                                       .storage = PinStorage::Dynamic,
                                                   },
                                                   1),
            document, presentation, types, "Second dynamic pin must be added");
    Execute(commands, std::make_unique<ReorderDynamicPinsCommand>(child, node, std::vector<PinId>{second_pin, pin}),
            document, presentation, types, "Pin reorder must execute");
    Expect(document.FindNode(child, node)->pins == std::vector<PinId>{second_pin, pin},
           "Pin reorder must persist the requested permutation");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindNode(child, node)->pins == std::vector<PinId>{pin, second_pin},
           "Pin reorder undo must restore the previous order");

    const LinkId dynamic_link = document.AllocateLinkId();
    Execute(commands,
            std::make_unique<ConnectPinsCommand>(child, Link{.id = dynamic_link, .output = second_pin, .input = pin}),
            document, presentation, types, "Dynamic pins must connect");
    Execute(commands,
            std::make_unique<SetLinkPresentationCommand>(
                dynamic_link,
                LinkPresentation{LinkStyle{.color = 0xFF112233U},
                                 PersistentRoutePointSequence{{presentation.AllocateRoutePointId(), {20.0f, 30.0f}}}}),
            document, presentation, types, "Dynamic link route must execute");
    PinInstance connected_update = *document.FindPin(child, pin);
    connected_update.label = "Connected dynamic";
    Execute(commands, std::make_unique<UpdateDynamicPinCommand>(child, connected_update), document, presentation, types,
            "Connected dynamic pin label update must execute");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindPin(child, pin)->label == "Renamed dynamic",
           "Connected dynamic pin label update must remain undoable");
    Execute(commands, std::make_unique<RemoveDynamicPinCommand>(child, pin), document, presentation, types,
            "Connected dynamic pin remove must execute");
    Expect(document.FindLink(child, dynamic_link) == nullptr && presentation.FindLink(dynamic_link) == nullptr,
           "Removing a dynamic pin must remove incident model and presentation "
           "links");
    Expect(commands.Undo(document, presentation, types).has_value() && document.FindPin(child, pin) != nullptr &&
               document.FindLink(child, dynamic_link) != nullptr && presentation.FindLink(dynamic_link) != nullptr &&
               presentation.FindLink(dynamic_link)->Route().size() == 1 &&
               presentation.FindLink(dynamic_link)->Route().front().position == Vec2{20.0f, 30.0f} &&
               presentation.FindLink(dynamic_link)->Style().color == std::optional<std::uint32_t>{0xFF112233U},
           "Dynamic pin remove undo must restore incident links and routes "
           "atomically");
    Expect(commands.Redo(document, presentation, types).has_value() && document.FindPin(child, pin) == nullptr &&
               document.FindLink(child, dynamic_link) == nullptr,
           "Dynamic pin remove redo must remove restored incident state again");
    Expect(commands.Undo(document, presentation, types).has_value(),
           "Dynamic pin remove must remain undoable after redo");

    auto dynamic_subgraph =
        commands.Execute(std::make_unique<SetNodeSubgraphCommand>(child, node,
                                                                  SubgraphReference{
                                                                      .ownership = SubgraphOwnership::Referenced,
                                                                      .target = DocumentGraphTarget{root},
                                                                  }),
                         document, presentation, types);
    Expect(!dynamic_subgraph && dynamic_subgraph.error().code == ErrorCode::InvalidArgument,
           "Subgraph binding must not take ownership of application dynamic pins");

    const NodeId subgraph_node = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                child, NodeCreation{.node = NodeInstance{.id = subgraph_node, .type = TypeId{"subgraph"}}}),
            document, presentation, types, "Subgraph node must be added");
    Execute(commands,
            std::make_unique<SetNodeSubgraphCommand>(child, subgraph_node,
                                                     SubgraphReference{
                                                         .ownership = SubgraphOwnership::Referenced,
                                                         .target = DocumentGraphTarget{root},
                                                     }),
            document, presentation, types, "Subgraph reference must execute");
    auto cycle =
        commands.Execute(std::make_unique<SetNodeSubgraphCommand>(child, subgraph_node,
                                                                  SubgraphReference{
                                                                      .ownership = SubgraphOwnership::Referenced,
                                                                      .target = DocumentGraphTarget{child},
                                                                  }),
                         document, presentation, types);
    Expect(!cycle && cycle.error().code == ErrorCode::InvalidGraph, "Subgraph command must reject cycles atomically");

    commands.Clear();
    Execute(commands, std::make_unique<RemoveGraphCommand>(child), document, presentation, types,
            "Graph remove must execute");
    Expect(document.FindGraph(child) == nullptr && presentation.FindNode(node) == nullptr,
           "Graph remove must clean model and associated presentation");
    Expect(commands.Undo(document, presentation, types).has_value(), "Graph remove undo must execute");
    Expect(document.FindGraph(child) != nullptr && document.FindPin(child, pin) != nullptr &&
               presentation.FindNode(node) != nullptr,
           "Graph remove undo must restore graph and presentation atomically");

    commands.Clear();
    Execute(commands, std::make_unique<SetSchemaVersionCommand>(7), document, presentation, types,
            "Schema command must execute");
    Expect(document.SchemaVersion() == 7, "Schema command must update schema version");
    Expect(commands.Undo(document, presentation, types).has_value() && document.SchemaVersion() == 1,
           "Schema undo must restore version");

    Graph malformed{.id = document.AllocateGraphId()};
    malformed.nodes.emplace(NodeId{}, NodeInstance{.id = {}, .type = TypeId{"invalid"}});
    const auto revision = document.ModelRevision();
    Expect(!commands.Execute(std::make_unique<AddGraphCommand>(std::move(malformed)), document, presentation, types),
           "Malformed graph restore must be rejected");
    Expect(document.ModelRevision() == revision, "Rejected graph restore must not change revision");
}

void TestPresentationCommands() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    RegistryCatalog& types = registry;
    CommandStack commands;
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value(), "Presentation source must register");
    Expect(registry.RegisterNodeType(SinkDescriptor()).has_value(), "Presentation sink must register");
    auto source = registry.Instantiate(document, TypeId{"test.source"});
    auto sink = registry.Instantiate(document, TypeId{"test.sink"});
    const GraphId graph = document.RootGraph();
    const NodeId source_id = source->node.id;
    const NodeId sink_id = sink->node.id;
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*source)), document, presentation, types,
            "Presentation source must be added");
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*sink)), document, presentation, types,
            "Presentation sink must be added");
    const LinkId link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = input}),
            document, presentation, types, "Presentation link must connect");

    commands.Clear();
    const auto model_revision = document.ModelRevision();
    Execute(commands, std::make_unique<ResizeNodeCommand>(source_id, Vec2{240.0f, 160.0f}), document, presentation,
            types, "Resize node must execute");
    Execute(commands, std::make_unique<SetNodeCollapsedCommand>(source_id, true), document, presentation, types,
            "Collapse node must execute");
    Execute(commands, std::make_unique<SetNodeZOrderCommand>(source_id, 42), document, presentation, types,
            "Z-order command must execute");
    Expect(presentation.FindNode(source_id)->size == Vec2{240.0f, 160.0f} &&
               presentation.FindNode(source_id)->collapsed && presentation.FindNode(source_id)->z_order == 42,
           "Node presentation commands must update their fields");
    Expect(document.ModelRevision() == model_revision, "Presentation commands must not modify model revision");

    Execute(commands,
            std::make_unique<SetLinkRoutePointsCommand>(link,
                                                        std::vector<RoutePoint>{
                                                            {presentation.AllocateRoutePointId(), {100.0f, 40.0f}},
                                                            {presentation.AllocateRoutePointId(), {180.0f, 80.0f}},
                                                        }),
            document, presentation, types, "Link route must execute");
    Expect(presentation.FindLink(link) && presentation.FindLink(link)->Route().size() == 2,
           "Link route command must create route presentation");
    Expect(commands.Undo(document, presentation, types).has_value(), "Link route undo must execute");
    Expect(presentation.FindLink(link) == nullptr, "Link route undo must restore absence of route state");
    Expect(commands.Redo(document, presentation, types).has_value(), "Link route redo must execute");

    const GroupId group = presentation.AllocateGroupId();
    Execute(commands,
            std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = group,
                .graph = graph,
                .geometry = GroupGeometry{.position = {20.0f, 30.0f}},
                .style = MakeGroupStyle(GroupStyle{.title = "Group"}),
            }),
            document, presentation, types, "Group add must execute");
    const std::uint64_t geometry_before_kind = presentation.GeometryRevision();
    Execute(commands,
            std::make_unique<SetGroupStyleCommand>(group, GroupStyle{.title = "Group", .kind = GroupKind::Comment}),
            document, presentation, types, "Group kind change must execute");
    Expect(presentation.GeometryRevision() == geometry_before_kind, "Group kind changes must remain render-only");
    Execute(commands, std::make_unique<MoveGroupCommand>(group, Vec2{60.0f, 70.0f}), document, presentation, types,
            "Group move must execute");
    Execute(commands, std::make_unique<ResizeGroupCommand>(group, Vec2{500.0f, 260.0f}), document, presentation, types,
            "Group resize must execute");
    Execute(commands, std::make_unique<SetGroupCollapsedCommand>(group, true), document, presentation, types,
            "Group collapse must execute");
    Expect(presentation.FindGroup(group)->geometry.position == Vec2{60.0f, 70.0f} &&
               presentation.FindGroup(group)->geometry.size == Vec2{500.0f, 260.0f} &&
               presentation.FindGroup(group)->geometry.collapsed,
           "Group commands must update persisted presentation");
    Execute(commands, std::make_unique<SetGroupMembersCommand>(group, GroupMemberSet{source_id}), document,
            presentation, types, "Initial group membership must execute");
    commands.Clear();
    Execute(commands,
            std::make_unique<ChangeGroupMembersCommand>(group, std::vector<NodeId>{source_id, sink_id},
                                                        std::vector<NodeId>{}),
            document, presentation, types, "Group membership delta must execute");
    Expect(presentation.FindGroup(group)->members.contains(source_id) &&
               presentation.FindGroup(group)->members.contains(sink_id),
           "Group membership delta must retain pre-existing additions");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               presentation.FindGroup(group)->members.contains(source_id) &&
               !presentation.FindGroup(group)->members.contains(sink_id),
           "Group membership delta undo must preserve pre-existing members");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               presentation.FindGroup(group)->members.contains(source_id) &&
               presentation.FindGroup(group)->members.contains(sink_id),
           "Group membership delta redo must restore only effective additions");
    Execute(commands, std::make_unique<RemoveGroupCommand>(group), document, presentation, types,
            "Group remove must execute");
    Expect(presentation.FindGroup(group) == nullptr, "Group remove must erase group");
    Expect(commands.Undo(document, presentation, types).has_value() && presentation.FindGroup(group),
           "Group remove undo must restore group");

    commands.Clear();
    CommandStack external;
    Execute(commands, std::make_unique<ResizeNodeCommand>(source_id, Vec2{320.0f, 180.0f}), document, presentation,
            types, "Presentation conflict fixture must execute");
    Execute(external, std::make_unique<SetNodeCollapsedCommand>(source_id, false), document, presentation, types,
            "External presentation command must execute");
    auto stale = commands.Undo(document, presentation, types);
    Expect(!stale && stale.error().code == ErrorCode::RevisionConflict,
           "Presentation revisions must reject stale undo from another stack");
}

void TestLargeGroupStyleMutationMetrics() {
    constexpr std::size_t LargeBodySize = 16U * 1024U * 1024U;
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    const NodeId node = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                graph, NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"large-group-style"}}}),
            document, presentation, types, "Large-style member node must execute");

    GroupPresentation defaults;
    Expect(defaults.style != nullptr, "Default group style handle must be non-null");
    const GroupStyleHandle original_style = MakeGroupStyle(GroupStyle{
        .title = "Large style",
        .body = std::string(LargeBodySize, 'a'),
    });
    GroupStyleHandle lifecycle_style;
    GraphPolicy lifecycle_policy;
    lifecycle_policy.evaluate_operation = [&](const OperationPolicyContext&, const OperationIntent& intent) {
        if (const auto* payload = intent.Get<GroupLifecycleOperation>()) {
            lifecycle_style = payload->value ? payload->value->style : nullptr;
        }
        return OperationPolicyDecision{AllowOperation{}};
    };
    const GroupId group = presentation.AllocateGroupId();
    auto added = commands.Execute(std::make_unique<AddGroupCommand>(GroupPresentation{
                                      .id = group,
                                      .graph = graph,
                                      .style = original_style,
                                      .members = {node},
                                  }),
                                  document, presentation, types, lifecycle_policy);
    Expect(added.has_value() && lifecycle_style == original_style,
           "Group lifecycle policy payload must share the immutable style "
           "generation");
    commands.Clear();

    GroupPresentation semantic_copy = *presentation.FindGroup(group);
    semantic_copy.style = MakeGroupStyle(*original_style);
    Expect(semantic_copy == *presentation.FindGroup(group) &&
               !semantic_copy.SharesStyleWith(*presentation.FindGroup(group)),
           "Group presentation equality must compare style values rather than "
           "pointer identity");

    const auto check_non_style = [&](const char* operation, std::unique_ptr<Command> command,
                                     const bool membership_delta) {
        commands.Clear();
        ResetTransactionMetrics();
        auto result = commands.Execute(std::move(command), document, presentation, types);
        Expect(result.has_value(), operation);
        const auto metrics = GetTransactionMetrics();
        Expect(presentation.FindGroup(group)->style == original_style,
               "Non-style group mutation replaced the immutable style handle");
        Expect(metrics.group_styles.logical_bytes == 0, "Non-style group mutation copied the large style payload");
        Expect(metrics.groups.logical_bytes < 64U * 1024U,
               "Non-style group mutation exceeded the outer shell copy bound");
        if (!membership_delta) {
            Expect(metrics.group_memberships.logical_bytes == 0 && metrics.presentation_indexes.logical_bytes == 0,
                   "Geometry or protection mutation copied memberships or indexes");
        }
    };

    check_non_style("Large-style move must execute", std::make_unique<MoveGroupCommand>(group, Vec2{10.0f, 20.0f}),
                    false);
    check_non_style("Large-style resize must execute",
                    std::make_unique<ResizeGroupCommand>(group, Vec2{400.0f, 240.0f}), false);
    check_non_style("Large-style collapse must execute", std::make_unique<SetGroupCollapsedCommand>(group, true),
                    false);
    check_non_style("Large-style z-order must execute", std::make_unique<SetGroupZOrderCommand>(group, 17), false);
    check_non_style("Large-style lock must execute", std::make_unique<SetGroupLockedCommand>(group, true), false);
    commands.Clear();
    Execute(commands, std::make_unique<SetGroupLockedCommand>(group, false), document, presentation, types,
            "Large-style unlock must execute");
    check_non_style(
        "Large-style member delta must execute",
        std::make_unique<ChangeGroupMembersCommand>(group, std::vector<NodeId>{}, std::vector<NodeId>{node}), true);

    commands.Clear();
    ResetTransactionMetrics();
    const GroupStyleHandle replacement_style = MakeGroupStyle(GroupStyle{
        .title = "Replacement style",
        .body = std::string(LargeBodySize, 'b'),
        .kind = GroupKind::Comment,
    });
    GroupStyleHandle policy_style;
    GraphPolicy style_policy;
    style_policy.evaluate_operation = [&](const OperationPolicyContext&, const OperationIntent& intent) {
        if (const auto* payload = intent.Get<GroupStyleOperation>()) policy_style = payload->value;
        return OperationPolicyDecision{AllowOperation{}};
    };
    auto styled = commands.Execute(std::make_unique<SetGroupStyleCommand>(group, replacement_style), document,
                                   presentation, types, style_policy);
    Expect(styled.has_value() && policy_style == replacement_style &&
               presentation.FindGroup(group)->style == replacement_style,
           "Style command and policy payload must share the replacement generation");
    const auto style_metrics = GetTransactionMetrics();
    Expect(style_metrics.group_styles.value_clones == 1 && style_metrics.group_styles.logical_bytes >= LargeBodySize &&
               style_metrics.group_styles.logical_bytes < LargeBodySize + 1024U,
           "Style change must account for exactly one immutable style allocation");

    ResetTransactionMetrics();
    Expect(commands.Undo(document, presentation, types).has_value() &&
               presentation.FindGroup(group)->style == original_style,
           "Style undo must restore the original shared generation");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               presentation.FindGroup(group)->style == replacement_style,
           "Style redo must restore the replacement shared generation");
    Expect(GetTransactionMetrics().group_styles.logical_bytes == 0,
           "Style undo/redo must not allocate or copy retained strings");

    const GroupId invalid_group = presentation.AllocateGroupId();
    GroupPresentation invalid{.id = invalid_group, .graph = graph};
    invalid.style.reset();
    auto rejected =
        commands.Execute(std::make_unique<AddGroupCommand>(std::move(invalid)), document, presentation, types);
    Expect(!rejected && rejected.error().code == ErrorCode::InvalidArgument,
           "Group mutation boundary must reject a null style handle");
}

void TestPersistentLinkPresentationMutations() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    RegistryCatalog& types = registry;
    CommandStack commands;
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value(), "Persistent route source must register");
    Expect(registry.RegisterNodeType(SinkDescriptor()).has_value(), "Persistent route sink must register");
    auto source = registry.Instantiate(document, TypeId{"test.source"});
    auto sink = registry.Instantiate(document, TypeId{"test.sink"});
    Expect(source && sink, "Persistent route nodes must instantiate");
    const GraphId graph = document.RootGraph();
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*source)), document, presentation, types,
            "Persistent route source must be added");
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*sink)), document, presentation, types,
            "Persistent route sink must be added");
    const LinkId link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = input}),
            document, presentation, types, "Persistent route link must connect");

    constexpr std::size_t RouteSize = 400'000;
    std::vector<RoutePoint> points;
    points.reserve(RouteSize);
    RoutePointId first;
    RoutePointId middle;
    RoutePointId last;
    for (std::size_t index = 0; index < RouteSize; ++index) {
        const RoutePointId id = presentation.AllocateRoutePointId();
        Expect(static_cast<bool>(id), "Persistent route ID allocation must succeed");
        if (index == 0) first = id;
        if (index == RouteSize / 2) middle = id;
        if (index + 1 == RouteSize) last = id;
        points.push_back(RoutePoint{
            .id = id,
            .position = {static_cast<float>(index), static_cast<float>(index % 97)},
        });
    }
    Execute(commands,
            std::make_unique<SetLinkPresentationCommand>(
                link, LinkPresentation{LinkStyle{
                                           .router = BezierLinkRouterType(),
                                           .color = 0xFF102030U,
                                       },
                                       PersistentRoutePointSequence{std::move(points)}}),
            document, presentation, types, "Persistent 400k route must be installed");
    commands.Clear();
    Expect(presentation.FindLink(link)->Route().size() == RouteSize && presentation.RoutePointOwner(first) == link &&
               presentation.RoutePointOwner(middle) == link && presentation.RoutePointOwner(last) == link,
           "Persistent route installation must preserve order and owner index "
           "entries");

    const PersistentRoutePointSequence initial_route = presentation.FindLink(link)->Route();
    const std::uint64_t color_geometry = presentation.GeometryRevision();
    ResetTransactionMetrics();
    Execute(commands, std::make_unique<SetLinkColorCommand>(link, 0xFFA0B0C0U), document, presentation, types,
            "400k route recolor must execute");
    const auto color_metrics = GetTransactionMetrics();
    Expect(presentation.GeometryRevision() == color_geometry &&
               presentation.FindLink(link)->Route().SharesStorageWith(initial_route) &&
               color_metrics.route_point_sequences.logical_bytes == 0 &&
               color_metrics.presentation_indexes.logical_bytes == 0,
           "Link recolor must preserve route roots, geometry revision, and owner "
           "indexes");

    commands.Clear();
    bool saw_style_payload = false;
    GraphPolicy style_policy;
    style_policy.evaluate_operation = [&](const OperationPolicyContext& context,
                                          const OperationIntent& intent) -> OperationPolicyDecision {
        const auto* payload = intent.Get<LinkPresentationOperation>();
        if (payload != nullptr) {
            saw_style_payload = payload->impact == LinkPresentationImpact::Style && payload->style &&
                                payload->style->color == 0xFF556677U && payload->route.empty() &&
                                context.staged_presentation.FindLink(link)->Route().SharesStorageWith(initial_route);
        }
        return AllowOperation{};
    };
    auto policy_color = commands.Execute(std::make_unique<SetLinkColorCommand>(link, 0xFF556677U), document,
                                         presentation, types, style_policy);
    Expect(policy_color && saw_style_payload, "Style policy payloads must carry only immutable style state and no "
                                              "route copy");

    commands.Clear();
    const std::uint64_t lock_geometry = presentation.GeometryRevision();
    ResetTransactionMetrics();
    Execute(commands, std::make_unique<SetLinkLockedCommand>(link, true), document, presentation, types,
            "400k route lock must execute");
    const auto lock_metrics = GetTransactionMetrics();
    Expect(presentation.GeometryRevision() == lock_geometry && presentation.FindLink(link)->Style().locked &&
               lock_metrics.route_point_sequences.logical_bytes == 0 &&
               lock_metrics.presentation_indexes.logical_bytes == 0,
           "Link lock must be protection-only and must not touch route storage "
           "or indexes");
    Execute(commands, std::make_unique<SetLinkLockedCommand>(link, false), document, presentation, types,
            "400k route unlock must execute");

    commands.Clear();
    const std::uint64_t router_geometry = presentation.GeometryRevision();
    ResetTransactionMetrics();
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, OrthogonalLinkRouterType()), document, presentation,
            types, "400k route router change must execute");
    const auto router_metrics = GetTransactionMetrics();
    Expect(presentation.GeometryRevision() == router_geometry + 1 &&
               presentation.FindLink(link)->Route().SharesStorageWith(initial_route) &&
               router_metrics.route_point_sequences.logical_bytes == 0 &&
               router_metrics.presentation_indexes.logical_bytes == 0,
           "Router changes must invalidate geometry without touching route roots "
           "or owner indexes");

    commands.Clear();
    const PersistentRoutePointSequence before_move = presentation.FindLink(link)->Route();
    const Vec2 previous_position = before_move.Find(middle)->position;
    const Vec2 moved_position{previous_position.x + 3.0f, previous_position.y + 7.0f};
    const std::uint64_t move_geometry = presentation.GeometryRevision();
    ResetTransactionMetrics();
    Execute(commands, std::make_unique<MoveRoutePointCommand>(link, middle, moved_position), document, presentation,
            types, "One point in a 400k route must move");
    const auto move_metrics = GetTransactionMetrics();
    const std::uint64_t move_route_bytes = move_metrics.route_point_sequences.logical_bytes;
    Expect(presentation.GeometryRevision() == move_geometry + 1 &&
               presentation.FindRoutePoint(link, middle)->position == moved_position &&
               before_move.Find(middle)->position == previous_position &&
               presentation.RoutePointOwner(middle) == link && move_metrics.route_point_sequences.root_clones == 1 &&
               move_metrics.route_point_sequences.value_clones == 1 &&
               move_metrics.route_point_sequences.shard_clones == 0 && move_route_bytes > 0 &&
               move_route_bytes < 128U * 1024U && move_metrics.presentation_indexes.logical_bytes == 0 &&
               move_metrics.incremental_records_validated == 1,
           "A one-point move must clone exactly one bounded route root/chunk "
           "path and no owner index");

    ResetTransactionMetrics();
    Expect(commands.Undo(document, presentation, types).has_value(), "400k point move undo must execute");
    const auto move_undo_metrics = GetTransactionMetrics();
    Expect(presentation.FindRoutePoint(link, middle)->position == previous_position &&
               move_undo_metrics.route_point_sequences.logical_bytes == move_route_bytes &&
               move_undo_metrics.presentation_indexes.logical_bytes == 0,
           "Point move undo must have the exact same structural clone bytes as "
           "apply");
    ResetTransactionMetrics();
    Expect(commands.Redo(document, presentation, types).has_value(), "400k point move redo must execute");
    const auto move_redo_metrics = GetTransactionMetrics();
    Expect(presentation.FindRoutePoint(link, middle)->position == moved_position &&
               move_redo_metrics.route_point_sequences.logical_bytes == move_route_bytes &&
               move_redo_metrics.presentation_indexes.logical_bytes == 0,
           "Point move redo must have the exact same structural clone bytes as "
           "apply");

    commands.Clear();
    const RoutePointId inserted = presentation.AllocateRoutePointId();
    const std::uint64_t insert_geometry = presentation.GeometryRevision();
    ResetTransactionMetrics();
    Execute(commands,
            std::make_unique<InsertRoutePointCommand>(link, RoutePoint{inserted, {7.0f, 11.0f}}, RouteSize / 2),
            document, presentation, types, "Point insertion into a 400k route must execute");
    const auto insert_metrics = GetTransactionMetrics();
    Expect(presentation.GeometryRevision() == insert_geometry + 1 &&
               presentation.FindLink(link)->Route().size() == RouteSize + 1 &&
               presentation.RoutePointOwner(inserted) == link &&
               insert_metrics.route_point_sequences.root_clones == 1 &&
               insert_metrics.route_point_sequences.value_clones == 1 &&
               insert_metrics.route_point_sequences.shard_clones != 0 &&
               insert_metrics.route_point_sequences.logical_bytes < 2U * 1024U * 1024U &&
               insert_metrics.presentation_indexes.logical_bytes != 0,
           "Point insertion must clone bounded route/index paths and add only "
           "its owner delta");

    const PersistentRoutePointSequence before_remove = presentation.FindLink(link)->Route();
    commands.Clear();
    ResetTransactionMetrics();
    Execute(commands, std::make_unique<RemoveRoutePointsCommand>(std::vector<RoutePointRef>{{link, inserted}}),
            document, presentation, types, "Point removal from a 400k route must execute");
    const auto remove_metrics = GetTransactionMetrics();
    Expect(presentation.FindLink(link)->Route().size() == RouteSize &&
               presentation.RoutePointOwner(inserted) == LinkId{} &&
               remove_metrics.route_point_sequences.root_clones == 1 &&
               remove_metrics.route_point_sequences.value_clones == 1 &&
               remove_metrics.route_point_sequences.shard_clones != 0 && remove_metrics.route_chunk_merges == 1 &&
               remove_metrics.route_points_reindexed <= PersistentRoutePointSequence::ChunkCapacity &&
               remove_metrics.presentation_indexes.logical_bytes != 0,
           "Point removal must compact its local route/index path and remove "
           "only its owner delta");

    ResetTransactionMetrics();
    Expect(commands.Undo(document, presentation, types).has_value(), "Route-only removal undo must execute");
    const auto route_undo_metrics = GetTransactionMetrics();
    Expect(presentation.FindLink(link)->Route().SharesStorageWith(before_remove) &&
               presentation.RoutePointOwner(inserted) == link &&
               route_undo_metrics.route_point_sequences.logical_bytes == 0,
           "Route-only undo must restore the retained persistent root without "
           "copying route points");
    ResetTransactionMetrics();
    Expect(commands.Redo(document, presentation, types).has_value(), "Route-only removal redo must execute");
    const auto route_redo_metrics = GetTransactionMetrics();
    Expect(presentation.RoutePointOwner(inserted) == LinkId{} &&
               route_redo_metrics.route_point_sequences.logical_bytes != 0,
           "Route-only redo must apply the bounded removal mutation and owner "
           "delta");

    commands.Clear();
    const PersistentRoutePointSequence identity_route = presentation.FindLink(link)->Route();
    ResetTransactionMetrics();
    auto identity = commands.Execute(std::make_unique<SetLinkRoutePointsCommand>(link, identity_route), document,
                                     presentation, types);
    const auto identity_metrics = GetTransactionMetrics();
    Expect(identity && !identity->presentation_changed &&
               presentation.FindLink(link)->Route().SharesStorageWith(identity_route) &&
               identity_metrics.route_point_sequences.logical_bytes == 0 &&
               identity_metrics.presentation_indexes.logical_bytes == 0,
           "Full route replacement must use the persistent-root identity fast path");
}

void TestLinkPresentationExactUndoAndJournal() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    RegistryCatalog& types = registry;
    CommandStack commands;
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value() &&
               registry.RegisterNodeType(SinkDescriptor()).has_value(),
           "Exact link-state descriptors must register");
    auto source = registry.Instantiate(document, TypeId{"test.source"});
    auto sink = registry.Instantiate(document, TypeId{"test.sink"});
    Expect(source && sink, "Exact link-state nodes must instantiate");
    const GraphId graph = document.RootGraph();
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*source)), document, presentation, types,
            "Exact link-state source must be added");
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*sink)), document, presentation, types,
            "Exact link-state sink must be added");
    const LinkId link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = input}),
            document, presentation, types, "Exact link-state link must connect");

    const auto reset_empty = [&] {
        Execute(commands, std::make_unique<SetLinkPresentationCommand>(link, LinkPresentation{}), document,
                presentation, types, "Empty link presentation must be installed");
        commands.Clear();
        Expect(presentation.FindLink(link) != nullptr && *presentation.FindLink(link) == LinkPresentation{},
               "Empty link presentation must remain present");
    };
    const auto expect_empty_undo = [&] {
        Expect(commands.Undo(document, presentation, types).has_value() && presentation.FindLink(link) != nullptr &&
                   *presentation.FindLink(link) == LinkPresentation{},
               "Undo must restore exact empty link presentation presence");
        Expect(commands.Redo(document, presentation, types).has_value(), "Exact link-state redo must execute");
    };

    reset_empty();
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, OrthogonalLinkRouterType()), document, presentation,
            types, "Router must apply to an empty present state");
    expect_empty_undo();
    Expect(presentation.FindLink(link)->Style().router == OrthogonalLinkRouterType(),
           "Router redo must restore the applied state");

    reset_empty();
    Execute(commands, std::make_unique<SetLinkColorCommand>(link, 0xFF123456U), document, presentation, types,
            "Color must apply to an empty present state");
    expect_empty_undo();
    Expect(presentation.FindLink(link)->Style().color == 0xFF123456U, "Color redo must restore the applied state");

    reset_empty();
    const RoutePoint route_point{presentation.AllocateRoutePointId(), {10.0f, 20.0f}};
    Execute(commands, std::make_unique<SetLinkRoutePointsCommand>(link, PersistentRoutePointSequence{{route_point}}),
            document, presentation, types, "Route must apply to an empty present state");
    expect_empty_undo();
    Expect(presentation.RoutePointOwner(route_point.id) == link, "Route redo must restore point ownership");

    reset_empty();
    const RoutePoint inserted{presentation.AllocateRoutePointId(), {30.0f, 40.0f}};
    Execute(commands, std::make_unique<InsertRoutePointCommand>(link, inserted, 0), document, presentation, types,
            "Insert must apply to an empty present state");
    expect_empty_undo();
    Expect(presentation.RoutePointOwner(inserted.id) == link, "Insert redo must restore point ownership");

    reset_empty();
    Execute(commands, std::make_unique<SetLinkLockedCommand>(link, true), document, presentation, types,
            "Lock must apply to an empty present state");
    expect_empty_undo();
    Expect(presentation.FindLink(link)->Style().locked, "Lock redo must restore the applied state");

    Execute(commands, std::make_unique<SetLinkLockedCommand>(link, false), document, presentation, types,
            "Lock-only presentation must be removed for journal tests");
    Expect(presentation.FindLink(link) == nullptr,
           "Unlocking the lock-only applied state must remove its presentation");
    commands.Clear();
    const std::uint64_t no_op_revision = presentation.PresentationRevision();
    const std::uint64_t no_op_geometry = presentation.GeometryRevision();
    std::vector<std::unique_ptr<Command>> canceling;
    canceling.push_back(std::make_unique<SetLinkColorCommand>(link, 0xFFAABBCCU));
    canceling.push_back(std::make_unique<SetLinkColorCommand>(link, std::nullopt));
    auto canceled =
        commands.Execute(std::make_unique<CompoundCommand>("Cancel link presentation", std::move(canceling)), document,
                         presentation, types);
    Expect(canceled && !canceled->presentation_changed && presentation.FindLink(link) == nullptr &&
               presentation.PresentationRevision() == no_op_revision &&
               presentation.GeometryRevision() == no_op_geometry && !commands.CanUndo(),
           "Null-to-null compound mutation must be a complete no-op");

    std::vector<std::unique_ptr<Command>> style_only;
    style_only.push_back(std::make_unique<SetLinkRouterCommand>(link, OrthogonalLinkRouterType()));
    style_only.push_back(std::make_unique<SetLinkRouterCommand>(link, TypeId{}));
    style_only.push_back(std::make_unique<SetLinkColorCommand>(link, 0xFF556677U));
    auto styled =
        commands.Execute(std::make_unique<CompoundCommand>("Restore geometry then style", std::move(style_only)),
                         document, presentation, types);
    Expect(styled && styled->presentation_changed && presentation.FindLink(link) != nullptr &&
               presentation.FindLink(link)->Style().color == 0xFF556677U &&
               presentation.FindLink(link)->Style().router.Empty() && presentation.GeometryRevision() == no_op_geometry,
           "Intermediate geometry changes must not advance final style-only "
           "geometry revision");
}

void TestMultiChunkPersistentRoutes() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    RegistryCatalog& types = registry;
    CommandStack commands;
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value() &&
               registry.RegisterNodeType(SinkDescriptor()).has_value(),
           "Multi-chunk route descriptors must register");
    auto source = registry.Instantiate(document, TypeId{"test.source"});
    auto sink = registry.Instantiate(document, TypeId{"test.sink"});
    Expect(source && sink, "Multi-chunk route nodes must instantiate");
    const GraphId graph = document.RootGraph();
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*source)), document, presentation, types,
            "Multi-chunk source must be added");
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*sink)), document, presentation, types,
            "Multi-chunk sink must be added");
    const LinkId link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = input}),
            document, presentation, types, "Multi-chunk link must connect");

    std::vector<RoutePointId> same_shard;
    for (std::uint64_t value = 1; same_shard.size() != 3; ++value) {
        const RoutePointId id{value};
        if ((IdHash{}(id) & 1023U) == 0) same_shard.push_back(id);
    }
    std::unordered_set<std::uint64_t> reserved;
    for (const RoutePointId id : same_shard)
        reserved.insert(id.Value());
    std::vector<RoutePoint> points;
    points.reserve(600);
    std::uint64_t filler = 1'000'000;
    for (std::size_t index = 0; index < 600; ++index) {
        RoutePointId id;
        if (index == 10)
            id = same_shard[0];
        else if (index == 300)
            id = same_shard[1];
        else if (index == 520)
            id = same_shard[2];
        else {
            while (reserved.contains(filler))
                ++filler;
            id = RoutePointId{filler++};
        }
        points.push_back(RoutePoint{id, {static_cast<float>(index), static_cast<float>(index % 13)}});
    }
    PersistentRoutePointSequence initial{points};
    PersistentRoutePointSequence independent{initial.ToVector()};
    Expect(initial == independent && !initial.SharesStorageWith(independent) &&
               initial.DifferenceIds(independent).added.empty() && initial.DifferenceIds(independent).removed.empty(),
           "Independent equal route roots must compare equal with an empty owner "
           "delta");
    Execute(commands, std::make_unique<SetLinkRoutePointsCommand>(link, initial), document, presentation, types,
            "Multi-chunk route must be installed");
    commands.Clear();
    const std::uint64_t equal_revision = presentation.PresentationRevision();
    auto equal_replacement =
        commands.Execute(std::make_unique<SetLinkRoutePointsCommand>(link, independent), document, presentation, types);
    Expect(equal_replacement && !equal_replacement->presentation_changed &&
               presentation.PresentationRevision() == equal_revision && !commands.CanUndo(),
           "Independent equal route replacement must be a no-op");

    ResetTransactionMetrics();
    auto non_finite = initial.WithMovedPoint(same_shard[0], Vec2{std::numeric_limits<float>::quiet_NaN(), 0.0f});
    Expect(!non_finite && non_finite.error().code == ErrorCode::InvalidArgument &&
               GetTransactionMetrics().route_point_sequences.logical_bytes == 0,
           "Non-finite persistent point moves must fail before cloning");

    const RoutePointId boundary{2'000'000};
    Execute(commands, std::make_unique<InsertRoutePointCommand>(link, RoutePoint{boundary, {-1.0f, -2.0f}}, 256),
            document, presentation, types, "Chunk-boundary insert must execute");
    const auto& inserted_route = presentation.FindLink(link)->Route();
    Expect(inserted_route.size() == 601 && inserted_route[255].id == points[255].id &&
               inserted_route[256].id == boundary && inserted_route[257].id == points[256].id,
           "Chunk-boundary insertion must preserve exact sequence order");

    const Vec2 moved{777.0f, 888.0f};
    Execute(commands, std::make_unique<MoveRoutePointCommand>(link, same_shard[2], moved), document, presentation,
            types, "Third-chunk point move must execute");
    Expect(presentation.FindRoutePoint(link, same_shard[2])->position == moved,
           "Third-chunk point move must update the indexed point");
    Execute(commands,
            std::make_unique<RemoveRoutePointsCommand>(
                std::vector<RoutePointRef>{{link, same_shard[0]}, {link, same_shard[1]}, {link, same_shard[2]}}),
            document, presentation, types, "Same-shard multi-chunk removal must execute");
    Expect(presentation.FindLink(link)->Route().size() == 598 &&
               presentation.RoutePointOwner(same_shard[0]) == LinkId{} &&
               presentation.RoutePointOwner(same_shard[1]) == LinkId{} &&
               presentation.RoutePointOwner(same_shard[2]) == LinkId{} &&
               ValidateGraphPresentation(document, presentation).has_value(),
           "Same-shard removals across chunks must preserve all route indexes");

    constexpr std::size_t FragmentChunks = 16;
    std::vector<RoutePoint> fragmented_points;
    fragmented_points.reserve(FragmentChunks * PersistentRoutePointSequence::ChunkCapacity);
    for (std::size_t index = 0; index < FragmentChunks * PersistentRoutePointSequence::ChunkCapacity; ++index) {
        fragmented_points.push_back(RoutePoint{
            presentation.AllocateRoutePointId(),
            Vec2{static_cast<float>(index), static_cast<float>(index % 17)},
        });
    }
    Execute(commands, std::make_unique<SetLinkRoutePointsCommand>(link, fragmented_points), document, presentation,
            types, "Adversarial route base must be installed");
    for (std::size_t chunk = 0; chunk < FragmentChunks; ++chunk) {
        const std::size_t index =
            chunk * (PersistentRoutePointSequence::ChunkCapacity + 1) + PersistentRoutePointSequence::ChunkCapacity / 2;
        Execute(
            commands,
            std::make_unique<InsertRoutePointCommand>(
                link, RoutePoint{presentation.AllocateRoutePointId(), Vec2{-1.0f, static_cast<float>(chunk)}}, index),
            document, presentation, types, "Every full route chunk must split");
    }
    commands.Clear();
    const PersistentRoutePointSequence before_compaction = presentation.FindLink(link)->Route();
    const auto fragmented_statistics = before_compaction.StorageStatistics();
    Expect(fragmented_statistics.chunk_count == FragmentChunks * 2 &&
               fragmented_statistics.mergeable_adjacent_pairs == 0 && before_compaction.ValidateStructure(),
           "Split route fixture must begin with valid non-mergeable 128/129 chunks");
    const auto retained_begin = before_compaction.begin();
    const auto retained_end = before_compaction.end();

    std::unordered_set<RoutePointId, IdHash> survivors;
    for (std::size_t chunk = 0; chunk < FragmentChunks; ++chunk) {
        survivors.insert(fragmented_points[chunk * PersistentRoutePointSequence::ChunkCapacity].id);
        survivors.insert(fragmented_points[(chunk + 1) * PersistentRoutePointSequence::ChunkCapacity - 1].id);
    }
    std::vector<RoutePointRef> fragmented_removals;
    for (const RoutePoint& point : before_compaction) {
        if (!survivors.contains(point.id)) fragmented_removals.push_back(RoutePointRef{link, point.id});
    }
    ResetTransactionMetrics();
    Execute(commands, std::make_unique<RemoveRoutePointsCommand>(fragmented_removals), document, presentation, types,
            "Adversarial fragmented route must compact on removal");
    const auto compaction_metrics = GetTransactionMetrics();
    const auto& compacted = presentation.FindLink(link)->Route();
    const auto compacted_statistics = compacted.StorageStatistics();
    Expect(compacted.size() == survivors.size() && compacted_statistics.chunk_count == 1 &&
               compacted_statistics.mergeable_adjacent_pairs == 0 && compacted.ValidateStructure() &&
               compaction_metrics.route_chunk_merges == FragmentChunks * 2 - 1 &&
               compaction_metrics.route_points_reindexed == survivors.size() - 1,
           "Removal must coalesce all adjacent sparse chunks and report exact "
           "compaction work");
    for (const RoutePointId survivor : survivors) {
        Expect(compacted.contains(survivor) && compacted.Find(survivor) != nullptr &&
                   presentation.RoutePointOwner(survivor) == link,
               "Every compacted survivor must retain exact route and owner indexes");
    }
    for (const RoutePointRef removed : fragmented_removals) {
        Expect(!compacted.contains(removed.point) && compacted.Find(removed.point) == nullptr &&
                   presentation.RoutePointOwner(removed.point) == LinkId{},
               "Every removed fragmented point must leave route and owner indexes");
    }
    const auto delta = before_compaction.DifferenceIds(compacted);
    Expect(delta.added.empty() && delta.removed.size() == fragmented_removals.size(),
           "Internal chunk relocation must not appear in the logical route-point "
           "delta");
    const std::vector<RoutePoint> retained_iteration{retained_begin, retained_end};
    Expect(retained_iteration == before_compaction.ToVector(),
           "Iterators must retain the pre-compaction immutable root");
    Expect(ValidateGraphPresentation(document, presentation).has_value(),
           "Explicit presentation validation must verify compacted chunk indexes");

    Expect(commands.Undo(document, presentation, types).has_value() &&
               presentation.FindLink(link)->Route().SharesStorageWith(before_compaction) &&
               presentation.FindLink(link)->Route().StorageStatistics().chunk_count == FragmentChunks * 2,
           "Compaction undo must restore the exact retained fragmented root");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               presentation.FindLink(link)->Route().StorageStatistics().chunk_count == 1 &&
               presentation.FindLink(link)->Route().ValidateStructure(),
           "Compaction redo must deterministically restore the compact "
           "representation");

    std::vector<RoutePoint> multi_group_points;
    multi_group_points.reserve(800);
    for (std::size_t index = 0; index < 800; ++index) {
        multi_group_points.push_back(RoutePoint{
            presentation.AllocateRoutePointId(),
            Vec2{static_cast<float>(index), 0.0f},
        });
    }
    Execute(commands, std::make_unique<SetLinkRoutePointsCommand>(link, multi_group_points), document, presentation,
            types, "Multi-group compaction route must be installed");
    std::vector<RoutePointRef> multi_group_removals;
    for (std::size_t chunk = 0; chunk < 3; ++chunk) {
        for (std::size_t offset = 0; offset < 56; ++offset) {
            multi_group_removals.push_back(RoutePointRef{
                link,
                multi_group_points[chunk * PersistentRoutePointSequence::ChunkCapacity + offset].id,
            });
        }
    }
    Execute(commands, std::make_unique<RemoveRoutePointsCommand>(multi_group_removals), document, presentation, types,
            "Multi-group route compaction must execute");
    const auto multi_group_statistics = presentation.FindLink(link)->Route().StorageStatistics();
    Expect(multi_group_statistics.point_count == 632 && multi_group_statistics.chunk_count == 3 &&
               multi_group_statistics.mergeable_adjacent_pairs == 0 &&
               presentation.FindLink(link)->Route().ValidateStructure() &&
               ValidateGraphPresentation(document, presentation).has_value(),
           "Compaction must reach the no-mergeable-neighbor fixed point across "
           "multiple output groups");
}

void TestCompatibilityOwnershipAndHistory() {
    RegistryCatalog compatibility;
    Expect(compatibility
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"convert.int-float"},
                   .display_name = "Int to float",
                   .static_pins =
                       {
                           PinDescriptor{.key = "input", .type = TypeId{"int"}},
                           PinDescriptor{
                               .key = "output",
                               .type = TypeId{"float"},
                               .direction = PinDirection::Output,
                           },
                       },
               })
               .has_value(),
           "Conversion node must register");
    Expect(compatibility.Check(TypeId{"float"}, TypeId{"float"}, PinKind::Data).status ==
               ConnectionResult::Status::Allowed,
           "Equal data types must connect");
    Expect(compatibility.Check(TypeId{"*"}, TypeId{"image"}, PinKind::Data).status == ConnectionResult::Status::Allowed,
           "Wildcard data types must connect");
    Expect(compatibility
               .RegisterConversion(ConversionDescriptor{
                   .key =
                       ConversionKey{
                           .source_type = TypeId{"int"},
                           .destination_type = TypeId{"float"},
                           .kind = PinKind::Data,
                       },
                   .node_type = TypeId{"convert.int-float"},
                   .input_pin = "input",
                   .output_pin = "output",
               })
               .has_value(),
           "Conversion must register");
    Expect(compatibility.Check(TypeId{"int"}, TypeId{"float"}, PinKind::Data).status ==
               ConnectionResult::Status::RequiresConversion,
           "Registered conversion must be reported");
    Expect(compatibility.Check(TypeId{"audio"}, TypeId{"image"}, PinKind::Data).status ==
               ConnectionResult::Status::Rejected,
           "Unrelated data types must be rejected");

    GraphDocument ordered_document;
    GraphPresentation ordered_presentation;
    RegistryCatalog ordered_registry;
    RegistryCatalog& ordered_types = ordered_registry;
    CommandStack ordered_commands;
    Expect(ordered_registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"ordered.static"},
                   .display_name = "Ordered static pins",
                   .static_pins =
                       {
                           PinDescriptor{.key = "first", .label = "First", .type = TypeId{"float"}},
                           PinDescriptor{.key = "second", .label = "Second", .type = TypeId{"float"}},
                       },
               })
               .has_value(),
           "Ordered static pin descriptor must register");
    auto wrong_order = ordered_registry.Instantiate(ordered_document, TypeId{"ordered.static"});
    Expect(wrong_order.has_value(), "Ordered static pin node must instantiate");
    std::ranges::reverse(wrong_order->node.pins);
    std::ranges::reverse(wrong_order->pins);
    auto wrong_order_result = ordered_commands.Execute(
        std::make_unique<AddNodeCommand>(ordered_document.RootGraph(), std::move(*wrong_order)), ordered_document,
        ordered_presentation, ordered_types);
    Expect(!wrong_order_result && wrong_order_result.error().code == ErrorCode::InvalidGraph &&
               ordered_document.FindGraph(ordered_document.RootGraph())->nodes.empty(),
           "Add node must reject a prepared creation whose static pin order no "
           "longer matches its descriptor");

    GraphDocument first_document;
    GraphPresentation first_presentation;
    GraphDocument second_document;
    GraphPresentation second_presentation;
    RegistryCatalog types;
    CommandStack commands;
    const GraphId graph = first_document.RootGraph();
    const NodeId node = first_document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                graph, NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"ownership"}}}),
            first_document, first_presentation, types, "Ownership fixture must execute");
    auto wrong_owner = commands.Undo(second_document, second_presentation, types);
    Expect(!wrong_owner && wrong_owner.error().code == ErrorCode::CommandFailed,
           "History must reject a different document and presentation");
    Expect(first_document.FindNode(graph, node) != nullptr,
           "Rejected ownership mismatch must preserve the original document");

    RegistryCatalog foreign_catalog;
    auto foreign_execute =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(graph, node, "foreign-execute", PropertyValue{true}),
                         first_document, first_presentation, foreign_catalog);
    auto foreign_undo = commands.Undo(first_document, first_presentation, foreign_catalog);
    Expect(!foreign_execute && foreign_execute.error().code == ErrorCode::RegistryMismatch && !foreign_undo &&
               foreign_undo.error().code == ErrorCode::RegistryMismatch,
           "Execute and undo must reject a catalog different from the one bound "
           "to history");
    Expect(commands.Undo(first_document, first_presentation, types).has_value(),
           "History catalog fixture must enter redo state");
    auto foreign_redo = commands.Redo(first_document, first_presentation, foreign_catalog);
    Expect(!foreign_redo && foreign_redo.error().code == ErrorCode::RegistryMismatch &&
               commands.Redo(first_document, first_presentation, types).has_value(),
           "Redo must reject a foreign catalog without consuming history");

    commands.Clear();
    Expect(commands
               .Execute(std::make_unique<SetNodePropertyCommand>(graph, node, "after-clear", PropertyValue{true}),
                        first_document, first_presentation, foreign_catalog)
               .has_value(),
           "Clear must release the command stack catalog binding");
    commands.SetHistoryLimit(0);
    commands.SetHistoryLimit(8);
    Expect(commands
               .Execute(std::make_unique<SetNodePropertyCommand>(graph, node, "after-limit-reset", PropertyValue{true}),
                        first_document, first_presentation, types)
               .has_value(),
           "Disabling history must clear its catalog binding");

    commands.Clear();
    for (int value = 1; value <= 3; ++value) {
        Execute(commands,
                std::make_unique<SetNodePropertyCommand>(graph, node, "history", PropertyValue{std::to_string(value)}),
                first_document, first_presentation, types, "History command must execute");
    }
    Expect(commands.Undo(first_document, first_presentation, types).has_value() &&
               commands.Undo(first_document, first_presentation, types).has_value() &&
               commands.Undo(first_document, first_presentation, types).has_value(),
           "History commands must all undo");
    commands.SetHistoryLimit(1);
    Expect(commands.Redo(first_document, first_presentation, types).has_value(), "Retained redo command must execute");
    Expect(!commands.CanRedo(), "History limit must trim excess redo entries");

    GraphDocument exhausted;
    GraphPresentation exhausted_presentation;
    CommandStack exhausted_commands;
    const GraphId exhausted_graph = exhausted.AllocateGraphId();
    Graph maximum_ids{.id = exhausted_graph};
    const NodeId maximum_node{std::numeric_limits<std::uint64_t>::max()};
    maximum_ids.nodes.emplace(maximum_node, NodeInstance{
                                                .id = maximum_node,
                                                .type = TypeId{"maximum"},
                                            });
    Execute(exhausted_commands, std::make_unique<AddGraphCommand>(std::move(maximum_ids)), exhausted,
            exhausted_presentation, types, "Maximum node ID graph must restore");
    RegistryCatalog registry;
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"maximum"},
                   .display_name = "Maximum",
               })
               .has_value(),
           "Maximum ID descriptor must register");
    Expect(!registry.Instantiate(exhausted, TypeId{"maximum"}), "Node instantiation must report exhausted IDs");
}

void TestTypeCompletionPoliciesAndProtection() {
    RegistryCatalog registry;
    RegistryCatalog& types = registry;
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"stage3.int-source"},
                   .display_name = "Int source",
                   .static_pins =
                       {
                           PinDescriptor{
                               .key = "value",
                               .type = TypeId{"int"},
                               .direction = PinDirection::Output,
                               .cardinality = PinCardinality::Multiple,
                           },
                       },
               })
               .has_value(),
           "Stage-three int source must register");
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"stage3.float-sink"},
                   .display_name = "Float sink",
                   .static_pins =
                       {
                           PinDescriptor{.key = "value", .type = TypeId{"float"}},
                       },
               })
               .has_value(),
           "Stage-three float sink must register");
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"stage3.float-source"},
                   .display_name = "Float source",
                   .static_pins =
                       {
                           PinDescriptor{
                               .key = "value",
                               .type = TypeId{"float"},
                               .direction = PinDirection::Output,
                               .cardinality = PinCardinality::Multiple,
                           },
                       },
               })
               .has_value(),
           "Stage-three float source must register");
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"stage3.int-sink"},
                   .display_name = "Int sink",
                   .static_pins =
                       {
                           PinDescriptor{.key = "value", .type = TypeId{"int"}},
                       },
               })
               .has_value(),
           "Stage-three int sink must register");
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"stage3.int-to-float"},
                   .display_name = "Int to float",
                   .static_pins =
                       {
                           PinDescriptor{.key = "source", .type = TypeId{"int"}},
                           PinDescriptor{
                               .key = "result",
                               .type = TypeId{"float"},
                               .direction = PinDirection::Output,
                               .cardinality = PinCardinality::Multiple,
                           },
                       },
               })
               .has_value(),
           "Stage-three converter must register");
    const ConversionDescriptor conversion{
        .key =
            ConversionKey{
                .source_type = TypeId{"int"},
                .destination_type = TypeId{"float"},
                .kind = PinKind::Data,
            },
        .node_type = TypeId{"stage3.int-to-float"},
        .input_pin = "source",
        .output_pin = "result",
    };
    auto invalid_conversion = conversion;
    invalid_conversion.input_pin = "missing";
    Expect(!types.RegisterConversion(invalid_conversion), "A conversion with a missing semantic pin must be rejected");
    Expect(types.RegisterConversion(conversion).has_value(), "A validated semantic-key conversion must register");

    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    auto source = registry.Instantiate(document, TypeId{"stage3.int-source"});
    auto sink = registry.Instantiate(document, TypeId{"stage3.float-sink"});
    Expect(source && sink, "Conversion endpoints must instantiate");
    const NodeId source_id = source->node.id;
    const NodeId sink_id = sink->node.id;
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*source), NodePresentation{.position = {0.0f, 0.0f}}),
            document, presentation, types, "Conversion source must be added");
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*sink), NodePresentation{.position = {400.0f, 0.0f}}),
            document, presentation, types, "Conversion sink must be added");
    commands.Clear();

    const auto decision = ValidateConnection(document, presentation, ConnectionRequest{graph, output, input}, types);
    Expect(decision.status == ConnectionResult::Status::RequiresConversion && decision.recipe &&
               decision.recipe->Descriptor() == conversion,
           "Connection query must return the exact conversion recipe");
    auto prepared = PrepareConnectionCommand(document, presentation, types, ConnectionRequest{graph, output, input},
                                             Vec2{200.0f, 40.0f});
    Expect(prepared.has_value(), "Convertible pins must produce an insertion command");
    const Revisions before_conversion{document.ModelRevision(), presentation.PresentationRevision()};
    auto inserted = commands.Execute(std::move(*prepared), document, presentation, types);
    Expect(inserted && inserted->model_changed && inserted->presentation_changed &&
               inserted->revisions == Revisions{before_conversion.model + 1, before_conversion.presentation + 1},
           "Conversion insertion must commit one semantic and presentation "
           "revision");
    const auto* converted_graph = document.FindGraph(graph);
    Expect(converted_graph->nodes.size() == 3 && converted_graph->links.size() == 2,
           "Conversion insertion must materialize one node and two links");
    const auto converter = std::ranges::find_if(
        converted_graph->nodes, [](const auto& entry) { return entry.second.type == TypeId{"stage3.int-to-float"}; });
    Expect(converter != converted_graph->nodes.end(), "Inserted converter must use the registered node type");
    const NodeId converter_id = converter->first;
    const auto* converter_state = presentation.FindNode(converter_id);
    Expect(converter_state != nullptr && converter_state->position == Vec2{200.0f, 40.0f},
           "Inserted converter must use the prepared position");
    std::vector<LinkId> conversion_links;
    for (const auto& [link_id, link] : converted_graph->links) {
        conversion_links.push_back(link_id);
        const auto* link_output = document.FindPin(graph, link.output);
        const auto* link_input = document.FindPin(graph, link.input);
        Expect(link_output != nullptr && link_input != nullptr && link_output->type == link_input->type,
               "Every materialized conversion leg must be directly type-compatible");
    }
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindNode(graph, converter_id) == nullptr && document.FindGraph(graph)->links.empty(),
           "One undo must remove the complete conversion insertion");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               document.FindNode(graph, converter_id) != nullptr &&
               std::ranges::all_of(conversion_links,
                                   [&](const LinkId link) { return document.FindLink(graph, link) != nullptr; }),
           "Conversion redo must preserve all allocated identities");

    auto int_sink = registry.Instantiate(document, TypeId{"stage3.int-sink"});
    auto second_float_sink = registry.Instantiate(document, TypeId{"stage3.float-sink"});
    Expect(int_sink && second_float_sink, "Converted reconnect endpoints must instantiate");
    const NodeId int_sink_id = int_sink->node.id;
    const NodeId second_float_sink_id = second_float_sink->node.id;
    const PinId int_input = int_sink->pins.front().id;
    const PinId second_float_input = second_float_sink->pins.front().id;
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(graph, std::move(*int_sink), NodePresentation{.position = {300.0f, 180.0f}}),
        document, presentation, types, "Int sink must be added");
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*second_float_sink),
                                             NodePresentation{.position = {500.0f, 180.0f}}),
            document, presentation, types, "Second float sink must be added");
    const LinkId reconnect_link = document.AllocateLinkId();
    Execute(
        commands,
        std::make_unique<ConnectPinsCommand>(graph, Link{.id = reconnect_link, .output = output, .input = int_input}),
        document, presentation, types, "Direct reconnect fixture must connect");
    const RoutePointId reconnect_route = presentation.AllocateRoutePointId();
    Execute(commands,
            std::make_unique<SetLinkPresentationCommand>(
                reconnect_link, LinkPresentation{LinkStyle{.color = 0xFF123456U},
                                                 PersistentRoutePointSequence{{reconnect_route, {180.0f, 150.0f}}}}),
            document, presentation, types, "Reconnect route fixture must be added");
    commands.Clear();
    auto converted_reconnect = PrepareConnectionCommand(
        document, presentation, types, ConnectionRequest{graph, output, second_float_input, reconnect_link},
        Vec2{260.0f, 170.0f});
    Expect(converted_reconnect.has_value(), "Convertible reconnect must produce one atomic command");
    Execute(commands, std::move(*converted_reconnect), document, presentation, types,
            "Convertible reconnect must execute");
    Expect(document.FindLink(graph, reconnect_link) != nullptr &&
               document.FindLink(graph, reconnect_link)->output == output &&
               presentation.FindLink(reconnect_link) != nullptr &&
               presentation.FindLink(reconnect_link)->Route().empty() &&
               presentation.FindLink(reconnect_link)->Style().color == 0xFF123456U,
           "Converted reconnect must retain the fixed-side link identity and "
           "reset only stale routing");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindLink(graph, reconnect_link)->input == int_input &&
               presentation.FindRoutePoint(reconnect_link, reconnect_route) != nullptr,
           "Converted reconnect undo must restore the original link and route");
    Expect(document.FindNode(graph, int_sink_id) != nullptr &&
               document.FindNode(graph, second_float_sink_id) != nullptr &&
               document.FindNode(graph, source_id) != nullptr && document.FindNode(graph, sink_id) != nullptr,
           "Converted reconnect must not affect endpoint node ownership");

    commands.Clear();
    const auto no_op_reconnect =
        commands.Execute(std::make_unique<ReconnectLinkCommand>(graph, reconnect_link, output, int_input), document,
                         presentation, types);
    Expect(no_op_reconnect && !no_op_reconnect->model_changed && !no_op_reconnect->presentation_changed &&
               presentation.FindRoutePoint(reconnect_link, reconnect_route) != nullptr,
           "Reconnect to unchanged endpoints must be a true no-op that preserves "
           "routing");

    Execute(commands, std::make_unique<SetPinReadOnlyCommand>(graph, int_input, true), document, presentation, types,
            "Remote reconnect endpoint must be protected");
    auto protected_reconnect = PrepareConnectionCommand(
        document, presentation, types, ConnectionRequest{graph, output, second_float_input, reconnect_link},
        Vec2{260.0f, 170.0f});
    Expect(!protected_reconnect && protected_reconnect.error().code == ErrorCode::ReadOnly &&
               document.FindLink(graph, reconnect_link)->input == int_input,
           "Reconnect must not detach a read-only old endpoint");
    auto protected_cascade = commands.Execute(
        std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{source_id}), document, presentation, types);
    Expect(!protected_cascade && protected_cascade.error().code == ErrorCode::ReadOnly &&
               document.FindNode(graph, source_id) != nullptr && document.FindLink(graph, reconnect_link) != nullptr,
           "Cascading node deletion must respect protection on the opposite "
           "endpoint");
    Expect(commands.Undo(document, presentation, types).has_value() && !document.FindPin(graph, int_input)->read_only,
           "Remote endpoint protection fixture must undo");

    auto float_source = registry.Instantiate(document, TypeId{"stage3.float-source"});
    auto fixed_sink = registry.Instantiate(document, TypeId{"stage3.float-sink"});
    Expect(float_source && fixed_sink, "Fixed-input reconnect endpoints must instantiate");
    const PinId float_output = float_source->pins.front().id;
    const PinId fixed_input = fixed_sink->pins.front().id;
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(graph, std::move(*float_source), NodePresentation{.position = {0.0f, 360.0f}}),
        document, presentation, types, "Float source must be added");
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(graph, std::move(*fixed_sink), NodePresentation{.position = {500.0f, 360.0f}}),
        document, presentation, types, "Fixed float sink must be added");
    const LinkId fixed_link = document.AllocateLinkId();
    Execute(commands,
            std::make_unique<ConnectPinsCommand>(graph,
                                                 Link{.id = fixed_link, .output = float_output, .input = fixed_input}),
            document, presentation, types, "Fixed-input reconnect link must connect");
    commands.Clear();
    auto fixed_input_conversion = PrepareConnectionCommand(
        document, presentation, types, ConnectionRequest{graph, output, fixed_input, fixed_link}, Vec2{280.0f, 330.0f});
    Expect(fixed_input_conversion.has_value(), "Fixed-input converted reconnect must prepare");
    Execute(commands, std::move(*fixed_input_conversion), document, presentation, types,
            "Fixed-input converted reconnect must execute");
    Expect(document.FindLink(graph, fixed_link) != nullptr &&
               document.FindLink(graph, fixed_link)->input == fixed_input &&
               document.FindLink(graph, fixed_link)->output != float_output,
           "Converted reconnect must retain the old identity on the fixed input "
           "leg");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindLink(graph, fixed_link)->output == float_output,
           "Fixed-input converted reconnect undo must restore its original source");

    GraphPolicy deny_connection;
    deny_connection.evaluate_operation = [](const OperationPolicyContext&,
                                            const OperationIntent& intent) -> OperationPolicyDecision {
        return intent.kind == OperationKind::Connect ? OperationPolicyDecision{DenyOperation{"runtime connection lock"}}
                                                     : OperationPolicyDecision{AllowOperation{}};
    };
    auto denied_connection = commands.Execute(
        std::make_unique<ConnectPinsCommand>(graph, Link{document.AllocateLinkId(), float_output, second_float_input}),
        document, presentation, types, deny_connection);
    Expect(!denied_connection && denied_connection.error().code == ErrorCode::PolicyRejected,
           "Graph-aware connection policy must guard execution");

    auto policy_node = registry.Instantiate(document, TypeId{"stage3.float-sink"});
    Expect(policy_node.has_value(), "Policy node must instantiate");
    GraphPolicy deny_create;
    deny_create.evaluate_operation = [](const OperationPolicyContext&,
                                        const OperationIntent& intent) -> OperationPolicyDecision {
        return intent.kind == OperationKind::AddNode
                   ? OperationPolicyDecision{DenyOperation{"float sinks are disabled"}}
                   : OperationPolicyDecision{AllowOperation{}};
    };
    const NodeId denied_node_id = policy_node->node.id;
    auto denied_add = commands.Execute(std::make_unique<AddNodeCommand>(graph, std::move(*policy_node)), document,
                                       presentation, types, deny_create);
    Expect(!denied_add && denied_add.error().code == ErrorCode::PolicyRejected &&
               document.FindNode(graph, denied_node_id) == nullptr,
           "Create policy must be enforced by the transaction layer");

    auto reentrant_node = registry.Instantiate(document, TypeId{"stage3.int-sink"});
    Expect(reentrant_node.has_value(), "Reentrant policy fixture must instantiate");
    const NodeId reentrant_node_id = reentrant_node->node.id;
    std::optional<ErrorCode> nested_error;
    GraphPolicy reentrant_policy;
    reentrant_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                              const OperationIntent&) -> OperationPolicyDecision {
        auto nested = commands.Execute(
            std::make_unique<SetNodePropertyCommand>(graph, source_id, "reentrant", PropertyValue{std::int64_t{1}}),
            document, presentation, types);
        if (!nested) nested_error = nested.error().code;
        return AllowOperation{};
    };
    auto reentrant_outer = commands.Execute(std::make_unique<AddNodeCommand>(graph, std::move(*reentrant_node)),
                                            document, presentation, types, reentrant_policy);
    Expect(reentrant_outer && nested_error == ErrorCode::CommandFailed &&
               document.FindNode(graph, reentrant_node_id) != nullptr &&
               !document.FindNode(graph, source_id)->properties.contains("reentrant"),
           "Command stack must reject reentrant policy execution without "
           "corrupting history");

    GraphPolicy deny_delete;
    deny_delete.evaluate_operation = [](const OperationPolicyContext&,
                                        const OperationIntent& intent) -> OperationPolicyDecision {
        return intent.kind == OperationKind::DeleteElements
                   ? OperationPolicyDecision{DenyOperation{"source is required"}}
                   : OperationPolicyDecision{AllowOperation{}};
    };
    auto denied_delete =
        commands.Execute(std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{source_id}), document,
                         presentation, types, deny_delete);
    Expect(!denied_delete && denied_delete.error().code == ErrorCode::PolicyRejected &&
               document.FindNode(graph, source_id) != nullptr,
           "Delete policy must reject cascades atomically");

    const GroupId policy_group = presentation.AllocateGroupId();
    GraphPolicy deny_group;
    deny_group.evaluate_operation = [](const OperationPolicyContext&,
                                       const OperationIntent& intent) -> OperationPolicyDecision {
        return intent.kind == OperationKind::AddGroup || intent.kind == OperationKind::RemoveGroup ||
                       intent.kind == OperationKind::SetGroupGeometry || intent.kind == OperationKind::SetGroupStyle ||
                       intent.kind == OperationKind::SetGroupMembers
                   ? OperationPolicyDecision{DenyOperation{"groups are managed externally"}}
                   : OperationPolicyDecision{AllowOperation{}};
    };
    auto denied_group = commands.Execute(std::make_unique<AddGroupCommand>(GroupPresentation{
                                             .id = policy_group,
                                             .graph = graph,
                                             .members = {source_id},
                                         }),
                                         document, presentation, types, deny_group);
    Expect(!denied_group && denied_group.error().code == ErrorCode::PolicyRejected &&
               presentation.FindGroup(policy_group) == nullptr,
           "Group policy must guard group creation and membership");

    std::vector<OperationIntent> preview_intents;
    GraphPolicy preview_policy;
    preview_policy.evaluate_operation = [&](const OperationPolicyContext& context, const OperationIntent& intent) {
        Expect(context.phase == OperationPhase::Preview, "Operation-specific policy checks must use the preview phase");
        preview_intents.push_back(intent);
        return OperationPolicyDecision{AllowOperation{}};
    };
    const auto* output_pin = document.FindPin(graph, output);
    const auto* input_pin = document.FindPin(graph, second_float_input);
    const auto* output_node = output_pin != nullptr ? document.FindNode(graph, output_pin->node) : nullptr;
    const auto* input_node = input_pin != nullptr ? document.FindNode(graph, input_pin->node) : nullptr;
    Expect(output_pin != nullptr && input_pin != nullptr && output_node != nullptr && input_node != nullptr,
           "Policy preview fixture endpoints must exist");
    (void)preview_policy.CheckCreateNode(document, presentation,
                                         CreateNodePolicyRequest{graph, TypeId{"stage3.float-sink"}});
    (void)preview_policy.CheckDeleteNode(document, presentation, DeleteNodePolicyRequest{graph, source_id});
    (void)preview_policy.CheckConnection(document, presentation,
                                         ConnectionPolicyRequest{
                                             graph,
                                             *output_node,
                                             *output_pin,
                                             *input_node,
                                             *input_pin,
                                             std::nullopt,
                                         });
    const GroupPresentation preview_group{
        .id = policy_group,
        .graph = graph,
    };
    (void)preview_policy.CheckGroupLifecycle(document, presentation,
                                             GroupLifecyclePolicyRequest{graph, nullptr, &preview_group});
    const auto* preview_create = preview_intents[0].Get<NodeOperation>();
    const auto* preview_delete = preview_intents[1].Get<NodeOperation>();
    const auto* preview_link = preview_intents[2].Get<LinkOperation>();
    const auto* preview_group_payload = preview_intents[3].Get<GroupLifecycleOperation>();
    Expect(preview_intents.size() == 4 && preview_create && preview_create->graph == graph && preview_create->Type() &&
               *preview_create->Type() == TypeId{"stage3.float-sink"} && preview_delete &&
               preview_delete->graph == graph && preview_delete->node == source_id && preview_link &&
               preview_link->graph == graph && preview_link->value.output == output_pin->id &&
               preview_link->value.input == input_pin->id && preview_group_payload &&
               preview_group_payload->graph == graph && preview_group_payload->group == policy_group,
           "Operation-specific policy previews must preserve request metadata");

    commands.Clear();
    Execute(commands, std::make_unique<SetNodeReadOnlyCommand>(graph, source_id, true), document, presentation, types,
            "Node read-only must be enabled");
    auto blocked_property = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "blocked", PropertyValue{std::int64_t{1}}), document,
        presentation, types);
    Expect(!blocked_property && blocked_property.error().code == ErrorCode::ReadOnly,
           "Read-only nodes must reject semantic edits");
    auto blocked_bypass =
        commands.Execute(std::make_unique<UnlockEditRelockCommand>(graph, source_id), document, presentation, types);
    Expect(!blocked_bypass && blocked_bypass.error().code == ErrorCode::ReadOnly &&
               document.FindNode(graph, source_id)->read_only &&
               !document.FindNode(graph, source_id)->properties.contains("bypass"),
           "Protection captured at transaction start must prevent "
           "unlock-edit-relock bypasses");
    Expect(commands.Undo(document, presentation, types).has_value() && !document.FindNode(graph, source_id)->read_only,
           "Undo must be able to restore protection state");

    Execute(commands, std::make_unique<SetNodeLockedCommand>(source_id, true), document, presentation, types,
            "Node lock must be enabled");
    auto blocked_move = commands.Execute(
        std::make_unique<MoveNodesCommand>(
            graph, MoveNodesCommand::Positions{{source_id, presentation.FindNode(source_id)->position}},
            MoveNodesCommand::Positions{{source_id, {50.0f, 50.0f}}}),
        document, presentation, types);
    Expect(!blocked_move && blocked_move.error().code == ErrorCode::Locked,
           "Locked nodes must reject presentation edits");
    auto semantic_on_locked = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "allowed", PropertyValue{std::int64_t{1}}), document,
        presentation, types);
    Expect(semantic_on_locked.has_value(), "Presentation locks must not turn semantic data read-only");
    Execute(commands, std::make_unique<SetNodeLockedCommand>(source_id, false), document, presentation, types,
            "Node lock must be disabled");

    Execute(commands, std::make_unique<SetLinkReadOnlyCommand>(graph, reconnect_link, true), document, presentation,
            types, "Link read-only must be enabled");
    auto blocked_link_delete = commands.Execute(
        std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{}, std::vector<LinkId>{reconnect_link}),
        document, presentation, types);
    Expect(!blocked_link_delete && blocked_link_delete.error().code == ErrorCode::ReadOnly,
           "Read-only links must reject deletion");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               !document.FindLink(graph, reconnect_link)->read_only,
           "Link protection must be undoable");
    Execute(commands, std::make_unique<SetLinkLockedCommand>(reconnect_link, true), document, presentation, types,
            "Link lock must be enabled");
    auto blocked_route =
        commands.Execute(std::make_unique<SetLinkRoutePointsCommand>(reconnect_link, std::vector<RoutePoint>{}),
                         document, presentation, types);
    Expect(!blocked_route && blocked_route.error().code == ErrorCode::Locked, "Locked links must reject route edits");
    Execute(commands, std::make_unique<SetLinkLockedCommand>(reconnect_link, false), document, presentation, types,
            "Link lock must be disabled");

    const GroupId locked_group = presentation.AllocateGroupId();
    Execute(commands,
            std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = locked_group,
                .graph = graph,
                .members = {source_id},
            }),
            document, presentation, types, "Lockable group must be added");
    Execute(commands, std::make_unique<SetGroupLockedCommand>(locked_group, true), document, presentation, types,
            "Group lock must be enabled");
    auto blocked_group =
        commands.Execute(std::make_unique<SetGroupStyleCommand>(locked_group, GroupStyle{.title = "Blocked"}), document,
                         presentation, types);
    Expect(!blocked_group && blocked_group.error().code == ErrorCode::Locked,
           "Locked groups must reject persisted edits");
    Execute(commands, std::make_unique<SetGroupLockedCommand>(locked_group, false), document, presentation, types,
            "Group lock must be disabled");

    Execute(commands, std::make_unique<SetGraphReadOnlyCommand>(graph, true), document, presentation, types,
            "Graph read-only must be enabled");
    auto blocked_graph_move = commands.Execute(std::make_unique<ResizeNodeCommand>(source_id, Vec2{250.0f, 100.0f}),
                                               document, presentation, types);
    Expect(!blocked_graph_move && blocked_graph_move.error().code == ErrorCode::ReadOnly,
           "Read-only graphs must reject semantic and presentation mutations");
    Execute(commands, std::make_unique<SetGraphReadOnlyCommand>(graph, false), document, presentation, types,
            "Graph read-only must be disabled explicitly");

    const GraphId child_graph_id = document.AllocateGraphId();
    const NodeId child_node = document.AllocateNodeId();
    Graph child_graph{.id = child_graph_id};
    child_graph.nodes.emplace(child_node, NodeInstance{
                                              .id = child_node,
                                              .type = TypeId{"stage3.child"},
                                          });
    Execute(commands, std::make_unique<AddGraphCommand>(std::move(child_graph)), document, presentation, types,
            "COW rollback graph must be added");
    const Graph child_before = *document.FindGraph(child_graph_id);
    commands.Clear();
    auto rolled_back_graph =
        commands.Execute(std::make_unique<RemoveGraphThenFailCommand>(child_graph_id), document, presentation, types);
    Expect(!rolled_back_graph && rolled_back_graph.error().code == ErrorCode::CommandFailed &&
               document.FindGraph(child_graph_id) != nullptr && *document.FindGraph(child_graph_id) == child_before,
           "Failed graph removal must not move from shared COW state before commit");

    GroupMemberSet cross_graph_members{child_node};
    auto cross_graph_group =
        commands.Execute(std::make_unique<SetGroupMembersCommand>(locked_group, std::move(cross_graph_members)),
                         document, presentation, types);
    Expect(!cross_graph_group && cross_graph_group.error().code == ErrorCode::InvalidArgument &&
               presentation.FindGroup(locked_group)->graph == graph,
           "Group graph ownership must be immutable");
}

void TestCompoundAtomicityAndRevisionConflict() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack first;
    CommandStack second;
    const GraphId graph = document.RootGraph();
    const NodeId node = document.AllocateNodeId();
    Execute(first,
            std::make_unique<AddNodeCommand>(
                graph, NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"compound"}}}),
            document, presentation, types, "Compound fixture node must be added");
    first.Clear();

    const Revisions before{document.ModelRevision(), presentation.PresentationRevision()};
    std::vector<std::unique_ptr<Command>> failing_children;
    failing_children.push_back(std::make_unique<ResizeNodeCommand>(node, Vec2{200.0f, 100.0f}));
    failing_children.push_back(
        std::make_unique<SetNodePropertyCommand>(graph, node, "atomic", PropertyValue{std::string{"set"}}));
    failing_children.push_back(std::make_unique<AddDynamicPinCommand>(graph,
                                                                      PinInstance{
                                                                          .id = document.AllocatePinId(),
                                                                          .node = node,
                                                                          .key = "invalid",
                                                                          .storage = PinStorage::Dynamic,
                                                                      },
                                                                      0));
    auto failed = first.Execute(std::make_unique<CompoundCommand>("Fail atomically", std::move(failing_children)),
                                document, presentation, types);
    Expect(!failed, "Invalid compound command must fail");
    Expect(!document.FindNode(graph, node)->properties.contains("atomic") &&
               presentation.FindNode(node)->size == Vec2{} &&
               Revisions{document.ModelRevision(), presentation.PresentationRevision()} == before,
           "Failed compound command must roll back both state and revisions");
    Expect(!first.CanUndo(), "Failed compound command must not enter history");

    LinkId allocated_during_apply;
    auto allocation_conflict =
        first.Execute(std::make_unique<AllocateDuringApplyCommand>(document, allocated_during_apply, graph, node),
                      document, presentation, types);
    Expect(!allocation_conflict && allocation_conflict.error().code == ErrorCode::RevisionConflict,
           "ID allocation during Apply must invalidate the transaction snapshot");
    Expect(!document.FindNode(graph, node)->properties.contains("allocation"),
           "Allocation conflict must not commit staged model changes");
    const LinkId allocated_after_failure = document.AllocateLinkId();
    Expect(allocated_during_apply && allocated_after_failure && allocated_after_failure != allocated_during_apply,
           "A failed transaction must not roll back externally reserved IDs");

    const GraphId child = document.AllocateGraphId();
    std::vector<std::unique_ptr<Command>> children;
    children.push_back(std::make_unique<AddGraphCommand>(child));
    children.push_back(std::make_unique<SetRootGraphCommand>(child));
    auto compound = Execute(first, std::make_unique<CompoundCommand>("Create root graph", std::move(children)),
                            document, presentation, types, "Valid compound command must execute");
    Expect(compound.model_changed && compound.revisions.model == before.model + 1,
           "Compound command must commit one model revision");
    Expect(first.UndoName() == "Create root graph", "Compound command must use one history entry");
    Expect(first.Undo(document, presentation, types).has_value(), "Compound undo must execute");
    Expect(document.RootGraph() == graph && document.FindGraph(child) == nullptr,
           "Compound undo must revert children in reverse order");

    first.Clear();
    Execute(first, std::make_unique<SetNodePropertyCommand>(graph, node, "redo", PropertyValue{std::string{"value"}}),
            document, presentation, types, "Redo fixture command must execute");
    Expect(first.Undo(document, presentation, types).has_value() && first.CanRedo(), "Redo fixture command must undo");
    auto no_op = first.Execute(std::make_unique<SetNodePropertyCommand>(graph, node, "redo", std::nullopt), document,
                               presentation, types);
    Expect(no_op && !no_op->model_changed && !no_op->presentation_changed && first.CanRedo(),
           "A successful no-op must preserve redo history and stay out of undo "
           "history");
    auto invalid_no_op = first.Execute(std::make_unique<SetNodePropertyCommand>(graph, node, "", std::nullopt),
                                       document, presentation, types);
    Expect(!invalid_no_op && invalid_no_op.error().code == ErrorCode::InvalidArgument && first.CanRedo(),
           "Invalid property keys must be rejected before no-op detection");
    std::vector<std::unique_ptr<Command>> semantic_no_op_children;
    semantic_no_op_children.push_back(
        std::make_unique<SetNodePropertyCommand>(graph, node, "temporary", PropertyValue{std::string{"value"}}));
    semantic_no_op_children.push_back(std::make_unique<SetNodePropertyCommand>(graph, node, "temporary", std::nullopt));
    auto semantic_no_op =
        first.Execute(std::make_unique<CompoundCommand>("Semantic no-op", std::move(semantic_no_op_children)), document,
                      presentation, types);
    Expect(semantic_no_op && !semantic_no_op->model_changed && !semantic_no_op->presentation_changed && first.CanRedo(),
           "A compound command with no net state change must preserve redo history");
    Expect(first.Redo(document, presentation, types).has_value(), "Redo must remain available after a no-op command");

    first.Clear();
    Execute(first,
            std::make_unique<SetNodePropertyCommand>(
                graph, node, "continuous", PropertyValue{1.0},
                SetNodePropertyCommand::Edit{.merge_key = 42, .begin = true, .final = false}),
            document, presentation, types, "Continuous edit must begin");
    Execute(first,
            std::make_unique<SetNodePropertyCommand>(graph, node, "continuous", PropertyValue{2.0},
                                                     SetNodePropertyCommand::Edit{.merge_key = 42, .final = false}),
            document, presentation, types, "Continuous edit must update");
    auto finalized = first.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, node, "continuous", PropertyValue{2.0},
                                                 SetNodePropertyCommand::Edit{.merge_key = 42, .final = true}),
        document, presentation, types);
    Expect(finalized && !finalized->model_changed, "Continuous edit finalization may be a semantic no-op");
    Expect(first.Undo(document, presentation, types).has_value() &&
               !document.FindNode(graph, node)->properties.contains("continuous") && !first.CanUndo(),
           "Continuous property edits must coalesce into one undo entry");
    Expect(first.Redo(document, presentation, types).has_value() &&
               std::get<double>(document.FindNode(graph, node)->properties.at("continuous")) == 2.0,
           "Coalesced property edit redo must apply the final gesture value");
    Expect(first.Undo(document, presentation, types).has_value(),
           "Coalesced property edit must remain undoable after redo");

    first.Clear();
    Execute(first, std::make_unique<SetNodePropertyCommand>(graph, node, "first", PropertyValue{std::string{"1"}}),
            document, presentation, types, "First stack command must execute");
    Execute(second, std::make_unique<SetNodePropertyCommand>(graph, node, "second", PropertyValue{std::string{"2"}}),
            document, presentation, types, "Second stack command must execute");
    auto stale = first.Undo(document, presentation, types);
    Expect(!stale && stale.error().code == ErrorCode::RevisionConflict,
           "A stack must reject undo after an external committed revision");
    Expect(document.FindNode(graph, node)->properties.contains("first") &&
               document.FindNode(graph, node)->properties.contains("second"),
           "Rejected stale undo must preserve current state");
}

void TestTransactionCopyOnWrite() {
    NodeMap move_source;
    const NodeId moved_node{2};
    move_source.emplace(moved_node, NodeInstance{.id = moved_node, .type = TypeId{"cow.moved"}});
    NodeMap move_target = std::move(move_source);
    Expect(move_source.contains(moved_node) && move_target.contains(moved_node),
           "Moved COW maps must remain valid shared snapshots");

    NodeMap editable;
    const NodeId direct_node{1};
    editable.emplace(direct_node, NodeInstance{
                                      .id = direct_node,
                                      .type = TypeId{"cow.direct"},
                                      .display_name = "Before",
                                  });
    NodeMap snapshot = editable;
    NodeInstance changed = editable.at(direct_node);
    changed.display_name = "After";
    ResetTransactionMetrics();
    editable.insert_or_assign(direct_node, std::move(changed));
    const auto direct_metrics = GetTransactionMetrics();
    Expect(editable.at(direct_node).display_name == "After" && snapshot.at(direct_node).display_name == "Before",
           "Entity replacement must preserve COW snapshot isolation");
    Expect(direct_metrics.node_maps.root_clones == 1 && direct_metrics.node_maps.directory_clones == 1 &&
               direct_metrics.node_maps.shard_clones == 1 && direct_metrics.node_maps.page_clones == 0 &&
               direct_metrics.node_maps.value_clones == 1 && direct_metrics.node_maps.copied_handles <= 256,
           "Entity replacement must clone one bounded shard path and one entity");

    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands;
    const GraphId root = document.RootGraph();
    const GraphId untouched = document.AllocateGraphId();
    Execute(commands, std::make_unique<AddGraphCommand>(untouched), document, presentation, types,
            "Second graph must be added");
    const NodeId node = document.AllocateNodeId();
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(root, NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"cow"}}}),
        document, presentation, types, "COW fixture node must be added");

    ResetTransactionMetrics();
    Execute(commands, std::make_unique<SetNodePropertyCommand>(root, node, "value", PropertyValue{std::int64_t{1}}),
            document, presentation, types, "COW property edit must execute");
    const auto property_metrics = GetTransactionMetrics();
    Expect(property_metrics.graphs.root_clones == 1 && property_metrics.graphs.directory_clones == 1 &&
               property_metrics.graphs.shard_clones == 1 && property_metrics.graphs.value_clones == 1 &&
               property_metrics.graph_revisions.root_clones == 1 && property_metrics.node_maps.root_clones == 1 &&
               property_metrics.node_maps.directory_clones == 1 && property_metrics.node_maps.shard_clones == 1 &&
               property_metrics.node_maps.value_clones == 1 && property_metrics.node_maps.copied_handles <= 256 &&
               property_metrics.pin_maps.logical_bytes == 0 && property_metrics.link_maps.logical_bytes == 0 &&
               property_metrics.node_presentations.logical_bytes == 0 && property_metrics.journal_entries == 1 &&
               property_metrics.copied_logical_bytes > 0,
           "A model edit must clone only bounded graph and node COW paths");

    ResetTransactionMetrics();
    Execute(commands, std::make_unique<ResizeNodeCommand>(node, Vec2{220.0f, 120.0f}), document, presentation, types,
            "COW presentation edit must execute");
    const auto presentation_metrics = GetTransactionMetrics();
    Expect(presentation_metrics.graphs.logical_bytes == 0 && presentation_metrics.graph_revisions.logical_bytes == 0 &&
               presentation_metrics.node_presentations.root_clones == 1 &&
               presentation_metrics.node_presentations.directory_clones == 1 &&
               presentation_metrics.node_presentations.shard_clones == 1 &&
               presentation_metrics.node_presentations.page_clones == 0 &&
               presentation_metrics.node_presentations.value_clones == 1 &&
               presentation_metrics.node_presentations.copied_handles <= 256 &&
               presentation_metrics.link_presentations.logical_bytes == 0 &&
               presentation_metrics.groups.logical_bytes == 0 && presentation_metrics.full_structure_validations == 0 &&
               presentation_metrics.incremental_records_validated == 1 && presentation_metrics.journal_entries == 1,
           "A presentation edit must clone only one bounded presentation shard "
           "path");

    GraphDocument many_graphs;
    GraphPresentation many_presentation;
    CommandStack many_commands;
    many_commands.SetHistoryLimit(0);
    std::vector<Graph> additions;
    additions.reserve(10'000);
    for (std::size_t index = 0; index < 10'000; ++index) {
        additions.push_back(Graph{
            .id = many_graphs.AllocateGraphId(),
            .display_name = "Graph " + std::to_string(index),
        });
    }
    Execute(many_commands, std::make_unique<BulkGraphsCommand>(std::move(additions)), many_graphs, many_presentation,
            types, "Large graph table fixture must execute");
    const GraphId many_root = many_graphs.RootGraph();
    const NodeId many_node = many_graphs.AllocateNodeId();
    Execute(many_commands,
            std::make_unique<AddNodeCommand>(
                many_root, NodeCreation{.node = NodeInstance{.id = many_node, .type = TypeId{"many.graphs"}}}),
            many_graphs, many_presentation, types, "Large graph table node must execute");
    const GraphDocumentSnapshot held_snapshot = CaptureGraphDocumentSnapshot(many_graphs);
    ResetTransactionMetrics();
    Execute(many_commands,
            std::make_unique<SetNodePropertyCommand>(many_root, many_node, "value", PropertyValue{std::int64_t{1}}),
            many_graphs, many_presentation, types, "Large graph table property edit must execute");
    const auto many_metrics = GetTransactionMetrics();
    Expect(many_metrics.graphs.root_clones == 1 && many_metrics.graphs.directory_clones == 1 &&
               many_metrics.graphs.shard_clones == 1 && many_metrics.graphs.value_clones == 1 &&
               many_metrics.graphs.copied_handles <= 512 && held_snapshot.FindNode(many_root, many_node) != nullptr &&
               !held_snapshot.FindNode(many_root, many_node)->properties.contains("value"),
           "A single edit in 10k graphs must clone one bounded graph path and "
           "preserve snapshots");
}

void TestStageThreeCommandsFragmentsAndLayout() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    RegistryCatalog& types = registry;
    CommandStack commands;
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value(), "Stage-three source must register");
    Expect(registry.RegisterNodeType(SinkDescriptor()).has_value(), "Stage-three sink must register");
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"test.bidirectional"},
                   .display_name = "Bidirectional",
                   .category = "Tests",
                   .static_pins =
                       {
                           PinDescriptor{.key = "input", .label = "Input", .type = TypeId{"float"}},
                           PinDescriptor{
                               .key = "output",
                               .label = "Output",
                               .type = TypeId{"float"},
                               .direction = PinDirection::Output,
                               .cardinality = PinCardinality::Multiple,
                           },
                       },
               })
               .has_value(),
           "Stage-three bidirectional descriptor must register");

    auto source = registry.Instantiate(document, TypeId{"test.source"});
    auto first_sink = registry.Instantiate(document, TypeId{"test.sink"});
    auto second_sink = registry.Instantiate(document, TypeId{"test.sink"});
    Expect(source && first_sink && second_sink, "Stage-three fixture nodes must instantiate");
    const GraphId graph = document.RootGraph();
    const NodeId source_id = source->node.id;
    const NodeId first_sink_id = first_sink->node.id;
    const NodeId second_sink_id = second_sink->node.id;
    const PinId output = source->pins.front().id;
    const PinId first_input = first_sink->pins.front().id;
    const PinId second_input = second_sink->pins.front().id;
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*source), NodePresentation{.position = {10.0f, 20.0f}}),
            document, presentation, types, "Stage-three source must be added");
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(graph, std::move(*first_sink), NodePresentation{.position = {300.0f, 20.0f}}),
        document, presentation, types, "Stage-three first sink must be added");
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*second_sink),
                                             NodePresentation{.position = {300.0f, 220.0f}}),
            document, presentation, types, "Stage-three second sink must be added");

    const LinkId link = document.AllocateLinkId();
    Execute(commands,
            std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = first_input}),
            document, presentation, types, "Stage-three link must connect");
    const RoutePointId first_point = presentation.AllocateRoutePointId();
    Execute(commands, std::make_unique<InsertRoutePointCommand>(link, RoutePoint{first_point, {160.0f, 40.0f}}, 0),
            document, presentation, types, "Route point insert must execute");
    Execute(commands, std::make_unique<MoveRoutePointCommand>(link, first_point, Vec2{170.0f, 50.0f}), document,
            presentation, types, "Route point move must execute");
    Expect(presentation.FindRoutePoint(link, first_point) != nullptr &&
               presentation.FindRoutePoint(link, first_point)->position == Vec2{170.0f, 50.0f},
           "Route point move must preserve stable identity");
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, OrthogonalLinkRouterType()), document, presentation,
            types, "Reconnect router selection must be configured");

    commands.Clear();
    Execute(commands, std::make_unique<ReconnectLinkCommand>(graph, link, output, second_input), document, presentation,
            types, "Reconnect must execute");
    Expect(document.FindLink(graph, link) != nullptr && document.FindLink(graph, link)->input == second_input &&
               presentation.FindLink(link) != nullptr && presentation.FindLink(link)->Route().empty() &&
               presentation.FindLink(link)->Style().router == OrthogonalLinkRouterType(),
           "Reconnect must retain the link ID and router while resetting stale "
           "waypoints");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindLink(graph, link)->input == first_input &&
               presentation.FindRoutePoint(link, first_point) != nullptr,
           "Reconnect undo must restore endpoints and route atomically");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               document.FindLink(graph, link)->input == second_input,
           "Reconnect redo must restore the replacement endpoint");

    commands.Clear();
    auto rejected_reconnect = commands.Execute(
        std::make_unique<ReconnectLinkCommand>(graph, link, first_input, second_input), document, presentation, types);
    Expect(!rejected_reconnect && document.FindLink(graph, link)->output == output &&
               document.FindLink(graph, link)->input == second_input,
           "Rejected reconnect must leave the original link intact");

    const RoutePointId second_point = presentation.AllocateRoutePointId();
    Execute(commands, std::make_unique<InsertRoutePointCommand>(link, RoutePoint{second_point, {220.0f, 160.0f}}, 0),
            document, presentation, types, "Second route point insert must execute");
    Execute(commands, std::make_unique<RemoveRoutePointsCommand>(std::vector<RoutePointRef>{{link, second_point}}),
            document, presentation, types, "Route point remove must execute");
    Expect(presentation.FindRoutePoint(link, second_point) == nullptr,
           "Route point remove must erase only the selected point");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               presentation.FindRoutePoint(link, second_point) != nullptr,
           "Route point remove undo must restore stable identity");
    auto mixed_route_remove = commands.Execute(std::make_unique<RemoveRoutePointsCommand>(std::vector<RoutePointRef>{
                                                   {link, second_point},
                                                   {link, RoutePointId{999999}},
                                               }),
                                               document, presentation, types);
    Expect(!mixed_route_remove && presentation.FindRoutePoint(link, second_point) != nullptr,
           "Route point removal must reject a mixed valid/invalid selection "
           "atomically");

    const GroupId group = presentation.AllocateGroupId();
    Execute(commands,
            std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = group,
                .graph = graph,
                .style = MakeGroupStyle(GroupStyle{
                    .title = "Stage three",
                    .body = "Interactive comment body",
                    .kind = GroupKind::Comment,
                }),
                .members = {source_id, second_sink_id},
            }),
            document, presentation, types, "Interactive comment group must be added");
    commands.Clear();
    Execute(commands, std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{second_sink_id}), document,
            presentation, types, "Grouped node delete must execute");
    Expect(presentation.FindGroup(group)->members == std::vector<NodeId>{source_id},
           "Deleting a node must remove it from explicit group membership");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               presentation.FindGroup(group)->members == std::vector<NodeId>{source_id, second_sink_id},
           "Grouped node delete undo must restore membership");

    GraphSelection selection{
        .graph = graph,
        .groups = {group},
    };
    auto fragment = CaptureGraphFragment(document, presentation, selection);
    Expect(fragment && fragment->nodes.size() == 2 && fragment->links.size() == 1 && fragment->groups.size() == 1,
           "Capturing a group must include its members and internal links");
    GraphFragment invalid_fragment = *fragment;
    invalid_fragment.links.front().link.input = PinId{999999};
    const NodeId allocation_probe = document.AllocateNodeId();
    Expect(!PrepareGraphFragmentPaste(document, presentation, types, invalid_fragment, graph, Vec2{500.0f, 200.0f}),
           "Fragment preflight must reject dangling endpoints without throwing");
    const NodeId allocation_after_rejection = document.AllocateNodeId();
    Expect(allocation_after_rejection.Value() == allocation_probe.Value() + 1,
           "Rejected fragment preflight must not consume IDs");

    GraphFragment incompatible_fragment = *fragment;
    for (auto& node : incompatible_fragment.nodes) {
        for (auto& pin : node.creation.pins) {
            if (pin.direction == PinDirection::Input) pin.type = TypeId{"incompatible"};
        }
    }
    auto incompatible_prepared =
        PrepareGraphFragmentPaste(document, presentation, types, incompatible_fragment, graph, Vec2{500.0f, 200.0f});
    Expect(!incompatible_prepared, "Fragment preflight must reject incompatible "
                                   "internal links before allocation");
    auto prepared = PrepareGraphFragmentPaste(document, presentation, types, *fragment, graph, Vec2{600.0f, 300.0f});
    Expect(prepared && prepared->remap.nodes.size() == 2 && prepared->remap.pins.size() == 2 &&
               prepared->remap.links.size() == 1 && prepared->remap.groups.size() == 1 &&
               prepared->remap.route_points.size() == 1,
            "Preparing paste must remap every stable fragment identity");
    const auto remap = prepared->remap;
    PreparedGraphFragment malformed_prepared = *prepared;
    malformed_prepared.prepared_descriptors.push_back({});
    auto malformed_paste = commands.Execute(
        std::make_unique<PasteGraphFragmentCommand>(std::move(malformed_prepared)), document, presentation, types);
    Expect(!malformed_paste && malformed_paste.error().code == ErrorCode::InvalidArgument,
           "Malformed prepared fragments must reject empty descriptor provenance without dereferencing it");
    commands.Clear();
    Execute(commands, std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared)), document, presentation, types,
            "Fragment paste must execute");
    const NodeId pasted_source = remap.nodes.at(source_id);
    const GroupId pasted_group = remap.groups.at(group);
    Expect(document.FindNode(graph, pasted_source) != nullptr && presentation.FindGroup(pasted_group) != nullptr &&
               presentation.FindGroup(pasted_group)->members.size() == 2 &&
               document.FindLink(graph, remap.links.at(link)) != nullptr,
           "Paste must commit remapped nodes, links, routes, and groups atomically");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindNode(graph, pasted_source) == nullptr && presentation.FindGroup(pasted_group) == nullptr,
           "Paste undo must remove the entire fragment in one history entry");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               document.FindNode(graph, pasted_source) != nullptr,
           "Paste redo must restore the entire prepared fragment");

    auto stale_prepared =
        PrepareGraphFragmentPaste(document, presentation, types, *fragment, graph, Vec2{800.0f, 300.0f});
    NodeTypeDescriptor source_v2 = SourceDescriptor();
    source_v2.version = 2;
    Expect(stale_prepared && registry.ReplaceNodeType(source_v2).has_value(),
           "Stale fragment descriptor fixture must prepare");
    auto stale_paste = commands.Execute(std::make_unique<PasteGraphFragmentCommand>(std::move(*stale_prepared)),
                                        document, presentation, types);
    Expect(!stale_paste && stale_paste.error().code == ErrorCode::InvalidGraph &&
               registry.ReplaceNodeType(SourceDescriptor()).has_value(),
           "Prepared fragment apply must reject a node schema replaced after "
           "preparation");

    const std::vector<NodeId> align_nodes{source_id, first_sink_id};
    auto aligned = ComputeNodeAlignment(document, presentation, graph, align_nodes, NodeAlignment::Top);
    Expect(aligned && aligned->after.at(source_id).y == aligned->after.at(first_sink_id).y,
           "Alignment must compute a deterministic shared edge");
    const std::vector<NodeId> layout_nodes{source_id, second_sink_id};
    auto automatic = ComputeAutoLayout(document, presentation, graph, layout_nodes, LayoutOptions{});
    Expect(automatic && automatic->after.size() == layout_nodes.size() &&
               automatic->after.at(source_id).x < automatic->after.at(second_sink_id).x,
           "Auto-layout must follow graph direction deterministically");

    auto cycle_a = registry.Instantiate(document, TypeId{"test.bidirectional"});
    auto cycle_b = registry.Instantiate(document, TypeId{"test.bidirectional"});
    auto cycle_c = registry.Instantiate(document, TypeId{"test.bidirectional"});
    Expect(cycle_a && cycle_b && cycle_c, "Cycle layout nodes must instantiate");
    const NodeId cycle_a_id = cycle_a->node.id;
    const NodeId cycle_b_id = cycle_b->node.id;
    const NodeId cycle_c_id = cycle_c->node.id;
    const PinId cycle_a_input = cycle_a->pins[0].id;
    const PinId cycle_a_output = cycle_a->pins[1].id;
    const PinId cycle_b_input = cycle_b->pins[0].id;
    const PinId cycle_b_output = cycle_b->pins[1].id;
    const PinId cycle_c_input = cycle_c->pins[0].id;
    const PinId cycle_c_output = cycle_c->pins[1].id;
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*cycle_a), NodePresentation{.position = {0.0f, 600.0f}}),
            document, presentation, types, "Cycle A must be added");
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(graph, std::move(*cycle_b), NodePresentation{.position = {200.0f, 600.0f}}),
        document, presentation, types, "Cycle B must be added");
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(graph, std::move(*cycle_c), NodePresentation{.position = {400.0f, 600.0f}}),
        document, presentation, types, "Cycle C must be added");
    const auto connect_cycle = [&](const PinId from, const PinId to, const char* message) {
        Execute(commands,
                std::make_unique<ConnectPinsCommand>(
                    graph, Link{.id = document.AllocateLinkId(), .output = from, .input = to}),
                document, presentation, types, message);
    };
    connect_cycle(cycle_a_output, cycle_b_input, "Cycle A to B must connect");
    connect_cycle(cycle_b_output, cycle_a_input, "Cycle B to A must connect");
    connect_cycle(cycle_b_output, cycle_c_input, "Cycle B to C must connect");
    connect_cycle(cycle_c_output, first_input, "Cycle C to sink must connect");
    const std::vector<NodeId> cycle_nodes{cycle_a_id, cycle_b_id, cycle_c_id, first_sink_id};
    auto cycle_layout = ComputeAutoLayout(document, presentation, graph, cycle_nodes, LayoutOptions{});
    Expect(cycle_layout && cycle_layout->after.at(cycle_a_id).x == cycle_layout->after.at(cycle_b_id).x &&
               cycle_layout->after.at(cycle_c_id).x > cycle_layout->after.at(cycle_a_id).x &&
               cycle_layout->after.at(first_sink_id).x > cycle_layout->after.at(cycle_c_id).x,
           "Auto-layout must preserve downstream levels after a strongly "
           "connected component");
    Expect(ValidateGraph(document, graph, registry).empty(), "Stage-three graph must remain valid");
}

void TestStageThreeEditorGestures() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    NodeUiRegistry ui;
    LinkRouterRegistry routers;
    RegistryCatalog& types = registry;
    CommandStack commands;
    Expect(registry.RegisterNodeType(SourceDescriptor()).has_value(), "Gesture source must register");
    Expect(registry.RegisterNodeType(SinkDescriptor()).has_value(), "Gesture sink must register");

    ImVec2 source_body_min;
    ImVec2 sink_body_min;
    Expect(ui.Register(NodeUiDescriptor{
                           .type = TypeId{"test.source"},
                           .draw_body =
                               [&](NodeUiContext&) {
                                   source_body_min = ImGui::GetCursorScreenPos();
                                   ImGui::Dummy({1.0f, 1.0f});
                               },
                       })
               .has_value(),
           "Gesture source UI must register");
    Expect(ui.Register(NodeUiDescriptor{
                           .type = TypeId{"test.sink"},
                           .draw_body =
                               [&](NodeUiContext&) {
                                   sink_body_min = ImGui::GetCursorScreenPos();
                                   ImGui::Dummy({1.0f, 1.0f});
                               },
                       })
               .has_value(),
           "Gesture sink UI must register");
    auto source = registry.Instantiate(document, TypeId{"test.source"});
    auto sink = registry.Instantiate(document, TypeId{"test.sink"});
    Expect(source && sink, "Gesture nodes must instantiate");
    const GraphId graph = document.RootGraph();
    const NodeId source_id = source->node.id;
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*source), NodePresentation{.position = {20.0f, 20.0f}}),
            document, presentation, types, "Gesture source must be added");
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*sink), NodePresentation{.position = {340.0f, 20.0f}}),
            document, presentation, types, "Gesture sink must be added");
    const LinkId link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = input}),
            document, presentation, types, "Gesture link must connect");
    const RoutePointId point = presentation.AllocateRoutePointId();
    Execute(commands, std::make_unique<InsertRoutePointCommand>(link, RoutePoint{point, {260.0f, 140.0f}}, 0), document,
            presentation, types, "Gesture route point must be added");
    const GroupId group = presentation.AllocateGroupId();
    Execute(commands,
            std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = group,
                .graph = graph,
                .geometry =
                    GroupGeometry{
                        .position = {20.0f, 220.0f},
                        .size = {280.0f, 120.0f},
                    },
                .style = MakeGroupStyle(GroupStyle{.title = "Gesture group"}),
                .members = {source_id},
            }),
            document, presentation, types, "Gesture group must be added");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Expect(pixels != nullptr, "Gesture test font atlas must build");

    EditorContext editor;
    EditorConfig config;
    config.snap_to_grid = false;
    config.show_minimap = false;
    const auto draw_frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Stage-three gestures", nullptr, ImGuiWindowFlags_NoDecoration);
        const auto result =
            DrawEditor(editor, document, presentation, commands, registry, ui, routers, {640.0f, 480.0f}, {}, config);
        ImGui::End();
        ImGui::Render();
        return result;
    };
    const auto click = [&](const ImVec2 position) {
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        auto result = draw_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        const auto released = draw_frame();
        result.model_changed |= released.model_changed;
        result.presentation_changed |= released.presentation_changed;
        result.selection_changed |= released.selection_changed;
        return result;
    };
    const auto drag = [&](const ImVec2 start, const ImVec2 end) {
        io.AddMousePosEvent(start.x, start.y);
        (void)draw_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        (void)draw_frame();
        io.AddMousePosEvent(end.x, end.y);
        (void)draw_frame();
        (void)draw_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        return draw_frame();
    };

    (void)draw_frame();
    const float header_height = config.node_header.minimum_height;
    const ImVec2 source_min = source_body_min - ImVec2{8.0f, header_height + 8.0f};
    const ImVec2 canvas_origin = source_min - ImVec2{20.0f, 20.0f};
    const ImVec2 collapse_control =
        source_min + ImVec2{config.node_width - (config.node_header.collapse_width + 2.0f) * 0.5f,
                            header_height * 0.5f};
    const auto collapsed = click(collapse_control);
    Expect(collapsed.presentation_changed && presentation.FindNode(source_id)->collapsed,
           "Node collapse control must commit through the command stack");
    (void)click(collapse_control);
    Expect(!presentation.FindNode(source_id)->collapsed, "Node collapse control must expand a collapsed node");
    config.enable_node_collapse = false;
    (void)click(collapse_control);
    Expect(!presentation.FindNode(source_id)->collapsed, "Disabled node collapse must turn the former control area into ordinary header input");
    config.enable_node_collapse = true;

    const float source_height = header_height + config.pin_spacing + 10.0f;
    const ImVec2 resize_handle = source_min + ImVec2{config.node_width - 2.0f, source_height - 2.0f};
    const auto resized = drag(resize_handle, resize_handle + ImVec2{52.0f, 38.0f});
    Expect(resized.presentation_changed && presentation.FindNode(source_id)->size == Vec2{242.0f, source_height + 38.0f},
           "Node resize gesture must persist the resolved logical size");

    editor.SetSelection(GraphSelection{
        .graph = graph,
        .route_points = {{link, point}},
    });
    const ImVec2 route_start = canvas_origin + ImVec2{260.0f, 140.0f};
    const auto route_moved = drag(route_start, route_start + ImVec2{24.0f, 32.0f});
    Expect(route_moved.presentation_changed &&
               presentation.FindRoutePoint(link, point)->position == Vec2{284.0f, 172.0f},
           "Reroute handle drag must preserve identity and persist its position");

    const ImVec2 group_start = canvas_origin + ImVec2{50.0f, 235.0f};
    const auto group_moved = drag(group_start, group_start + ImVec2{30.0f, 20.0f});
    Expect(group_moved.presentation_changed &&
               presentation.FindGroup(group)->geometry.position == Vec2{50.0f, 240.0f} &&
               presentation.FindNode(source_id)->position == Vec2{50.0f, 40.0f},
           "Group drag must atomically move the group and explicit members");

    Execute(commands, std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{}, std::vector<LinkId>{link}),
            document, presentation, types, "Gesture link delete must execute");
    (void)draw_frame();
    const ImVec2 current_source_min = source_body_min - ImVec2{8.0f, header_height + 8.0f};
    const float sink_input_gutter = ImGui::CalcTextSize("Value").x + 18.0f;
    const ImVec2 current_sink_min = sink_body_min - ImVec2{sink_input_gutter + 8.0f, header_height + 8.0f};
    const float pin_y = header_height + config.pin_spacing * 0.5f;
    const ImVec2 output_position = current_source_min + ImVec2{242.0f, pin_y};
    const ImVec2 input_position = current_sink_min + ImVec2{0.0f, pin_y};
    const auto reconnected = drag(output_position, input_position + ImVec2{12.0f, 8.0f});
    Expect(reconnected.model_changed && document.FindGraph(graph)->links.size() == 1,
           "Link drag must magnetically acquire a nearby compatible pin after "
           "deletion");

    const ImVec2 outside_drag_start =
        source_body_min - ImVec2{8.0f, header_height + 8.0f} + ImVec2{30.0f, header_height * 0.5f};
    const auto removed_from_group = drag(outside_drag_start, outside_drag_start + ImVec2{24.0f, 0.0f});
    Expect(removed_from_group.presentation_changed && presentation.FindGroup(group)->members.empty(),
           "Dragging a member outside every group must remove its membership");
    const Vec2 before_group_drop = presentation.FindNode(source_id)->position;
    const ImVec2 inside_drag_start =
        source_body_min - ImVec2{8.0f, header_height + 8.0f} + ImVec2{30.0f, header_height * 0.5f};
    const ImVec2 group_drop = canvas_origin + ImVec2{90.0f, 255.0f};
    const auto added_to_group = drag(inside_drag_start, group_drop);
    Expect(added_to_group.presentation_changed &&
               presentation.FindGroup(group)->members == std::vector<NodeId>{source_id},
           "Dragging a node into a group must add explicit membership");
    Expect(commands.Undo(document, presentation, types).has_value() && presentation.FindGroup(group)->members.empty() &&
               presentation.FindNode(source_id)->position == before_group_drop,
           "A single undo must restore both node position and group membership");

    ImGui::DestroyContext();
}

void TestNodeUiExtensibility() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog registry;
    NodeUiRegistry ui;
    LinkRouterRegistry routers;
    RegistryCatalog& types = registry;
    CommandStack commands;
    int body_calls = 0;
    int inspector_calls = 0;
    int pin_style_calls = 0;
    int header_state_calls = 0;
    int header_glyph_calls = 0;
    int duplicate_calls = 0;
    GraphSelection duplicated_selection;
    NodeId header_item_node;
    ImVec2 body_button_min;
    ImVec2 body_button_max;
    ImVec2 header_glyph_min;
    ImVec2 header_glyph_max;

    Expect(!registry.RegisterNodeType(NodeTypeDescriptor{
               .type = TypeId{"ui.invalid"},
               .display_name = "Invalid UI node",
               .default_properties =
                   {
                       {"nan", PropertyValue{std::numeric_limits<double>::quiet_NaN()}},
                   },
           }),
           "Node descriptors must reject non-finite typed defaults");
    Expect(registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"ui.test"},
                   .display_name = "UI test",
                   .static_pins =
                       {
                           PinDescriptor{
                               .key = "output",
                               .label = "Output",
                               .type = TypeId{"float"},
                               .direction = PinDirection::Output,
                               .cardinality = PinCardinality::Multiple,
                           },
                       },
                   .default_properties =
                       {
                           {"enabled", PropertyValue{false}},
                           {"count", PropertyValue{std::int64_t{7}}},
                           {"scale", PropertyValue{1.5}},
                           {"title", PropertyValue{std::string{"typed"}}},
                           {"offset", PropertyValue{Vec2{2.0f, 3.0f}}},
                           {"asset", PropertyValue{AssetReference{42}}},
                       },
                   .behavior = std::make_shared<const NodeBehavior>(NodeBehavior{
                       .validate =
                           [](const NodeInstance& node, std::span<const PinInstance>) {
                               const auto enabled = node.properties.find("enabled");
                               if (enabled == node.properties.end() || !std::holds_alternative<bool>(enabled->second)) {
                                   return std::vector<std::string>{"Property 'enabled' must be boolean"};
                               }
                               return std::vector<std::string>{};
                           },
                   }),
               })
               .has_value(),
           "Typed UI node descriptor must register");
    Expect(ui.RegisterHeaderGlyph(NodeHeaderGlyphDescriptor{
                  .id = "test.play",
                  .aspect_ratio = 1.0f,
                  .draw = [&](const NodeHeaderGlyphDrawContext& context) {
                      ++header_glyph_calls;
                      const ImVec2 min{context.min.x, context.min.y};
                      const ImVec2 max{context.max.x, context.max.y};
                      header_glyph_min = min;
                      header_glyph_max = max;
                      context.draw_list.AddTriangleFilled(
                          {min.x, min.y}, {min.x, max.y}, {max.x, (min.y + max.y) * 0.5f}, context.color);
                  },
              }).has_value(),
           "Custom header glyph must register independently");
    Expect(ui.Register(NodeUiDescriptor{
                           .type = TypeId{"ui.test"},
                           .draw_body =
                               [&](NodeUiContext& context) {
                                   ++body_calls;
                                   if (ImGui::Button("Enable custom node")) {
                                       context.SetProperty("enabled", PropertyValue{true});
                                   }
                                   body_button_min = ImGui::GetItemRectMin();
                                   body_button_max = ImGui::GetItemRectMax();
                               },
                           .draw_inspector =
                               [&](NodeUiContext& context) {
                                   ++inspector_calls;
                                   ImGui::TextUnformatted("Custom inspector widget");
                                   if (context.FindProperty("gain") == nullptr) {
                                       context.SetProperty("gain", PropertyValue{2.5});
                                   }
                                   const bool has_dynamic =
                                       std::ranges::any_of(context.Node().pins, [&](const PinId pin) {
                                           const auto* value = context.FindPin(pin);
                                           return value != nullptr && value->storage == PinStorage::Dynamic;
                                       });
                                   if (!has_dynamic) {
                                       (void)context.AddDynamicPin(PinDescriptor{
                                           .key = "dynamic_input",
                                           .label = "Dynamic input",
                                           .type = TypeId{"float"},
                                       });
                                       (void)context.AddDynamicPin(PinDescriptor{
                                           .key = "dynamic_second",
                                           .label = "Dynamic second",
                                           .type = TypeId{"float"},
                                       });
                                   }
                               },
                           .default_size = {260.0f, 120.0f},
                           .header_color = 0xFF664422U,
                           .resolve_header =
                               [&](const NodeHeaderContext& context) {
                                   ++header_state_calls;
                                   if (context.node.id != header_item_node) {
                                       return NodeHeaderPresentation{.lines = {"UI test", "runtime-id"}};
                                   }
                                   return NodeHeaderPresentation{
                                       .lines = {"UI test", "runtime-id"},
                                       .items = {
                                           NodeHeaderItem{
                                               .id = "state",
                                               .content = NodeHeaderGlyph{"test.play"},
                                               .active = true,
                                               .action = "inspect-runtime",
                                               .tooltip = "Inspect runtime state",
                                           },
                                           NodeHeaderItem{
                                               .id = "kind",
                                               .content = NodeHeaderBadge{"LIVE"},
                                           },
                                       },
                                   };
                               },
                       })
               .has_value(),
           "Node UI descriptor must register independently");
    Expect(!ui.Register(NodeUiDescriptor{.type = TypeId{"ui.test"}}), "Duplicate node UI descriptors must be rejected");
    Expect(ui.RegisterPinStyle(TypeId{"float"},
                               [&](const PinStyleContext& context) {
                                   ++pin_style_calls;
                                   return PinStyle{
                                       .color = context.connected ? 0xFF00FF00U : 0xFFFFAA00U,
                                       .radius = 7.0f,
                                       .shape = PinShape::Diamond,
                                   };
                               })
               .has_value(),
           "Pin style callback must register by value type");

    auto creation = registry.Instantiate(document, TypeId{"ui.test"});
    Expect(creation.has_value(), "Typed UI node must instantiate");
    const GraphId graph = document.RootGraph();
    const NodeId node = creation->node.id;
    header_item_node = node;
    const PinId output = creation->pins.front().id;
    Expect(std::get<bool>(creation->node.properties.at("enabled")) == false &&
               std::get<std::int64_t>(creation->node.properties.at("count")) == 7 &&
               std::get<double>(creation->node.properties.at("scale")) == 1.5 &&
               std::get<std::string>(creation->node.properties.at("title")) == "typed" &&
               std::get<Vec2>(creation->node.properties.at("offset")) == Vec2{2.0f, 3.0f} &&
               std::get<AssetReference>(creation->node.properties.at("asset")).id == 42,
           "Node instantiation must preserve every typed property alternative");
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*creation),
                                             NodePresentation{.position = {20.0f, 20.0f}, .z_order = 1}),
            document, presentation, types, "UI test node must be added");
    auto lower_creation = registry.Instantiate(document, TypeId{"ui.test"});
    Expect(lower_creation.has_value(), "Overlapped UI node must instantiate");
    const NodeId lower_node = lower_creation->node.id;
    Execute(commands,
            std::make_unique<AddNodeCommand>(graph, std::move(*lower_creation),
                                             NodePresentation{.position = {20.0f, 20.0f}, .z_order = 0}),
            document, presentation, types, "Overlapped UI node must be added");
    Expect(ValidateGraph(document, graph, registry).empty(),
           "Node-specific validation must accept valid typed defaults");
    Execute(commands,
            std::make_unique<SetNodePropertyCommand>(graph, node, "enabled", PropertyValue{std::string{"wrong type"}}),
            document, presentation, types, "Property alternative change must execute");
    const auto validation = ValidateGraph(document, graph, registry);
    Expect(std::ranges::any_of(
               validation,
               [](const ValidationIssue& issue) { return issue.message == "Property 'enabled' must be boolean"; }),
           "Node-specific validator must diagnose invalid typed property "
           "alternatives");
    Expect(commands.Undo(document, presentation, types).has_value(), "Invalid domain property fixture must undo");
    commands.Clear();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Expect(pixels != nullptr, "Node UI font atlas must build");

    EditorContext editor;
    EditorConfig editor_config;
    editor_config.node_header.maximum_text_lines = 2;
    EditorCallbacks editor_callbacks;
    editor_callbacks.duplicate_selection = [&](const GraphSelection& selection) {
        ++duplicate_calls;
        duplicated_selection = selection;
    };
    const auto draw_editor_frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Node UI body", nullptr, ImGuiWindowFlags_NoDecoration);
        const auto frame_result =
            DrawEditor(editor, document, presentation, commands, registry, ui, routers,
                       {640.0f, 480.0f}, {}, editor_config, editor_callbacks);
        ImGui::End();
        ImGui::Render();
        return frame_result;
    };
    const auto initial_frame = draw_editor_frame();
    Expect(body_calls == 2 && pin_style_calls > 0 && header_state_calls == 2 && header_glyph_calls == 1,
           "Editor must invoke custom body, header presentation, glyph, and pin style callbacks");
    Expect(!initial_frame.model_changed && !std::get<bool>(document.FindNode(graph, node)->properties.at("enabled")),
           "Rendering a custom widget must remain read-only until interaction");
    const ImVec2 body_button_center = (body_button_min + body_button_max) * 0.5f;
    io.AddMousePosEvent(body_button_center.x, body_button_center.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    (void)draw_editor_frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    const auto clicked_frame = draw_editor_frame();
    Expect(body_calls == 6 && clicked_frame.model_changed &&
               std::get<bool>(document.FindNode(graph, node)->properties.at("enabled")) &&
               !std::get<bool>(document.FindNode(graph, lower_node)->properties.at("enabled")),
           "Only the topmost embedded ImGui widget must receive an overlapping "
           "mouse click");

    const ImVec2 header_glyph_center = (header_glyph_min + header_glyph_max) * 0.5f;
    io.AddMousePosEvent(header_glyph_center.x, header_glyph_center.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    const auto header_action_frame = draw_editor_frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    (void)draw_editor_frame();
    Expect(header_action_frame.header_actions.size() == 1 &&
               header_action_frame.header_actions.front().graph == graph &&
               header_action_frame.header_actions.front().node == node &&
               header_action_frame.header_actions.front().item == "state" &&
               header_action_frame.header_actions.front().action == "inspect-runtime",
           "Actionable header glyphs must report a stable application action without mutating the graph");

    header_item_node = lower_node;
    (void)draw_editor_frame();
    const ImVec2 obscured_glyph_center = (header_glyph_min + header_glyph_max) * 0.5f;
    io.AddMousePosEvent(obscured_glyph_center.x, obscured_glyph_center.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    const auto obscured_action_frame = draw_editor_frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    (void)draw_editor_frame();
    Expect(obscured_action_frame.header_actions.empty(),
           "Header actions on lower nodes must not receive clicks through an overlapping top node");
    header_item_node = node;

    editor.SetSelection(GraphSelection{.graph = graph, .nodes = {node}});
    editor.DuplicateSelection();
    const auto duplicate_frame = draw_editor_frame();
    Expect(duplicate_calls == 1 && duplicated_selection.graph == graph &&
               duplicated_selection.nodes == std::vector<NodeId>{node} && !duplicate_frame.model_changed,
           "Application-owned duplication must use the same deferred action path as editor requests");

    ImGui::NewFrame();
    ImGui::Begin("Node UI inspector");
    const auto inspector_result = DrawNodeInspector(ui, document, presentation, commands, registry, graph, node);
    ImGui::End();
    ImGui::Render();
    Expect(inspector_calls == 1 && inspector_result.model_changed, "Inspector callback must execute queued edits");
    const auto* edited_node = document.FindNode(graph, node);
    Expect(std::get<double>(edited_node->properties.at("gain")) == 2.5, "Inspector must persist typed properties");
    const auto dynamic = std::ranges::find_if(edited_node->pins, [&](const PinId pin) {
        return document.FindPin(graph, pin)->storage == PinStorage::Dynamic;
    });
    Expect(dynamic != edited_node->pins.end(), "Inspector context must create dynamic pins through commands");
    const PinId dynamic_pin = *dynamic;
    const PinId second_dynamic_pin = edited_node->pins[2];
    Expect(document.FindPin(graph, edited_node->pins[1])->key == "dynamic_input" &&
               document.FindPin(graph, edited_node->pins[2])->key == "dynamic_second",
           "Multiple deferred dynamic pin appends must preserve callback order");
    auto reorder_static = commands.Execute(
        std::make_unique<ReorderDynamicPinsCommand>(
            graph, node, std::vector<PinId>{edited_node->pins[1], edited_node->pins[0], edited_node->pins[2]}),
        document, presentation, types);
    Expect(!reorder_static && reorder_static.error().code == ErrorCode::InvalidArgument,
           "Dynamic reorder must preserve descriptor-owned static pin slots");

    const LinkId link = document.AllocateLinkId();
    Execute(commands,
            std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = dynamic_pin}),
            document, presentation, types, "Dynamic pin must accept compatible links");
    PinInstance incompatible = *document.FindPin(graph, dynamic_pin);
    incompatible.type = TypeId{"int"};
    auto structural_update =
        commands.Execute(std::make_unique<UpdateDynamicPinCommand>(graph, incompatible), document, presentation, types);
    Expect(!structural_update && structural_update.error().code == ErrorCode::InvalidGraph,
           "Connected dynamic pins must reject structural metadata changes");
    Expect(commands.Undo(document, presentation, types).has_value(), "Dynamic link undo must execute");
    Expect(commands.Undo(document, presentation, types).has_value(), "Inspector compound undo must execute");
    Expect(!document.FindNode(graph, node)->properties.contains("gain") &&
               document.FindPin(graph, dynamic_pin) == nullptr &&
               document.FindPin(graph, second_dynamic_pin) == nullptr,
           "Inspector property and dynamic pin edits must undo atomically");

    ImGui::DestroyContext();
}

void TestMovedStateAndEditorReadOnly() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    const NodeId node = document.AllocateNodeId();
    auto change =
        Execute(commands,
                std::make_unique<ModelOnlyNodeCommand>(
                    graph, NodeInstance{.id = node, .type = TypeId{"model-only"}, .display_name = "Model only"}),
                document, presentation, types, "Model-only custom command must execute");
    Expect(change.model_changed && !change.presentation_changed && presentation.FindNode(node) == nullptr,
           "Custom transaction command must report its exact changed domain");

    GraphDocument moved_document{std::move(document)};
    GraphPresentation moved_presentation{std::move(presentation)};
    Expect(commands.Undo(moved_document, moved_presentation, types).has_value(),
           "Stable command history must rebind to jointly moved document owners");
    Expect(document.RootGraph() && presentation.PresentationRevision() == 0,
           "Moved-from document state must remain reusable and valid");
    Expect(commands.Redo(moved_document, moved_presentation, types).has_value(), "Moved history must remain redoable");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2{800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Expect(pixels != nullptr, "Editor font atlas must build");

    RegistryCatalog registry;
    NodeUiRegistry ui;
    LinkRouterRegistry routers;
    EditorContext editor;
    editor.SetSelection(GraphSelection{
        .graph = moved_document.RootGraph(),
        .nodes = {NodeId{999999}},
    });
    editor.FrameAll();
    const Revisions before{moved_document.ModelRevision(), moved_presentation.PresentationRevision()};
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0.0f, 0.0f});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Node editor smoke", nullptr, ImGuiWindowFlags_NoDecoration);
    const auto result =
        DrawEditor(editor, moved_document, moved_presentation, commands, registry, ui, routers, {640.0f, 480.0f});
    ImGui::End();
    ImGui::Render();
    Expect(result.selection_changed, "Editor must report stale selection pruning");
    Expect(!result.model_changed && !result.presentation_changed,
           "Read-only rendering and fallback layout must not report persisted "
           "changes");
    Expect(Revisions{moved_document.ModelRevision(), moved_presentation.PresentationRevision()} == before,
           "DrawEditor must not normalize or mutate persisted state during "
           "rendering");
    Expect(ImGui::GetDrawData() != nullptr, "Editor must produce draw data");
    ImGui::DestroyContext();
}

void TestStageFourSubgraphsAndIntergraphLinks() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands;
    const GraphId root = document.RootGraph();

    const NodeId owner = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(root, NodeCreation{.node = NodeInstance{.id = owner,
                                                                                     .type = TypeId{"test.subgraph"},
                                                                                     .display_name = "Owned"}}),
            document, presentation, types, "Owned subgraph call-site must be added");

    GraphInterface interface{
        .version = 3,
        .pins =
            {
                GraphInterfacePin{
                    .key = "in",
                    .label = "In",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Input,
                    .caller_cardinality = PinCardinality::Single,
                    .boundary_cardinality = PinCardinality::Multiple,
                },
                GraphInterfacePin{
                    .key = "out",
                    .label = "Out",
                    .type = TypeId{"float"},
                    .direction = PinDirection::Output,
                    .caller_cardinality = PinCardinality::Multiple,
                    .boundary_cardinality = PinCardinality::Single,
                },
            },
    };
    const GraphId child = document.AllocateGraphId();
    std::vector<std::unique_ptr<Command>> create_child;
    create_child.push_back(std::make_unique<AddGraphCommand>(Graph{
        .id = child,
        .display_name = "Child",
        .lifetime = GraphLifetime::Owned,
    }));
    create_child.push_back(std::make_unique<SetNodeSubgraphCommand>(root, owner,
                                                                    SubgraphReference{
                                                                        .ownership = SubgraphOwnership::Owned,
                                                                        .target = DocumentGraphTarget{child},
                                                                    }));
    create_child.push_back(std::make_unique<SetGraphInterfaceCommand>(child, interface));
    Execute(commands, std::make_unique<CompoundCommand>("Create owned subgraph", std::move(create_child)), document,
            presentation, types, "Owned subgraph and interface must be created atomically");

    const auto* owner_node = document.FindNode(root, owner);
    const auto* child_graph = document.FindGraph(child);
    Expect(owner_node != nullptr && owner_node->role == NodeRole::Subgraph && owner_node->pins.size() == 2,
           "Subgraph call-site pins must project the child interface");
    Expect(child_graph != nullptr && child_graph->lifetime == GraphLifetime::Owned &&
               std::ranges::count_if(child_graph->nodes,
                                     [](const auto& entry) { return entry.second.role == NodeRole::BoundaryInput; }) ==
                   1 &&
               std::ranges::count_if(child_graph->nodes,
                                     [](const auto& entry) { return entry.second.role == NodeRole::BoundaryOutput; }) ==
                   1,
           "Graph interface command must create exactly one boundary node per "
           "direction");
    Expect(document.ValidateStructure().has_value(), "Owned hierarchy and boundary projections must validate");

    const NodeId competing_owner = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                root, NodeCreation{.node = NodeInstance{.id = competing_owner, .type = TypeId{"test.subgraph"}}}),
            document, presentation, types, "Competing owner fixture node must be added");
    auto duplicate_owner =
        commands.Execute(std::make_unique<SetNodeSubgraphCommand>(root, competing_owner,
                                                                  SubgraphReference{
                                                                      .ownership = SubgraphOwnership::Owned,
                                                                      .target = DocumentGraphTarget{child},
                                                                  }),
                         document, presentation, types);
    Expect(!duplicate_owner && duplicate_owner.error().code == ErrorCode::InvalidGraph &&
               document.FindNode(root, competing_owner)->role == NodeRole::Regular &&
               document.ValidateStructure().has_value(),
           "Incremental ownership validation must reject a second owner atomically");

    const NodeId lone_boundary = document.AllocateNodeId();
    auto invalid_boundary =
        commands.Execute(std::make_unique<AddNodeCommand>(root, NodeCreation{.node =
                                                                                 NodeInstance{
                                                                                     .id = lone_boundary,
                                                                                     .type = TypeId{"test.boundary"},
                                                                                     .role = NodeRole::BoundaryInput,
                                                                                 }}),
                         document, presentation, types);
    Expect(!invalid_boundary && invalid_boundary.error().code == ErrorCode::InvalidGraph &&
               document.FindNode(root, lone_boundary) == nullptr,
           "Incremental boundary validation must reject a lone boundary node "
           "atomically");

    GraphInterface expanded_interface = interface;
    expanded_interface.version = 4;
    expanded_interface.pins.front().label = "Input signal";
    expanded_interface.pins.push_back(GraphInterfacePin{
        .key = "gain",
        .label = "Gain",
        .type = TypeId{"float"},
        .direction = PinDirection::Input,
    });
    Execute(commands, std::make_unique<SetNodeReadOnlyCommand>(root, owner, true), document, presentation, types,
            "Subgraph call-site must become read-only");
    GraphInterface relabeled_interface = interface;
    relabeled_interface.version = 4;
    relabeled_interface.pins.front().label = "Relabeled input";
    const auto read_only_label =
        commands.Execute(std::make_unique<SetGraphInterfaceCommand>(child, std::move(relabeled_interface)), document,
                         presentation, types);
    Expect(!read_only_label && read_only_label.error().code == ErrorCode::ReadOnly &&
               document.FindNode(root, owner)->pins.size() == 2 && document.FindGraph(child)->interface.version == 3,
           "Interface synchronization must not relabel pins owned by a read-only "
           "call-site");
    const auto read_only_interface = commands.Execute(
        std::make_unique<SetGraphInterfaceCommand>(child, expanded_interface), document, presentation, types);
    Expect(!read_only_interface && read_only_interface.error().code == ErrorCode::ReadOnly,
           "Interface synchronization must not replace read-only call-site nodes "
           "or pins");
    Execute(commands, std::make_unique<SetNodeReadOnlyCommand>(root, owner, false), document, presentation, types,
            "Subgraph call-site must become writable again");
    const SemanticRevisionSet before_expanded_root_revisions = document.GraphRevisions(root);
    const SemanticRevisionSet before_expanded_child_revisions = document.GraphRevisions(child);
    Execute(commands, std::make_unique<SetGraphInterfaceCommand>(child, expanded_interface), document, presentation,
            types, "Graph interface update must propagate");
    const SemanticRevisionSet expanded_root_revisions = document.GraphRevisions(root);
    const SemanticRevisionSet expanded_child_revisions = document.GraphRevisions(child);
    Expect(expanded_root_revisions.topology == before_expanded_root_revisions.topology + 1 &&
               expanded_root_revisions.layout == before_expanded_root_revisions.layout + 1 &&
               expanded_child_revisions.topology == before_expanded_child_revisions.topology + 1 &&
               expanded_child_revisions.layout == before_expanded_child_revisions.layout + 1,
           "Interface apply must advance every affected graph revision");
    Expect(document.FindNode(root, owner)->pins.size() == 3 && document.FindGraph(child)->interface.version == 4,
           "Interface updates must synchronize every call-site and boundary "
           "projection");
    std::vector<PinId> reordered_owner = document.FindNode(root, owner)->pins;
    std::ranges::reverse(reordered_owner);
    const std::uint64_t model_before_rejected_reorder = document.ModelRevision();
    auto rejected_owner_reorder =
        commands.Execute(std::make_unique<ReorderDynamicPinsCommand>(root, owner, std::move(reordered_owner)), document,
                         presentation, types);
    Expect(!rejected_owner_reorder && rejected_owner_reorder.error().code == ErrorCode::InvalidArgument &&
               document.ModelRevision() == model_before_rejected_reorder &&
               document.GraphRevisions(root) == expanded_root_revisions,
           "Subgraph projection pins must reject manual reordering without "
           "changing revisions");
    const auto boundary_input = std::ranges::find_if(document.FindGraph(child)->nodes, [](const auto& entry) {
        return entry.second.role == NodeRole::BoundaryInput;
    });
    Expect(boundary_input != document.FindGraph(child)->nodes.end(),
           "Expanded interface must retain an input boundary node");
    std::vector<PinId> reordered_boundary = boundary_input->second.pins;
    std::ranges::reverse(reordered_boundary);
    auto rejected_boundary_reorder = commands.Execute(
        std::make_unique<ReorderDynamicPinsCommand>(child, boundary_input->first, std::move(reordered_boundary)),
        document, presentation, types);
    Expect(!rejected_boundary_reorder && rejected_boundary_reorder.error().code == ErrorCode::InvalidArgument &&
               document.ModelRevision() == model_before_rejected_reorder &&
               document.GraphRevisions(child) == expanded_child_revisions && document.ValidateStructure().has_value(),
           "Boundary projection pins must reject manual reordering and remain "
           "structurally valid");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindNode(root, owner)->pins.size() == 2 && document.FindGraph(child)->interface.version == 3,
           "Interface synchronization must undo as one command");
    const SemanticRevisionSet undone_root_revisions = document.GraphRevisions(root);
    const SemanticRevisionSet undone_child_revisions = document.GraphRevisions(child);
    Expect(undone_root_revisions.topology == expanded_root_revisions.topology + 1 &&
               undone_root_revisions.layout == expanded_root_revisions.layout + 1 &&
               undone_child_revisions.topology == expanded_child_revisions.topology + 1 &&
               undone_child_revisions.layout == expanded_child_revisions.layout + 1,
           "Interface undo must advance graph revisions from live state");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               document.FindNode(root, owner)->pins.size() == 3 && document.FindGraph(child)->interface.version == 4,
           "Interface synchronization must redo as one command");
    const SemanticRevisionSet redone_root_revisions = document.GraphRevisions(root);
    const SemanticRevisionSet redone_child_revisions = document.GraphRevisions(child);
    Expect(redone_root_revisions.topology == undone_root_revisions.topology + 1 &&
               redone_root_revisions.layout == undone_root_revisions.layout + 1 &&
               redone_child_revisions.topology == undone_child_revisions.topology + 1 &&
               redone_child_revisions.layout == undone_child_revisions.layout + 1,
           "Interface redo must keep graph revisions monotonic");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindGraph(child)->interface.version == 3,
           "Interface fixture must return to its pre-update state");
    Expect(document.GraphRevisions(root).topology == redone_root_revisions.topology + 1 &&
               document.GraphRevisions(child).topology == redone_child_revisions.topology + 1,
           "Repeated interface undo must not reuse snapshot revisions");

    const NodeId nested_owner = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                child, NodeCreation{.node = NodeInstance{.id = nested_owner, .type = TypeId{"test.subgraph"}}}),
            document, presentation, types, "Nested owner node must be added");
    const GraphId grandchild = document.AllocateGraphId();
    std::vector<std::unique_ptr<Command>> create_grandchild;
    create_grandchild.push_back(std::make_unique<AddGraphCommand>(Graph{
        .id = grandchild,
        .display_name = "Grandchild",
        .lifetime = GraphLifetime::Owned,
    }));
    create_grandchild.push_back(std::make_unique<SetNodeSubgraphCommand>(child, nested_owner,
                                                                         SubgraphReference{
                                                                             .ownership = SubgraphOwnership::Owned,
                                                                             .target = DocumentGraphTarget{grandchild},
                                                                         }));
    create_grandchild.push_back(std::make_unique<SetGraphInterfaceCommand>(grandchild, GraphInterface{}));
    Execute(commands, std::make_unique<CompoundCommand>("Create nested owned subgraph", std::move(create_grandchild)),
            document, presentation, types, "Nested owned subgraph must be created atomically");

    auto fragment = CaptureGraphFragment(document, presentation, GraphSelection{.graph = root, .nodes = {owner}});
    Expect(fragment && fragment->owned_graphs.size() == 2,
           "Capturing an owned call-site must include its complete owned closure");
    std::ranges::reverse(fragment->owned_graphs);
    auto prepared = PrepareGraphFragmentPaste(document, presentation, types, *fragment, root, Vec2{500.0f, 200.0f});
    Expect(prepared && prepared->remap.graphs.size() == 2,
           "Owned fragment preparation must remap every graph ID independent of "
           "bundle order");
    const NodeId copied_owner = prepared->remap.nodes.at(owner);
    const GraphId copied_child = prepared->remap.graphs.at(child);
    const GraphId copied_grandchild = prepared->remap.graphs.at(grandchild);
    Execute(commands, std::make_unique<PasteGraphFragmentCommand>(std::move(*prepared)), document, presentation, types,
            "Owned graph closure must paste atomically");
    const auto* copied_target =
        std::get_if<DocumentGraphTarget>(&document.FindNode(root, copied_owner)->subgraph->target);
    Expect(copied_target != nullptr && copied_target->graph == copied_child && copied_child != child &&
               copied_grandchild != grandchild && document.FindGraph(copied_child) != nullptr &&
               document.FindGraph(copied_grandchild) != nullptr,
           "Owned paste must deep-copy graphs instead of aliasing the source "
           "hierarchy");
    Expect(commands.Undo(document, presentation, types).has_value() &&
               document.FindNode(root, copied_owner) == nullptr && document.FindGraph(copied_child) == nullptr &&
               document.FindGraph(copied_grandchild) == nullptr,
           "One undo must remove a pasted owned closure completely");

    commands.Clear();
    Execute(commands, std::make_unique<DeleteElementsCommand>(root, std::vector<NodeId>{owner}), document, presentation,
            types, "Deleting an owned call-site must cascade");
    Expect(document.FindGraph(child) == nullptr && document.FindGraph(grandchild) == nullptr,
           "Deleting an owner must remove all owned descendants");
    Expect(commands.Undo(document, presentation, types).has_value() && document.FindGraph(child) != nullptr &&
               document.FindGraph(grandchild) != nullptr,
           "Owned cascade deletion must restore the complete hierarchy on undo");

    const GraphId reusable = document.AllocateGraphId();
    Execute(commands, std::make_unique<AddGraphCommand>(Graph{.id = reusable, .display_name = "Reusable"}), document,
            presentation, types, "Reusable graph must be added");
    const NodeId reference_a = document.AllocateNodeId();
    const NodeId reference_b = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                root, NodeCreation{.node = NodeInstance{.id = reference_a, .type = TypeId{"test.subgraph"}}}),
            document, presentation, types, "First reference node must be added");
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                root, NodeCreation{.node = NodeInstance{.id = reference_b, .type = TypeId{"test.subgraph"}}}),
            document, presentation, types, "Second reference node must be added");
    const SubgraphReference shared_reference{
        .ownership = SubgraphOwnership::Referenced,
        .target = DocumentGraphTarget{reusable},
    };
    Execute(commands, std::make_unique<SetNodeSubgraphCommand>(root, reference_a, shared_reference), document,
            presentation, types, "First reusable reference must bind");
    Execute(commands, std::make_unique<SetNodeSubgraphCommand>(root, reference_b, shared_reference), document,
            presentation, types, "Second reusable reference must bind");
    commands.Clear();
    Execute(commands, std::make_unique<DeleteElementsCommand>(root, std::vector<NodeId>{reference_a, reference_b}),
            document, presentation, types, "Referenced call-sites must be deletable");
    Expect(document.FindGraph(reusable) != nullptr,
           "Deleting referenced call-sites must not delete their reusable target");

    const NodeId reusable_owner = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                reusable, NodeCreation{.node = NodeInstance{.id = reusable_owner, .type = TypeId{"test.subgraph"}}}),
            document, presentation, types, "Reusable graph owner node must be added");
    const GraphId reusable_child = document.AllocateGraphId();
    std::vector<std::unique_ptr<Command>> create_reusable_child;
    create_reusable_child.push_back(std::make_unique<AddGraphCommand>(Graph{
        .id = reusable_child,
        .lifetime = GraphLifetime::Owned,
    }));
    create_reusable_child.push_back(
        std::make_unique<SetNodeSubgraphCommand>(reusable, reusable_owner,
                                                 SubgraphReference{
                                                     .ownership = SubgraphOwnership::Owned,
                                                     .target = DocumentGraphTarget{reusable_child},
                                                 }));
    Execute(commands,
            std::make_unique<CompoundCommand>("Create reusable graph hierarchy", std::move(create_reusable_child)),
            document, presentation, types, "Reusable graph owned child must be created");
    commands.Clear();
    Execute(commands, std::make_unique<RemoveGraphCommand>(reusable), document, presentation, types,
            "Reusable graph hierarchy must be removable");
    Expect(document.FindGraph(reusable) == nullptr && document.FindGraph(reusable_child) == nullptr,
           "Removing a reusable graph must cascade through its owned descendants");
    Expect(commands.Undo(document, presentation, types).has_value() && document.FindGraph(reusable) != nullptr &&
               document.FindGraph(reusable_child) != nullptr,
           "Graph hierarchy removal undo must restore all descendants");

    const GraphId left = document.AllocateGraphId();
    const GraphId right = document.AllocateGraphId();
    Execute(commands, std::make_unique<AddGraphCommand>(left), document, presentation, types,
            "Left intergraph area must be added");
    Execute(commands, std::make_unique<AddGraphCommand>(right), document, presentation, types,
            "Right intergraph area must be added");
    const NodeId sender = document.AllocateNodeId();
    const PinId sender_pin = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(left,
                                             NodeCreation{
                                                 .node = NodeInstance{.id = sender,
                                                                      .type = TypeId{"intergraph.send"},
                                                                      .role = NodeRole::IntergraphOutput},
                                                 .pins = {PinInstance{
                                                     .id = sender_pin,
                                                     .node = sender,
                                                     .key = "channel",
                                                     .label = "Channel",
                                                     .type = TypeId{"float"},
                                                     .direction = PinDirection::Input,
                                                     .storage = PinStorage::Dynamic,
                                                 }},
                                             }),
            document, presentation, types, "Intergraph sender must be added");
    const NodeId receiver = document.AllocateNodeId();
    const PinId receiver_pin = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(right,
                                             NodeCreation{
                                                 .node = NodeInstance{.id = receiver,
                                                                      .type = TypeId{"intergraph.receive"},
                                                                      .role = NodeRole::IntergraphInput},
                                                 .pins = {PinInstance{
                                                     .id = receiver_pin,
                                                     .node = receiver,
                                                     .key = "channel",
                                                     .label = "Channel",
                                                     .type = TypeId{"float"},
                                                     .direction = PinDirection::Output,
                                                     .storage = PinStorage::Dynamic,
                                                 }},
                                             }),
            document, presentation, types, "Intergraph receiver must be added");
    const IntergraphLinkId channel = document.AllocateIntergraphLinkId();
    Execute(commands,
            std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
                .id = channel,
                .source = {left, sender, sender_pin},
                .destination = {right, receiver, receiver_pin},
            }),
            document, presentation, types, "Intergraph proxies must connect");
    Expect(document.FindIntergraphLink(channel) != nullptr && document.ValidateStructure().has_value(),
           "Intergraph links must be first-class validated document records");
    auto remove_endpoint_pin =
        commands.Execute(std::make_unique<RemoveDynamicPinCommand>(left, sender_pin), document, presentation, types);
    PinInstance incompatible_endpoint = *document.FindPin(left, sender_pin);
    incompatible_endpoint.type = TypeId{"int"};
    auto update_endpoint_pin =
        commands.Execute(std::make_unique<UpdateDynamicPinCommand>(left, std::move(incompatible_endpoint)), document,
                         presentation, types);
    Expect(!remove_endpoint_pin && remove_endpoint_pin.error().code == ErrorCode::InvalidGraph &&
               !update_endpoint_pin && update_endpoint_pin.error().code == ErrorCode::InvalidGraph &&
               document.FindIntergraphLink(channel) != nullptr && document.ValidateStructure().has_value(),
           "Intergraph endpoint pins must not be removed or structurally changed "
           "while connected");

    const NodeId reverse_sender = document.AllocateNodeId();
    const PinId reverse_sender_pin = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(right,
                                             NodeCreation{
                                                 .node = NodeInstance{.id = reverse_sender,
                                                                      .type = TypeId{"intergraph.send"},
                                                                      .role = NodeRole::IntergraphOutput},
                                                 .pins = {PinInstance{
                                                     .id = reverse_sender_pin,
                                                     .node = reverse_sender,
                                                     .key = "reverse",
                                                     .label = "Reverse",
                                                     .type = TypeId{"float"},
                                                     .direction = PinDirection::Input,
                                                     .storage = PinStorage::Dynamic,
                                                 }},
                                             }),
            document, presentation, types, "Reverse intergraph sender must be added");
    const NodeId reverse_receiver = document.AllocateNodeId();
    const PinId reverse_receiver_pin = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(left,
                                             NodeCreation{
                                                 .node = NodeInstance{.id = reverse_receiver,
                                                                      .type = TypeId{"intergraph.receive"},
                                                                      .role = NodeRole::IntergraphInput},
                                                 .pins = {PinInstance{
                                                     .id = reverse_receiver_pin,
                                                     .node = reverse_receiver,
                                                     .key = "reverse",
                                                     .label = "Reverse",
                                                     .type = TypeId{"float"},
                                                     .direction = PinDirection::Output,
                                                     .storage = PinStorage::Dynamic,
                                                 }},
                                             }),
            document, presentation, types, "Reverse intergraph receiver must be added");
    auto recursive_channel = commands.Execute(std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
                                                  .id = document.AllocateIntergraphLinkId(),
                                                  .source = {right, reverse_sender, reverse_sender_pin},
                                                  .destination = {left, reverse_receiver, reverse_receiver_pin},
                                              }),
                                              document, presentation, types);
    Expect(!recursive_channel && recursive_channel.error().code == ErrorCode::InvalidGraph,
           "Intergraph links must not create recursive graph dependencies");

    EditorContext editor;
    Expect(editor.ResetNavigation(document).has_value(), "Editor navigation must initialize from the root");
    editor.SetPan({35.0f, 45.0f});
    editor.SetSelection(GraphSelection{.graph = root, .nodes = {owner}});
    Expect(editor.EnterSubgraph(document, owner).has_value() && editor.ActiveGraph() == child &&
               editor.Breadcrumbs().size() == 2,
           "Entering a local subgraph must extend the breadcrumb path");
    editor.SetPan({90.0f, 110.0f});
    Expect(editor.NavigateBack() && editor.ActiveGraph() == root && editor.Pan() == Vec2{35.0f, 45.0f} &&
               editor.Selection().nodes == std::vector<NodeId>{owner},
           "Breadcrumb back navigation must restore the parent viewport and "
           "selection");
}

void TestStageFiveEditorGeometryCacheAndRouting() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog nodes;
    NodeUiRegistry ui;
    LinkRouterRegistry routers;
    RegistryCatalog& types = nodes;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    Expect(nodes
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"stage5.node"},
                   .display_name = "Stage five node",
                   .property_impacts =
                       {
                           {"runtime", PropertyImpact::RuntimeOnly},
                           {"rendering", PropertyImpact::Rendering},
                           {"geometry", PropertyImpact::Geometry},
                           {"topology", PropertyImpact::Topology},
                       },
               })
               .has_value(),
           "Stage-five semantic descriptor must register");
    const NodeId source = document.AllocateNodeId();
    const NodeId sink = document.AllocateNodeId();
    const PinId output = document.AllocatePinId();
    const PinId input = document.AllocatePinId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                graph,
                NodeCreation{
                    .node = NodeInstance{.id = source, .type = TypeId{"stage5.node"}, .display_name = "Source"},
                    .pins = {PinInstance{
                        .id = output,
                        .node = source,
                        .key = "out",
                        .label = "Out",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                        .cardinality = PinCardinality::Multiple,
                    }},
                },
                NodePresentation{.position = {20.0f, 20.0f}, .size = {200.0f, 100.0f}}),
            document, presentation, types, "Stage-five source must be added");
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                graph,
                NodeCreation{
                    .node = NodeInstance{.id = sink, .type = TypeId{"stage5.node"}, .display_name = "Sink"},
                    .pins = {PinInstance{
                        .id = input,
                        .node = sink,
                        .key = "in",
                        .label = "In",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Input,
                    }},
                },
                NodePresentation{.position = {340.0f, 220.0f}, .size = {200.0f, 100.0f}}),
            document, presentation, types, "Stage-five sink must be added");
    const LinkId link = document.AllocateLinkId();
    Execute(commands, std::make_unique<ConnectPinsCommand>(graph, Link{.id = link, .output = output, .input = input}),
            document, presentation, types, "Stage-five link must connect");

    int layout_calls = 0;
    Expect(
        ui.Register(NodeUiDescriptor{
                        .type = TypeId{"stage5.node"},
                        .default_size = {200.0f, 100.0f},
                        .layout = [&](const NodeUiLayoutContext& context) -> Result<NodeUiLayout> {
                            ++layout_calls;
                            NodeUiLayout layout;
                            layout.body = GraphRect{{8.0f, context.header_height + 8.0f},
                                                    {context.node_size.x - 8.0f, context.node_size.y - 20.0f}};
                            for (const PinId pin_id : context.node.pins) {
                                const auto& pin = context.graph.pins.at(pin_id);
                                const bool output_pin = pin.direction == PinDirection::Output;
                                layout.pins.push_back(PinPlacement{
                                    .pin = pin_id,
                                    .position = {context.node_size.x * 0.5f, output_pin ? 0.0f : context.node_size.y},
                                    .outward_normal = {0.0f, output_pin ? -1.0f : 1.0f},
                                    .label =
                                        PinLabelPlacement{
                                            .offset = {0.0f, output_pin ? 9.0f : -9.0f},
                                            .pivot = {0.5f, output_pin ? 0.0f : 1.0f},
                                        },
                                });
                            }
                            return layout;
                        },
                    })
            .has_value(),
        "Stage-five custom node layout must register");

    int router_calls = 0;
    Vec2 routed_output;
    Vec2 routed_input;
    Vec2 routed_output_normal;
    Vec2 routed_input_normal;
    const TypeId custom_router{"stage5.router.step"};
    Expect(routers
               .Register(LinkRouterDescriptor{
                   .type = custom_router,
                   .callback = [&](const LinkRoutingContext& context) -> Result<LinkPath> {
                       ++router_calls;
                       routed_output = context.output.position;
                       routed_input = context.input.position;
                       routed_output_normal = context.output.outward_normal;
                       routed_input_normal = context.input.outward_normal;
                       const Vec2 corner{context.output.position.x, context.input.position.y};
                       return LinkPath{{
                           LinkPathSegment{LinePathSegment{context.output.position, corner}, 0},
                           LinkPathSegment{LinePathSegment{corner, context.input.position}, 0},
                       }};
                   },
               })
               .has_value(),
           "Stage-five custom link router must register");
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, custom_router), document, presentation, types,
            "Stage-five custom router must persist");
    Expect(commands.Undo(document, presentation, types).has_value() && presentation.FindLink(link) == nullptr,
           "Link router selection must undo without leaving empty presentation "
           "state");
    Expect(commands.Redo(document, presentation, types).has_value() &&
               presentation.FindLink(link)->Style().router == custom_router,
           "Link router selection must redo atomically");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800.0f, 600.0f};
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Expect(pixels != nullptr, "Stage-five font atlas must build");

    EditorContext editor;
    EditorConfig config;
    config.show_minimap = false;
    config.show_breadcrumbs = false;
    const auto draw_frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0.0f, 0.0f});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Stage-five editor", nullptr, ImGuiWindowFlags_NoDecoration);
        const auto frame =
            DrawEditor(editor, document, presentation, commands, nodes, ui, routers, {640.0f, 480.0f}, {}, config);
        ImGui::End();
        ImGui::Render();
        return frame;
    };

    editor.ResetMetrics();
    (void)draw_frame();
    const EditorMetrics cold = editor.Metrics();
    Expect(layout_calls == 2 && router_calls == 1 && cold.geometry_rebuilds == 1 && cold.routed_links == 1,
           "Cold editor frame must build each node layout and link route exactly "
           "once");
    Expect(routed_output == Vec2{120.0f, 20.0f} && routed_input == Vec2{440.0f, 320.0f} &&
               routed_output_normal == Vec2{0.0f, -1.0f} && routed_input_normal == Vec2{0.0f, 1.0f},
           "Custom pin placement and normals must drive router endpoints");

    editor.ResetMetrics();
    (void)draw_frame();
    const EditorMetrics warm = editor.Metrics();
    Expect(layout_calls == 2 && router_calls == 1 && warm.geometry_rebuilds == 0 && warm.routed_links == 0,
           "Warm editor frame must reuse cached logical geometry and routed paths");

    const auto execute_property = [&](const char* key, const std::int64_t value) {
        auto result =
            commands.Execute(std::make_unique<SetNodePropertyCommand>(graph, source, key, PropertyValue{value}),
                             document, presentation, types);
        Expect(result.has_value(), "Property-impact command must execute");
    };
    const SemanticRevisionSet before_runtime = document.GraphRevisions(graph);
    execute_property("runtime", 1);
    const SemanticRevisionSet after_runtime = document.GraphRevisions(graph);
    Expect(after_runtime.serial > before_runtime.serial && after_runtime.value == before_runtime.value + 1 &&
               after_runtime.layout == before_runtime.layout && after_runtime.topology == before_runtime.topology,
           "Runtime-only properties must advance only value revisions");
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 0 && editor.Metrics().routed_links == 0,
           "Runtime-only properties must not invalidate geometry");

    execute_property("rendering", 1);
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 0, "Rendering-only properties must not invalidate geometry");

    const SemanticRevisionSet before_geometry = document.GraphRevisions(graph);
    execute_property("geometry", 1);
    const SemanticRevisionSet after_geometry = document.GraphRevisions(graph);
    Expect(after_geometry.value == before_geometry.value + 1 && after_geometry.layout == before_geometry.layout + 1 &&
               after_geometry.topology == before_geometry.topology,
           "Geometry properties must advance value and layout revisions");
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 1, "Geometry properties must invalidate geometry");

    const SemanticRevisionSet before_topology = document.GraphRevisions(graph);
    execute_property("topology", 1);
    const SemanticRevisionSet after_topology = document.GraphRevisions(graph);
    Expect(after_topology.value == before_topology.value + 1 && after_topology.layout == before_topology.layout + 1 &&
               after_topology.topology == before_topology.topology + 1,
           "Topology properties must advance every semantic graph revision");
    (void)draw_frame();

    RegistryCatalog conflicting_nodes;
    Expect(conflicting_nodes
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"stage5.node"},
                   .display_name = "Conflicting stage five node",
                   .property_impacts = {{"topology", PropertyImpact::RuntimeOnly}},
               })
               .has_value(),
           "Conflicting property-impact registry must register");
    const SemanticRevisionSet before_topology_undo = document.GraphRevisions(graph);
    auto conflicting_undo = commands.Undo(document, presentation, conflicting_nodes);
    Expect(!conflicting_undo && conflicting_undo.error().code == ErrorCode::RegistryMismatch &&
               commands.Undo(document, presentation, nodes).has_value(),
           "Topology property undo must reject a substituted registry and retain "
           "the history entry");
    const SemanticRevisionSet after_topology_undo = document.GraphRevisions(graph);
    Expect(after_topology_undo.value == before_topology_undo.value + 1 &&
               after_topology_undo.layout == before_topology_undo.layout + 1 &&
               after_topology_undo.topology == before_topology_undo.topology + 1,
           "Property undo must retain the impact captured during initial apply");
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 1, "Captured topology impact must invalidate geometry during undo");
    auto conflicting_redo = commands.Redo(document, presentation, conflicting_nodes);
    Expect(!conflicting_redo && conflicting_redo.error().code == ErrorCode::RegistryMismatch &&
               commands.Redo(document, presentation, nodes).has_value(),
           "Topology property redo must reject a substituted registry and retain "
           "the history entry");
    const SemanticRevisionSet after_topology_redo = document.GraphRevisions(graph);
    Expect(after_topology_redo.value == after_topology_undo.value + 1 &&
               after_topology_redo.layout == after_topology_undo.layout + 1 &&
               after_topology_redo.topology == after_topology_undo.topology + 1,
           "Property redo must retain the originally captured impact");
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 1, "Captured topology impact must invalidate geometry during redo");

    execute_property("undeclared", 1);
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 1, "Undeclared properties must conservatively invalidate geometry");

    auto color_state = *presentation.FindNode(source);
    color_state.color = 0xFF123456U;
    const std::uint64_t geometry_revision_before_color = presentation.GeometryRevision();
    auto color_change = commands.Execute(std::make_unique<SetNodePresentationCommand>(source, color_state), document,
                                         presentation, types);
    Expect(color_change && presentation.GeometryRevision() == geometry_revision_before_color,
           "Presentation colors must not advance geometry revision");
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 0, "Presentation colors must be rendered without rebuilding geometry");

    const std::uint64_t ui_layout_before_style = ui.LayoutRevision();
    Expect(ui.RegisterPinStyle(TypeId{"float"}, [](const PinStyleContext&) { return PinStyle{}; }).has_value(),
           "Stage-five pin style must register");
    Expect(ui.LayoutRevision() == ui_layout_before_style, "Pin style changes must not advance UI layout revision");
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 0, "Pin style changes must not invalidate geometry");

    const GraphId inactive_graph = document.AllocateGraphId();
    Execute(commands, std::make_unique<AddGraphCommand>(inactive_graph), document, presentation, types,
            "Inactive graph must be added");
    const NodeId inactive_node = document.AllocateNodeId();
    Execute(
        commands,
        std::make_unique<AddNodeCommand>(inactive_graph,
                                         NodeCreation{
                                             .node = NodeInstance{.id = inactive_node, .type = TypeId{"stage5.node"}},
                                         }),
        document, presentation, types, "Inactive graph node must be added");
    (void)draw_frame();
    editor.ResetMetrics();
    auto inactive_edit =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(inactive_graph, inactive_node, "geometry",
                                                                  PropertyValue{std::int64_t{1}}),
                         document, presentation, types);
    Expect(inactive_edit.has_value(), "Inactive graph property must execute");
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 0,
           "Semantic changes in inactive graphs must not invalidate active graph "
           "geometry");

    const int routers_before_pan = router_calls;
    editor.SetPan({75.0f, 30.0f});
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 0 && router_calls == routers_before_pan,
           "Pan must remain a pure transform without geometry invalidation");

    const int layouts_before_invalidation = layout_calls;
    const int routers_before_invalidation = router_calls;
    editor.InvalidateGeometry();
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 1 && layout_calls == layouts_before_invalidation + 2 &&
               router_calls == routers_before_invalidation + 1,
           "Explicit invalidation must rebuild callbacks with external captured "
           "state");
    Execute(commands,
            std::make_unique<MoveNodesCommand>(graph, MoveNodesCommand::Positions{{source, {20.0f, 20.0f}}},
                                               MoveNodesCommand::Positions{{source, {40.0f, 50.0f}}}),
            document, presentation, types, "Stage-five source move must execute");
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 1 && routed_output == Vec2{140.0f, 50.0f},
           "Presentation revision changes must invalidate cached endpoints and "
           "incident routes");

    const TypeId throwing_router{"stage5.router.throwing"};
    Expect(routers
               .Register(LinkRouterDescriptor{
                   .type = throwing_router,
                   .callback = [](const LinkRoutingContext&) -> Result<LinkPath> {
                       throw std::runtime_error("expected router failure");
                   },
               })
               .has_value(),
           "Throwing router fixture must register");
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, throwing_router), document, presentation, types,
            "Throwing router fixture must persist");
    (void)draw_frame();
    Expect(editor.LastError().find("expected router failure") != std::string::npos,
           "Router exceptions must be contained and reported without escaping "
           "DrawEditor");
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, custom_router), document, presentation, types,
            "Custom router must be restored after exception testing");

    const TypeId self_removing_router{"stage5.router.self-removing"};
    Expect(routers
               .Register(LinkRouterDescriptor{
                   .type = self_removing_router,
                   .callback = [&](const LinkRoutingContext& context) -> Result<LinkPath> {
                       (void)routers.Unregister(self_removing_router);
                       return LinkPath{
                           {LinkPathSegment{LinePathSegment{context.output.position, context.input.position}, 0}}};
                   },
               })
               .has_value(),
           "Self-removing router fixture must register");
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, self_removing_router), document, presentation, types,
            "Self-removing router fixture must persist");
    (void)draw_frame();
    Expect(editor.LastError().find("must not mutate editor state or their registry") != std::string::npos,
           "Router registry mutation must fail closed without leaving dangling "
           "callbacks");
    Execute(commands, std::make_unique<SetLinkRouterCommand>(link, custom_router), document, presentation, types,
            "Custom router must be restored after mutation testing");

    GraphDocument replacement_document;
    GraphPresentation replacement_presentation;
    CommandStack replacement_commands;
    const GraphId replacement_graph = replacement_document.RootGraph();
    for (int index = 0; index < 3; ++index) {
        const NodeId replacement_node = replacement_document.AllocateNodeId();
        const PinId replacement_pin = replacement_document.AllocatePinId();
        Execute(replacement_commands,
                std::make_unique<AddNodeCommand>(
                    replacement_graph,
                    NodeCreation{
                        .node = NodeInstance{.id = replacement_node, .type = TypeId{"stage5.node"}},
                        .pins = {PinInstance{
                            .id = replacement_pin,
                            .node = replacement_node,
                            .key = "pin",
                            .label = "Replacement",
                            .type = TypeId{"float"},
                            .direction = index == 0 ? PinDirection::Output : PinDirection::Input,
                        }},
                    },
                    NodePresentation{.position = {1000.0f + index * 240.0f, 900.0f}}),
                replacement_document, replacement_presentation, types, "Replacement cache-identity node must be added");
    }
    const NodeId first_replacement = replacement_document.FindGraph(replacement_graph)->nodes.begin()->first;
    while (replacement_presentation.PresentationRevision() < presentation.PresentationRevision()) {
        const Vec2 before = replacement_presentation.FindNode(first_replacement)->position;
        const Vec2 after = before + Vec2{1.0f, 1.0f};
        Execute(replacement_commands,
                std::make_unique<MoveNodesCommand>(replacement_graph,
                                                   MoveNodesCommand::Positions{{first_replacement, before}},
                                                   MoveNodesCommand::Positions{{first_replacement, after}}),
                replacement_document, replacement_presentation, types,
                "Replacement document revision must match the prior presentation "
                "generation");
    }
    std::int64_t replacement_revision_step = 0;
    while (replacement_document.ModelRevision() < document.ModelRevision()) {
        Execute(replacement_commands,
                std::make_unique<SetNodePropertyCommand>(replacement_graph, first_replacement, "revision_step",
                                                         PropertyValue{replacement_revision_step++}),
                replacement_document, replacement_presentation, types,
                "Replacement model revision must match the prior document generation");
    }
    Expect(replacement_document.ModelRevision() == document.ModelRevision() &&
               replacement_presentation.PresentationRevision() == presentation.PresentationRevision(),
           "Cache identity fixture must use equal revision counters");
    document = std::move(replacement_document);
    presentation = std::move(replacement_presentation);
    commands.Clear();
    const int layouts_before_replacement = layout_calls;
    editor.ResetMetrics();
    (void)draw_frame();
    Expect(editor.Metrics().geometry_rebuilds == 1 && layout_calls == layouts_before_replacement + 3,
           "Equal revisions from different document identities must not reuse "
           "stale geometry");

    ImGui::DestroyContext();
}

void TestBreakingPolicyArchitecture() {
    struct ApprovalRequest final {
        int ticket;
    };

    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog nodes;
    RegistryCatalog& types = nodes;
    CommandStack commands;
    const GraphId graph = document.RootGraph();

    Expect(nodes.RegisterNodeType(SourceDescriptor()).has_value(), "Policy source descriptor must register");
    Expect(nodes.RegisterNodeType(SinkDescriptor()).has_value(), "Policy sink descriptor must register");
    Expect(nodes
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"policy.int-source"},
                   .display_name = "Integer source",
                   .static_pins = {PinDescriptor{
                       .key = "value",
                       .type = TypeId{"int"},
                       .direction = PinDirection::Output,
                       .cardinality = PinCardinality::Multiple,
                   }},
               })
               .has_value(),
           "Policy integer source descriptor must register");
    Expect(nodes
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"policy.converter"},
                   .display_name = "Integer to float",
                   .static_pins =
                       {
                           PinDescriptor{
                               .key = "input",
                               .type = TypeId{"int"},
                               .direction = PinDirection::Input,
                           },
                           PinDescriptor{
                               .key = "output",
                               .type = TypeId{"float"},
                               .direction = PinDirection::Output,
                               .cardinality = PinCardinality::Multiple,
                           },
                       },
               })
               .has_value(),
           "Policy converter descriptor must register");
    Expect(types
               .RegisterConversion(ConversionDescriptor{
                   .key =
                       ConversionKey{
                           .source_type = TypeId{"int"},
                           .destination_type = TypeId{"float"},
                           .kind = PinKind::Data,
                       },
                   .node_type = TypeId{"policy.converter"},
                   .input_pin = "input",
                   .output_pin = "output",
               })
               .has_value(),
           "Policy conversion must register");

    auto source = nodes.Instantiate(document, TypeId{"test.source"});
    auto sink = nodes.Instantiate(document, TypeId{"test.sink"});
    Expect(source && sink, "Policy fixture nodes must instantiate");
    const NodeId source_id = source->node.id;
    const PinId source_output = source->pins.front().id;
    const PinId sink_input = sink->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*source)), document, presentation, types,
            "Policy source must be added");
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*sink)), document, presentation, types,
            "Policy sink must be added");
    commands.Clear();

    const CommandPathEntry* shared_path_entry = nullptr;
    bool shared_path = true;
    std::size_t shared_path_leaves = 0;
    GraphPolicy path_policy;
    path_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                         const OperationIntent& operation) -> OperationPolicyDecision {
        Expect(!operation.path.empty(), "Transaction leaves must have a command path");
        const auto* entry = &operation.path[0];
        if (shared_path_entry == nullptr) shared_path_entry = entry;
        shared_path &= shared_path_entry == entry;
        ++shared_path_leaves;
        return AllowOperation{};
    };
    const NodeId path_node = document.AllocateNodeId();
    auto path_result =
        commands.Execute(std::make_unique<AddNodeCommand>(
                             graph, NodeCreation{.node = NodeInstance{.id = path_node, .type = TypeId{"policy.path"}}}),
                         document, presentation, types, path_policy);
    Expect(sizeof(OperationIntent) <= 96 && path_result && shared_path_leaves >= 2 && shared_path,
           "Operation intents must stay compact and share one immutable path per "
           "scope");
    commands.Clear();

    GraphPolicy deny_blocked_property;
    CommandPath denied_path;
    deny_blocked_property.evaluate_operation = [&](const OperationPolicyContext&,
                                                   const OperationIntent& operation) -> OperationPolicyDecision {
        Expect(!operation.path.empty(), "Final policy checks must contain "
                                        "transaction leaves with command paths");
        const auto* property = operation.Get<PropertyOperation>();
        if (operation.kind == OperationKind::SetNodeProperty && property && property->change->key == "blocked") {
            denied_path = operation.path;
            return DenyOperation{"Property is locked"};
        }
        return AllowOperation{};
    };

    std::vector<std::unique_ptr<Command>> compound_children;
    compound_children.push_back(std::make_unique<SetNodeDisplayNameCommand>(graph, source_id, "Must roll back"));
    compound_children.push_back(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "blocked", PropertyValue{std::int64_t{1}}));
    auto denied_compound =
        commands.Execute(std::make_unique<CompoundCommand>("Denied compound", std::move(compound_children)), document,
                         presentation, types, deny_blocked_property);
    Expect(!denied_compound && denied_compound.error().code == ErrorCode::PolicyRejected &&
               document.FindNode(graph, source_id)->display_name == "Source" &&
               !document.FindNode(graph, source_id)->properties.contains("blocked") && denied_path.size() == 2 &&
               denied_path[1].child == 1,
           "A denied compound leaf must roll back every sibling and report its "
           "path");

    std::vector<std::unique_ptr<Command>> nested_children;
    nested_children.push_back(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "blocked", PropertyValue{std::int64_t{2}}));
    std::vector<std::unique_ptr<Command>> outer_children;
    outer_children.push_back(std::make_unique<SetNodeDisplayNameCommand>(graph, source_id, "Still rolls back"));
    outer_children.push_back(std::make_unique<CompoundCommand>("Nested", std::move(nested_children)));
    auto denied_nested = commands.Execute(std::make_unique<CompoundCommand>("Outer", std::move(outer_children)),
                                          document, presentation, types, deny_blocked_property);
    Expect(!denied_nested && denied_nested.error().code == ErrorCode::PolicyRejected && denied_path.size() == 3 &&
               denied_path[1].child == 1 && denied_path[2].child == 0 &&
               document.FindNode(graph, source_id)->display_name == "Source",
           "Nested compound leaves must be independently authorized with a "
           "complete path");

    auto denied_custom = commands.Execute(
        std::make_unique<CustomPropertyMutationCommand>(graph, source_id, "blocked", PropertyValue{std::int64_t{3}}),
        document, presentation, types, deny_blocked_property);
    Expect(!denied_custom && denied_custom.error().code == ErrorCode::PolicyRejected &&
               !document.FindNode(graph, source_id)->properties.contains("blocked") && denied_path.size() == 1 &&
               denied_path[0].name == "Custom mutation",
           "Custom commands must not bypass transaction-generated policy leaves");

    std::optional<OperationIntent> execute_payload;
    GraphPolicy capture_payload;
    capture_payload.evaluate_operation = [&](const OperationPolicyContext& context,
                                             const OperationIntent& operation) -> OperationPolicyDecision {
        const auto* property = operation.Get<PropertyOperation>();
        if (operation.kind == OperationKind::SetNodeProperty && property && property->change->key == "payload") {
            Expect(context.phase == OperationPhase::Execute, "Execute payload must use the execute phase");
            execute_payload = operation;
        }
        return AllowOperation{};
    };
    auto payload_result = commands.Execute(
        std::make_unique<CustomPropertyMutationCommand>(graph, source_id, "payload", PropertyValue{std::int64_t{41}}),
        document, presentation, types, capture_payload);
    const auto* executed_property = execute_payload ? execute_payload->Get<PropertyOperation>() : nullptr;
    Expect(payload_result && execute_payload && executed_property && executed_property->graph == graph &&
               executed_property->node == source_id && execute_payload->action == OperationAction::Set &&
               executed_property->change->current == std::optional<PropertyValue>{PropertyValue{std::int64_t{41}}} &&
               execute_payload->path.size() == 1,
           "Execute policy must receive exact transaction-generated IDs, property "
           "value, action, and path");

    const LinkId cascade_link = document.AllocateLinkId();
    Execute(commands,
            std::make_unique<ConnectPinsCommand>(
                graph, Link{.id = cascade_link, .output = source_output, .input = sink_input}),
            document, presentation, types, "Cascade fixture link must connect");
    GraphPolicy deny_cascade_link;
    deny_cascade_link.evaluate_operation = [](const OperationPolicyContext&,
                                              const OperationIntent& operation) -> OperationPolicyDecision {
        if (operation.kind == OperationKind::Connect && operation.action == OperationAction::Erase) {
            return DenyOperation{"Cascaded link removal is denied"};
        }
        return AllowOperation{};
    };
    auto denied_cascade =
        commands.Execute(std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{source_id}), document,
                         presentation, types, deny_cascade_link);
    Expect(!denied_cascade && denied_cascade.error().code == ErrorCode::PolicyRejected &&
               document.FindNode(graph, source_id) != nullptr && document.FindLink(graph, cascade_link) != nullptr,
           "A denied cascade leaf must atomically preserve its node and link");

    auto integer_source = nodes.Instantiate(document, TypeId{"policy.int-source"});
    auto converter_sink = nodes.Instantiate(document, TypeId{"test.sink"});
    Expect(integer_source && converter_sink, "Converter policy fixtures must instantiate");
    const PinId integer_output = integer_source->pins.front().id;
    const PinId converter_input = converter_sink->pins.front().id;
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*integer_source)), document, presentation,
            types, "Integer source must be added");
    Execute(commands, std::make_unique<AddNodeCommand>(graph, std::move(*converter_sink)), document, presentation,
            types, "Converter sink must be added");
    const auto nodes_before_converter = document.FindGraph(graph)->nodes.size();
    auto converter_command =
        PrepareConnectionCommand(document, presentation, types,
                                 ConnectionRequest{graph, integer_output, converter_input, {}}, Vec2{200.0f, 100.0f});
    Expect(converter_command.has_value(), "Converter command must prepare");
    GraphPolicy deny_converter_link;
    deny_converter_link.evaluate_operation = [](const OperationPolicyContext&,
                                                const OperationIntent& operation) -> OperationPolicyDecision {
        return operation.kind == OperationKind::Connect
                   ? OperationPolicyDecision{DenyOperation{"Converter links are denied"}}
                   : OperationPolicyDecision{AllowOperation{}};
    };
    auto denied_converter =
        commands.Execute(std::move(*converter_command), document, presentation, types, deny_converter_link);
    Expect(!denied_converter && denied_converter.error().code == ErrorCode::PolicyRejected &&
               document.FindGraph(graph)->nodes.size() == nodes_before_converter,
           "Denying a converter leaf must roll back its node, pins, "
           "presentation, and links");

    bool replaced_root = false;
    int replacement_leaves = 0;
    std::optional<ErrorCode> replacement_reentrant_error;
    GraphPolicy replacement_policy;
    replacement_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                                const OperationIntent& operation) -> OperationPolicyDecision {
        const auto* property = operation.Get<PropertyOperation>();
        if (property && property->change->key.starts_with("replacement-")) {
            ++replacement_leaves;
            Expect(operation.path.size() == 2, "Replacement compound leaves must be restaged with child paths");
        }
        return AllowOperation{};
    };
    replacement_policy.evaluate_batch = [&](const BatchPolicyContext&,
                                            const std::span<const OperationIntent> batch) -> BatchPolicyDecision {
        const bool replace = !replaced_root && std::ranges::any_of(batch, [](const OperationIntent& operation) {
            const auto* property = operation.Get<PropertyOperation>();
            return property && property->change->key == "replace-original";
        });
        if (!replace) return AllowBatch{};
        replaced_root = true;
        return ReplaceBatch{[&, graph, source_id] {
            auto nested = commands.Undo(document, presentation, types);
            if (!nested) replacement_reentrant_error = nested.error().code;
            std::vector<std::unique_ptr<Command>> replacement;
            replacement.push_back(std::make_unique<SetNodePropertyCommand>(graph, source_id, "replacement-a",
                                                                           PropertyValue{std::int64_t{1}}));
            replacement.push_back(std::make_unique<SetNodePropertyCommand>(graph, source_id, "replacement-b",
                                                                           PropertyValue{std::int64_t{2}}));
            return std::make_unique<CompoundCommand>("Replacement compound", std::move(replacement));
        }};
    };
    auto replaced = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "replace-original", PropertyValue{std::int64_t{9}}),
        document, presentation, types, replacement_policy);
    Expect(replaced && replacement_leaves == 2 && replacement_reentrant_error == ErrorCode::CommandFailed &&
               !document.FindNode(graph, source_id)->properties.contains("replace-original") &&
               document.FindNode(graph, source_id)->properties.contains("replacement-a") &&
               document.FindNode(graph, source_id)->properties.contains("replacement-b") &&
               commands.UndoName() == "Replacement compound",
           "Replacement must replace the root request and fully restage and "
           "authorize its compound");

    GraphPolicy null_replacement;
    null_replacement.evaluate_batch = [](const BatchPolicyContext&,
                                         std::span<const OperationIntent>) -> BatchPolicyDecision {
        return ReplaceBatch{[] { return std::unique_ptr<Command>{}; }};
    };
    auto null_result = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "null-replacement", PropertyValue{true}), document,
        presentation, types, null_replacement);
    Expect(!null_result && null_result.error().code == ErrorCode::PolicyRejected,
           "A null replacement must fail closed");

    GraphPolicy throwing_replacement;
    throwing_replacement.evaluate_batch = [](const BatchPolicyContext&,
                                             std::span<const OperationIntent>) -> BatchPolicyDecision {
        return ReplaceBatch{
            []() -> std::unique_ptr<Command> { throw std::runtime_error("replacement factory failed"); }};
    };
    auto throwing_result = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "throwing-replacement", PropertyValue{true}),
        document, presentation, types, throwing_replacement);
    Expect(!throwing_result && throwing_result.error().code == ErrorCode::CommandFailed &&
               throwing_result.error().message.find("replacement factory failed") != std::string::npos,
           "Replacement factory exceptions must be contained");

    GraphPolicy malformed_leaf;
    malformed_leaf.evaluate_operation = [](const OperationPolicyContext&,
                                           const OperationIntent&) -> OperationPolicyDecision {
        return DeferOperation{std::any{}};
    };
    auto empty_leaf_defer = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "empty-leaf-defer", PropertyValue{true}), document,
        presentation, types, malformed_leaf);
    Expect(!empty_leaf_defer && empty_leaf_defer.error().code == ErrorCode::PolicyRejected && !commands.HasPending(),
           "An empty leaf defer request must fail closed as a normalized denial");
    GraphPolicy malformed_batch;
    malformed_batch.evaluate_batch = [](const BatchPolicyContext&,
                                        std::span<const OperationIntent>) -> BatchPolicyDecision {
        return DeferBatch{std::any{}};
    };
    auto empty_batch_defer = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "empty-batch-defer", PropertyValue{true}), document,
        presentation, types, malformed_batch);
    Expect(!empty_batch_defer && empty_batch_defer.error().code == ErrorCode::PolicyRejected && !commands.HasPending(),
           "An empty batch defer request must fail closed as a normalized denial");
    GraphPolicy throwing_callbacks;
    throwing_callbacks.evaluate_operation = [](const OperationPolicyContext&,
                                               const OperationIntent&) -> OperationPolicyDecision {
        throw std::runtime_error("leaf callback failed");
    };
    auto throwing_leaf = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "throwing-leaf", PropertyValue{true}), document,
        presentation, types, throwing_callbacks);
    Expect(!throwing_leaf && throwing_leaf.error().code == ErrorCode::PolicyRejected &&
               throwing_leaf.error().message.find("leaf callback failed") != std::string::npos,
           "Leaf policy callback exceptions must be normalized into denials");
    throwing_callbacks.evaluate_operation = {};
    throwing_callbacks.evaluate_batch = [](const BatchPolicyContext&,
                                           std::span<const OperationIntent>) -> BatchPolicyDecision {
        throw std::runtime_error("batch callback failed");
    };
    auto throwing_batch = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "throwing-batch", PropertyValue{true}), document,
        presentation, types, throwing_callbacks);
    Expect(!throwing_batch && throwing_batch.error().code == ErrorCode::PolicyRejected &&
               throwing_batch.error().message.find("batch callback failed") != std::string::npos,
           "Batch policy callback exceptions must be normalized into denials");

    GraphPolicy looping_replacement;
    looping_replacement.evaluate_batch = [graph, source_id](const BatchPolicyContext&,
                                                            std::span<const OperationIntent>) -> BatchPolicyDecision {
        return ReplaceBatch{[graph, source_id] {
            return std::make_unique<SetNodePropertyCommand>(graph, source_id, "replacement-loop", PropertyValue{true});
        }};
    };
    auto looping_result = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "replacement-loop", PropertyValue{true}), document,
        presentation, types, looping_replacement);
    Expect(!looping_result && looping_result.error().code == ErrorCode::PolicyRejected &&
               !document.FindNode(graph, source_id)->properties.contains("replacement-loop"),
           "Replacement recursion must stop at a bounded depth without committing");

    GraphPolicy defer_policy;
    defer_policy.evaluate_operation = [](const OperationPolicyContext&,
                                         const OperationIntent& operation) -> OperationPolicyDecision {
        const auto* property = operation.Get<PropertyOperation>();
        if (operation.kind == OperationKind::SetNodeProperty && property &&
            property->change->key.starts_with("defer-")) {
            return DeferOperation{ApprovalRequest{73}};
        }
        return AllowOperation{};
    };
    auto deferred = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "defer-resume", PropertyValue{std::int64_t{5}}),
        document, presentation, types, defer_policy);
    Expect(deferred && deferred->deferred && commands.HasPending() && commands.PendingOperation() != nullptr &&
               commands.PendingOperation()->id == deferred->deferred->id &&
               !document.FindNode(graph, source_id)->properties.contains("defer-resume") &&
               deferred->deferred->batch.size() == 1 && deferred->deferred->requests.size() == 1 &&
               deferred->deferred->requests[0].operation_index == 0 &&
               deferred->deferred->requests[0].path == deferred->deferred->batch[0].path &&
               std::any_cast<ApprovalRequest>(&deferred->deferred->requests[0].request) != nullptr &&
               std::any_cast<ApprovalRequest>(&deferred->deferred->requests[0].request)->ticket == 73,
           "Defer must return a typed request and retain the exact uncommitted "
           "batch");
    const DeferredOperationId resume_id = deferred->deferred->id;
    auto invalid_resume_mode = commands.Resume(resume_id, document, presentation, static_cast<ResumeMode>(255));
    Expect(!invalid_resume_mode && invalid_resume_mode.error().code == ErrorCode::InvalidArgument &&
               commands.HasPending(),
           "An invalid resume mode must fail closed without consuming the "
           "prepared operation");
    auto blocked_execute = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "while-pending", PropertyValue{true}), document,
        presentation, types);
    auto blocked_undo = commands.Undo(document, presentation, types);
    auto blocked_redo = commands.Redo(document, presentation, types);
    Expect(!blocked_execute && blocked_execute.error().code == ErrorCode::OperationPending && !blocked_undo &&
               blocked_undo.error().code == ErrorCode::OperationPending && !blocked_redo &&
               blocked_redo.error().code == ErrorCode::OperationPending,
           "Execute, undo, and redo must all be blocked while one operation is "
           "pending");
    auto resumed = commands.Resume(resume_id, document, presentation, ResumeMode::CommitPrepared);
    Expect(resumed && !resumed->deferred && !commands.HasPending() &&
               document.FindNode(graph, source_id)->properties.contains("defer-resume"),
           "Resume must commit the prepared transaction without restaging");
    auto duplicate_resume = commands.Resume(resume_id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!duplicate_resume && duplicate_resume.error().code == ErrorCode::DeferredOperationNotFound,
           "A deferred operation must not be resumed twice");

    auto cancelled = commands.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "defer-cancel", PropertyValue{true}), document,
        presentation, types, defer_policy);
    Expect(cancelled && cancelled->deferred, "Cancellation fixture must defer");
    const DeferredOperationId cancel_id = cancelled->deferred->id;
    Expect(commands.Cancel(cancel_id).has_value() && !commands.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("defer-cancel"),
           "Cancel must discard prepared state without changing history or the "
           "document");
    auto duplicate_cancel = commands.Cancel(cancel_id);
    Expect(!duplicate_cancel && duplicate_cancel.error().code == ErrorCode::DeferredOperationNotFound,
           "A deferred operation must not be cancelled twice");

    const auto property_compound = [graph, source_id](std::string name, std::string first, std::string second) {
        std::vector<std::unique_ptr<Command>> children;
        children.push_back(std::make_unique<SetNodePropertyCommand>(graph, source_id, std::move(first),
                                                                    PropertyValue{std::int64_t{1}}));
        children.push_back(std::make_unique<SetNodePropertyCommand>(graph, source_id, std::move(second),
                                                                    PropertyValue{std::int64_t{2}}));
        return std::make_unique<CompoundCommand>(std::move(name), std::move(children));
    };

    std::size_t mixed_execute_callbacks = 0;
    GraphPolicy mixed_execute_policy;
    mixed_execute_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                                  const OperationIntent& operation) -> OperationPolicyDecision {
        ++mixed_execute_callbacks;
        const auto* property = operation.Get<PropertyOperation>();
        if (property && property->change->key == "mixed-execute-defer") {
            return DeferOperation{ApprovalRequest{101}};
        }
        if (property && property->change->key == "mixed-execute-deny") {
            return DenyOperation{"Later execute leaf denied"};
        }
        return AllowOperation{};
    };
    auto mixed_execute =
        commands.Execute(property_compound("Mixed execute", "mixed-execute-defer", "mixed-execute-deny"), document,
                         presentation, types, mixed_execute_policy);
    Expect(!mixed_execute && mixed_execute.error().code == ErrorCode::PolicyRejected && mixed_execute_callbacks == 2 &&
               !commands.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("mixed-execute-defer") &&
               !document.FindNode(graph, source_id)->properties.contains("mixed-execute-deny"),
           "A later execute denial must override an earlier defer after "
           "evaluating every leaf");

    GraphPolicy multiple_defer_policy;
    multiple_defer_policy.evaluate_operation = [](const OperationPolicyContext&,
                                                  const OperationIntent& operation) -> OperationPolicyDecision {
        const auto* property = operation.Get<PropertyOperation>();
        if (!property) return AllowOperation{};
        if (property->change->key == "multi-defer-a") return DeferOperation{ApprovalRequest{201}};
        if (property->change->key == "multi-defer-b") return DeferOperation{ApprovalRequest{202}};
        return AllowOperation{};
    };
    auto multiple_deferred = commands.Execute(property_compound("Multiple defer", "multi-defer-a", "multi-defer-b"),
                                              document, presentation, types, multiple_defer_policy);
    Expect(multiple_deferred && multiple_deferred->deferred && multiple_deferred->deferred->requests.size() == 2 &&
               multiple_deferred->deferred->requests[0].operation_index == 0 &&
               multiple_deferred->deferred->requests[1].operation_index == 1 &&
               multiple_deferred->deferred->requests[0].path[1].child == 0 &&
               multiple_deferred->deferred->requests[1].path[1].child == 1 &&
               std::any_cast<ApprovalRequest>(&multiple_deferred->deferred->requests[0].request)->ticket == 201 &&
               std::any_cast<ApprovalRequest>(&multiple_deferred->deferred->requests[1].request)->ticket == 202,
           "Every deferred leaf request must remain visible with its batch index "
           "and path");
    Expect(commands.Resume(multiple_deferred->deferred->id, document, presentation, ResumeMode::CommitPrepared)
                   .has_value() &&
               document.FindNode(graph, source_id)->properties.contains("multi-defer-a") &&
               document.FindNode(graph, source_id)->properties.contains("multi-defer-b"),
           "Resume must explicitly approve all deferred leaf requests together");

    GraphPolicy batch_defer_policy;
    batch_defer_policy.evaluate_batch = [](const BatchPolicyContext&,
                                           std::span<const OperationIntent>) -> BatchPolicyDecision {
        return DeferBatch{ApprovalRequest{211}};
    };
    auto batch_deferred =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(graph, source_id, "batch-defer", PropertyValue{true}),
                         document, presentation, types, batch_defer_policy);
    Expect(batch_deferred && batch_deferred->deferred && batch_deferred->deferred->requests.size() == 1 &&
               batch_deferred->deferred->requests.front().scope == DeferredRequestScope::Batch &&
               !batch_deferred->deferred->requests.front().operation_index &&
               std::any_cast<ApprovalRequest>(&batch_deferred->deferred->requests.front().request)->ticket == 211,
           "Batch defer must retain one typed batch-scoped request");
    GraphPolicy defer_batch_again;
    defer_batch_again.evaluate_batch = [](const BatchPolicyContext& context,
                                          std::span<const OperationIntent>) -> BatchPolicyDecision {
        return context.pass == PolicyEvaluationPass::Resume ? BatchPolicyDecision{DeferBatch{ApprovalRequest{212}}}
                                                            : BatchPolicyDecision{AllowBatch{}};
    };
    auto deferred_again = commands.Resume(batch_deferred->deferred->id, document, presentation, ResumeMode::Reauthorize,
                                          defer_batch_again);
    Expect(deferred_again && deferred_again->deferred && commands.HasPending() &&
               deferred_again->deferred->requests.size() == 1 &&
               std::any_cast<ApprovalRequest>(&deferred_again->deferred->requests.front().request)->ticket == 212 &&
               !document.FindNode(graph, source_id)->properties.contains("batch-defer"),
           "Repeated deferral during reauthorization must replace retained "
           "requests without committing");
    GraphPolicy allow_batch_resume;
    allow_batch_resume.evaluate_batch =
        [](const BatchPolicyContext&, std::span<const OperationIntent>) -> BatchPolicyDecision { return AllowBatch{}; };
    Expect(commands.Resume(batch_deferred->deferred->id, document, presentation, ResumeMode::Reauthorize,
                           allow_batch_resume)
                   .has_value() &&
               document.FindNode(graph, source_id)->properties.contains("batch-defer"),
           "A re-deferred batch must commit only after a later reauthorization "
           "allows it");

    bool priority_replacement_created = false;
    std::size_t priority_callbacks = 0;
    GraphPolicy replace_over_defer;
    replace_over_defer.evaluate_operation = [&](const OperationPolicyContext&,
                                                const OperationIntent& operation) -> OperationPolicyDecision {
        ++priority_callbacks;
        const auto* property = operation.Get<PropertyOperation>();
        if (!property) return AllowOperation{};
        if (property->change->key == "priority-defer") return DeferOperation{ApprovalRequest{301}};
        return AllowOperation{};
    };
    replace_over_defer.evaluate_batch = [&](const BatchPolicyContext&,
                                            const std::span<const OperationIntent> batch) -> BatchPolicyDecision {
        const bool replace = std::ranges::any_of(batch, [](const OperationIntent& operation) {
            const auto* property = operation.Get<PropertyOperation>();
            return property && property->change->key == "priority-replace";
        });
        if (!replace) return AllowBatch{};
        return ReplaceBatch{[&] {
            priority_replacement_created = true;
            return std::make_unique<SetNodePropertyCommand>(graph, source_id, "priority-winner", PropertyValue{true});
        }};
    };
    auto priority_replaced =
        commands.Execute(property_compound("Replace over defer", "priority-defer", "priority-replace"), document,
                         presentation, types, replace_over_defer);
    Expect(priority_replaced && priority_replacement_created && priority_callbacks == 3 && !commands.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("priority-defer") &&
               !document.FindNode(graph, source_id)->properties.contains("priority-replace") &&
               document.FindNode(graph, source_id)->properties.contains("priority-winner"),
           "One replacement must override deferred leaves only after the "
           "complete batch is evaluated");

    GraphPolicy conflicting_replacements;
    conflicting_replacements.evaluate_batch =
        [graph, source_id](const BatchPolicyContext&,
                           const std::span<const OperationIntent> batch) -> BatchPolicyDecision {
        const auto properties = std::ranges::count_if(
            batch, [](const OperationIntent& operation) { return operation.kind == OperationKind::SetNodeProperty; });
        if (properties < 2) return AllowBatch{};
        return ReplaceBatch{[graph, source_id] {
            return std::make_unique<SetNodePropertyCommand>(graph, source_id, "batch-replacement", PropertyValue{true});
        }};
    };
    auto conflicting =
        commands.Execute(property_compound("Conflicting replacements", "replace-conflict-a", "replace-conflict-b"),
                         document, presentation, types, conflicting_replacements);
    Expect(conflicting && !commands.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("replace-conflict-a") &&
               !document.FindNode(graph, source_id)->properties.contains("replace-conflict-b") &&
               document.FindNode(graph, source_id)->properties.contains("batch-replacement"),
           "One batch replacement must handle any number of matching leaf "
           "operations");

    bool denied_replacement_created = false;
    std::size_t deny_over_replace_callbacks = 0;
    GraphPolicy deny_over_replace;
    deny_over_replace.evaluate_operation = [&](const OperationPolicyContext&,
                                               const OperationIntent& operation) -> OperationPolicyDecision {
        ++deny_over_replace_callbacks;
        const auto* property = operation.Get<PropertyOperation>();
        if (property && property->change->key == "deny-priority-deny") {
            return DenyOperation{"Deny overrides replacement"};
        }
        return AllowOperation{};
    };
    deny_over_replace.evaluate_batch = [&](const BatchPolicyContext&,
                                           std::span<const OperationIntent>) -> BatchPolicyDecision {
        return ReplaceBatch{[&] {
            denied_replacement_created = true;
            return std::make_unique<SetNodePropertyCommand>(graph, source_id, "denied-replacement",
                                                            PropertyValue{true});
        }};
    };
    auto denied_replacement =
        commands.Execute(property_compound("Deny over replacement", "deny-priority-replace", "deny-priority-deny"),
                         document, presentation, types, deny_over_replace);
    Expect(!denied_replacement && denied_replacement.error().code == ErrorCode::PolicyRejected &&
               deny_over_replace_callbacks == 2 && !denied_replacement_created && !commands.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("denied-replacement"),
           "A denial must override replacement without invoking its factory");

    std::size_t cascade_callbacks = 0;
    bool cascade_deferred = false;
    bool cascade_denied = false;
    bool cascade_checked_after_deny = false;
    GraphPolicy mixed_cascade_policy;
    mixed_cascade_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                                  const OperationIntent& operation) -> OperationPolicyDecision {
        ++cascade_callbacks;
        if (!cascade_deferred && operation.kind == OperationKind::Connect &&
            operation.action == OperationAction::Erase) {
            cascade_deferred = true;
            return DeferOperation{ApprovalRequest{401}};
        }
        if (operation.kind == OperationKind::DeleteElements) {
            cascade_denied = true;
            return DenyOperation{"Later cascade leaf denied"};
        }
        if (cascade_denied) cascade_checked_after_deny = true;
        return AllowOperation{};
    };
    auto mixed_cascade =
        commands.Execute(std::make_unique<DeleteElementsCommand>(graph, std::vector<NodeId>{source_id}), document,
                         presentation, types, mixed_cascade_policy);
    Expect(!mixed_cascade && mixed_cascade.error().code == ErrorCode::PolicyRejected && cascade_callbacks >= 4 &&
               cascade_deferred && cascade_denied && cascade_checked_after_deny && !commands.HasPending() &&
               document.FindNode(graph, source_id) != nullptr && document.FindLink(graph, cascade_link) != nullptr,
           "Cascade authorization must continue after defer and deny, with "
           "denial rolling back the complete cascade");

    CommandStack mixed_history;
    Execute(mixed_history, property_compound("Mixed history", "mixed-history-a", "mixed-history-b"), document,
            presentation, types, "Mixed history fixture must execute");
    std::size_t mixed_undo_callbacks = 0;
    GraphPolicy mixed_undo_policy;
    mixed_undo_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                               const OperationIntent& operation) -> OperationPolicyDecision {
        ++mixed_undo_callbacks;
        const auto* property = operation.Get<PropertyOperation>();
        if (property && property->change->key == "mixed-history-b") return DeferOperation{ApprovalRequest{501}};
        if (property && property->change->key == "mixed-history-a") return DenyOperation{"Later undo leaf denied"};
        return AllowOperation{};
    };
    auto mixed_undo =
        mixed_history.Undo(document, presentation, mixed_undo_policy, UndoPolicyMode::RespectCurrentPolicy, types);
    Expect(!mixed_undo && mixed_undo.error().code == ErrorCode::PolicyRejected && mixed_undo_callbacks == 2 &&
               !mixed_history.HasPending() &&
               document.FindNode(graph, source_id)->properties.contains("mixed-history-a") &&
               document.FindNode(graph, source_id)->properties.contains("mixed-history-b"),
           "A later undo denial must override an earlier defer without advancing "
           "history");
    GraphPolicy allow_all;
    allow_all.evaluate_operation = [](const OperationPolicyContext&,
                                      const OperationIntent&) -> OperationPolicyDecision { return AllowOperation{}; };
    Expect(
        mixed_history.Undo(document, presentation, allow_all, UndoPolicyMode::RespectCurrentPolicy, types).has_value(),
        "Mixed history fixture must become redoable");
    std::size_t mixed_redo_callbacks = 0;
    GraphPolicy mixed_redo_policy;
    mixed_redo_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                               const OperationIntent& operation) -> OperationPolicyDecision {
        ++mixed_redo_callbacks;
        const auto* property = operation.Get<PropertyOperation>();
        if (property && property->change->key == "mixed-history-a") return DeferOperation{ApprovalRequest{601}};
        if (property && property->change->key == "mixed-history-b") return DenyOperation{"Later redo leaf denied"};
        return AllowOperation{};
    };
    auto mixed_redo = mixed_history.Redo(document, presentation, types, mixed_redo_policy);
    Expect(!mixed_redo && mixed_redo.error().code == ErrorCode::PolicyRejected && mixed_redo_callbacks == 2 &&
               !mixed_history.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("mixed-history-a") &&
               !document.FindNode(graph, source_id)->properties.contains("mixed-history-b"),
           "A later redo denial must override an earlier defer without advancing "
           "history");

    CommandStack stale_stack;
    auto stale_deferred = stale_stack.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "defer-stale", PropertyValue{true}), document,
        presentation, types, defer_policy);
    Expect(stale_deferred && stale_deferred->deferred, "Stale revision fixture must defer");
    CommandStack external;
    Execute(external,
            std::make_unique<SetNodePropertyCommand>(graph, source_id, "external-change", PropertyValue{true}),
            document, presentation, types, "External change must commit");
    auto stale_resume =
        stale_stack.Resume(stale_deferred->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!stale_resume && stale_resume.error().code == ErrorCode::RevisionConflict && stale_stack.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("defer-stale"),
           "Resume must reject stale revisions and keep the prepared operation "
           "cancellable");
    Expect(stale_stack.Cancel(stale_deferred->deferred->id).has_value(),
           "A stale prepared operation must remain cancellable");

    CommandStack identity_stack;
    auto identity_deferred = identity_stack.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "defer-identity", PropertyValue{true}), document,
        presentation, types, defer_policy);
    GraphDocument other_document;
    GraphPresentation other_presentation;
    auto wrong_identity = identity_stack.Resume(identity_deferred->deferred->id, other_document, other_presentation,
                                                ResumeMode::CommitPrepared);
    Expect(!wrong_identity && wrong_identity.error().code == ErrorCode::CommandFailed &&
               identity_stack.Cancel(identity_deferred->deferred->id).has_value(),
           "Resume must reject a different document identity without consuming "
           "pending work");

    std::optional<GraphDocument> moved_document;
    std::optional<GraphPresentation> moved_presentation;
    CommandStack moved_stack;
    DeferredOperationId moved_operation;
    GraphId moved_graph;
    NodeId moved_node;
    {
        GraphDocument old_document;
        GraphPresentation old_presentation;
        moved_graph = old_document.RootGraph();
        auto moved_creation = nodes.Instantiate(old_document, TypeId{"test.source"});
        Expect(moved_creation.has_value(), "Moved-owner fixture must instantiate");
        moved_node = moved_creation->node.id;
        Execute(moved_stack, std::make_unique<AddNodeCommand>(moved_graph, std::move(*moved_creation)), old_document,
                old_presentation, types, "Moved-owner fixture node must execute");
        GraphPolicy moved_defer;
        moved_defer.evaluate_operation = [](const OperationPolicyContext&,
                                            const OperationIntent& operation) -> OperationPolicyDecision {
            const auto* property = operation.Get<PropertyOperation>();
            return property && property->change->key == "moved-owner"
                       ? OperationPolicyDecision{DeferOperation{ApprovalRequest{701}}}
                       : OperationPolicyDecision{AllowOperation{}};
        };
        auto pending_move = moved_stack.Execute(
            std::make_unique<SetNodePropertyCommand>(moved_graph, moved_node, "moved-owner", PropertyValue{true}),
            old_document, old_presentation, types, moved_defer);
        Expect(pending_move && pending_move->deferred, "Moved-owner fixture must defer");
        moved_operation = pending_move->deferred->id;
        moved_document.emplace(std::move(old_document));
        moved_presentation.emplace(std::move(old_presentation));
    }
    auto moved_resume =
        moved_stack.Resume(moved_operation, *moved_document, *moved_presentation, ResumeMode::CommitPrepared);
    Expect(moved_resume && !moved_stack.HasPending() &&
               moved_document->FindNode(moved_graph, moved_node)->properties.contains("moved-owner"),
           "Resume must rebind a prepared transaction after its document, "
           "presentation, and stack are moved");

    CommandStack history;
    Execute(
        history,
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "history-policy", PropertyValue{std::int64_t{88}}),
        document, presentation, types, "History policy fixture must execute");
    bool saw_inverse = false;
    GraphPolicy deny_inverse;
    deny_inverse.evaluate_operation = [&](const OperationPolicyContext& context,
                                          const OperationIntent& operation) -> OperationPolicyDecision {
        const auto* property = operation.Get<PropertyOperation>();
        if (operation.kind == OperationKind::SetNodeProperty && property && property->change->key == "history-policy") {
            saw_inverse = context.phase == OperationPhase::Undo && operation.action == OperationAction::Erase &&
                          property->change->previous == std::optional<PropertyValue>{PropertyValue{std::int64_t{88}}};
            return DenyOperation{"Current policy denies inverse"};
        }
        return AllowOperation{};
    };
    auto denied_undo = history.Undo(document, presentation, deny_inverse, UndoPolicyMode::RespectCurrentPolicy, types);
    Expect(!denied_undo && denied_undo.error().code == ErrorCode::PolicyRejected && saw_inverse &&
               document.FindNode(graph, source_id)->properties.contains("history-policy"),
           "RespectCurrentPolicy undo must authorize the staged inverse batch");
    GraphPolicy allow_history;
    allow_history.evaluate_operation = [](const OperationPolicyContext&,
                                          const OperationIntent&) -> OperationPolicyDecision {
        return AllowOperation{};
    };
    Expect(
        history.Undo(document, presentation, allow_history, UndoPolicyMode::RespectCurrentPolicy, types).has_value() &&
            !document.FindNode(graph, source_id)->properties.contains("history-policy"),
        "Allowed policy-aware undo must commit its inverse batch");

    bool saw_forward = false;
    GraphPolicy deny_forward;
    deny_forward.evaluate_operation = [&](const OperationPolicyContext& context,
                                          const OperationIntent& operation) -> OperationPolicyDecision {
        const auto* property = operation.Get<PropertyOperation>();
        if (operation.kind == OperationKind::SetNodeProperty && property && property->change->key == "history-policy") {
            saw_forward = context.phase == OperationPhase::Redo && operation.action == OperationAction::Set &&
                          property->change->current == std::optional<PropertyValue>{PropertyValue{std::int64_t{88}}};
            return DenyOperation{"Current policy denies forward"};
        }
        return AllowOperation{};
    };
    auto denied_redo = history.Redo(document, presentation, types, deny_forward);
    Expect(!denied_redo && denied_redo.error().code == ErrorCode::PolicyRejected && saw_forward &&
               !document.FindNode(graph, source_id)->properties.contains("history-policy"),
           "Redo must authorize the staged forward batch");
    Expect(history.Redo(document, presentation, types, allow_history).has_value() &&
               document.FindNode(graph, source_id)->properties.contains("history-policy"),
           "Allowed policy-aware redo must commit its forward batch");
    int restore_callbacks = 0;
    GraphPolicy deny_everything;
    deny_everything.evaluate_operation = [&](const OperationPolicyContext&,
                                             const OperationIntent&) -> OperationPolicyDecision {
        ++restore_callbacks;
        return DenyOperation{"Must be bypassed"};
    };
    Expect(history.Undo(document, presentation, deny_everything, UndoPolicyMode::RestoreHistory, types).has_value() &&
               restore_callbacks == 0,
           "RestoreHistory undo must bypass application policy");

    GraphPolicy defer_history;
    defer_history.evaluate_operation = [](const OperationPolicyContext&,
                                          const OperationIntent& operation) -> OperationPolicyDecision {
        if (operation.kind == OperationKind::SetNodeProperty) return DeferOperation{ApprovalRequest{91}};
        return AllowOperation{};
    };
    auto deferred_redo = history.Redo(document, presentation, types, defer_history);
    Expect(deferred_redo && deferred_redo->deferred && deferred_redo->deferred->phase == OperationPhase::Redo &&
               !document.FindNode(graph, source_id)->properties.contains("history-policy"),
           "Deferred redo must retain its staged forward batch");
    bool reauthorized_redo = false;
    bool reauthorized_undo = false;
    GraphPolicy reauthorize_history;
    reauthorize_history.evaluate_batch = [&](const BatchPolicyContext& context,
                                             std::span<const OperationIntent>) -> BatchPolicyDecision {
        reauthorized_redo |= context.pass == PolicyEvaluationPass::Resume && context.phase == OperationPhase::Redo;
        reauthorized_undo |= context.pass == PolicyEvaluationPass::Resume && context.phase == OperationPhase::Undo;
        return AllowBatch{};
    };
    Expect(history.Resume(deferred_redo->deferred->id, document, presentation, ResumeMode::Reauthorize,
                          reauthorize_history)
                   .has_value() &&
               reauthorized_redo && document.FindNode(graph, source_id)->properties.contains("history-policy"),
           "Reauthorizing deferred redo must advance history only after the "
           "current policy allows it");
    auto deferred_undo =
        history.Undo(document, presentation, defer_history, UndoPolicyMode::RespectCurrentPolicy, types);
    Expect(deferred_undo && deferred_undo->deferred && deferred_undo->deferred->phase == OperationPhase::Undo &&
               document.FindNode(graph, source_id)->properties.contains("history-policy"),
           "Deferred undo must retain its staged inverse batch");
    Expect(history.Resume(deferred_undo->deferred->id, document, presentation, ResumeMode::Reauthorize,
                          reauthorize_history)
                   .has_value() &&
               reauthorized_undo && !document.FindNode(graph, source_id)->properties.contains("history-policy"),
           "Reauthorizing deferred undo must advance history only after the "
           "current policy allows it");

    Execute(history, std::make_unique<SetNodePropertyCommand>(graph, source_id, "reentrant-undo", PropertyValue{true}),
            document, presentation, types, "Reentrant undo fixture must execute");
    std::optional<ErrorCode> nested_undo_error;
    GraphPolicy reentrant_undo_policy;
    reentrant_undo_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                                   const OperationIntent&) -> OperationPolicyDecision {
        auto nested = history.Undo(document, presentation, types);
        if (!nested) nested_undo_error = nested.error().code;
        return AllowOperation{};
    };
    auto outer_undo =
        history.Undo(document, presentation, reentrant_undo_policy, UndoPolicyMode::RespectCurrentPolicy, types);
    Expect(outer_undo && nested_undo_error == ErrorCode::CommandFailed &&
               !document.FindNode(graph, source_id)->properties.contains("reentrant-undo"),
           "Busy protection must cover policy-aware undo callbacks without "
           "corrupting history");

    bool saw_operation_position = false;
    bool saw_staged_batch = false;
    CommandStack batch_stack;
    GraphPolicy staged_policy;
    staged_policy.evaluate_operation = [&](const OperationPolicyContext& context,
                                           const OperationIntent&) -> OperationPolicyDecision {
        saw_operation_position =
            context.operation_index == 0 && context.batch_size == 1 && context.pass == PolicyEvaluationPass::Initial;
        return AllowOperation{};
    };
    staged_policy.evaluate_batch = [&](const BatchPolicyContext& context,
                                       std::span<const OperationIntent> batch) -> BatchPolicyDecision {
        const auto* before = context.before_document.FindNode(graph, source_id);
        const auto* staged = context.staged_document.FindNode(graph, source_id);
        saw_staged_batch = context.batch_size == batch.size() && batch.size() == 1 && before != nullptr &&
                           staged != nullptr && !before->properties.contains("batch-staged") &&
                           staged->properties.contains("batch-staged");
        return AllowBatch{};
    };
    auto staged_result = batch_stack.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "batch-staged", PropertyValue{true}), document,
        presentation, types, staged_policy);
    Expect(staged_result.has_value(), "Batch staged-state fixture must execute");
    Expect(saw_operation_position && saw_staged_batch,
           "Leaf and batch policy contexts must expose exact batch position and "
           "before/staged state");

    GraphPolicy reauthorize_defer;
    reauthorize_defer.evaluate_operation = [](const OperationPolicyContext&,
                                              const OperationIntent& operation) -> OperationPolicyDecision {
        const auto* property = operation.Get<PropertyOperation>();
        return property && property->change->key == "resume-reauthorize"
                   ? OperationPolicyDecision{DeferOperation{ApprovalRequest{902}}}
                   : OperationPolicyDecision{AllowOperation{}};
    };
    auto reauthorize_pending = batch_stack.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "resume-reauthorize", PropertyValue{true}), document,
        presentation, types, reauthorize_defer);
    Expect(reauthorize_pending && reauthorize_pending->deferred, "Resume reauthorization fixture must defer");
    bool saw_resume_pass = false;
    GraphPolicy deny_resume;
    deny_resume.evaluate_batch = [&](const BatchPolicyContext& context,
                                     std::span<const OperationIntent>) -> BatchPolicyDecision {
        saw_resume_pass = context.pass == PolicyEvaluationPass::Resume &&
                          context.staged_document.FindNode(graph, source_id)->properties.contains("resume-reauthorize");
        return DenyBatch{"Runtime mode now denies edits"};
    };
    auto denied_resume = batch_stack.Resume(reauthorize_pending->deferred->id, document, presentation,
                                            ResumeMode::Reauthorize, deny_resume);
    Expect(!denied_resume && denied_resume.error().code == ErrorCode::PolicyRejected && saw_resume_pass &&
               batch_stack.HasPending() &&
               !document.FindNode(graph, source_id)->properties.contains("resume-reauthorize"),
           "Resume reauthorization denial must retain the prepared transaction "
           "without committing");
    GraphPolicy allow_resume;
    allow_resume.evaluate_batch = [](const BatchPolicyContext&,
                                     std::span<const OperationIntent>) -> BatchPolicyDecision { return AllowBatch{}; };
    Expect(batch_stack
                   .Resume(reauthorize_pending->deferred->id, document, presentation, ResumeMode::Reauthorize,
                           allow_resume)
                   .has_value() &&
               document.FindNode(graph, source_id)->properties.contains("resume-reauthorize"),
           "Resume reauthorization must commit only after the current policy "
           "allows the retained batch");

    CommandStack limited{CommandStack::Options{
        .history_limit = 0,
        .max_policy_batch_operations = 1,
        .max_replacements = 2,
    }};
    GraphPolicy active_policy;
    active_policy.evaluate_operation = [](const OperationPolicyContext&,
                                          const OperationIntent&) -> OperationPolicyDecision {
        return AllowOperation{};
    };
    auto oversized = limited.Execute(property_compound("Limited batch", "limited-a", "limited-b"), document,
                                     presentation, types, active_policy);
    Expect(!oversized && oversized.error().code == ErrorCode::SizeLimitExceeded &&
               !document.FindNode(graph, source_id)->properties.contains("limited-a") &&
               !document.FindNode(graph, source_id)->properties.contains("limited-b"),
           "Policy batch limits must reject oversized mutation batches atomically");

    CommandStack early_limit{CommandStack::Options{
        .history_limit = 0,
        .max_policy_batch_operations = 8,
        .max_replacements = 0,
    }};
    std::size_t mutation_attempts = 0;
    std::size_t policy_calls = 0;
    GraphPolicy counted_policy;
    counted_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                            const OperationIntent&) -> OperationPolicyDecision {
        ++policy_calls;
        return AllowOperation{};
    };
    ResetTransactionMetrics();
    auto early_abort = early_limit.Execute(std::make_unique<BudgetProbeCommand>(graph, source_id, mutation_attempts),
                                           document, presentation, types, counted_policy);
    const auto budget_metrics = GetTransactionMetrics();
    Expect(!early_abort && early_abort.error().code == ErrorCode::SizeLimitExceeded && mutation_attempts == 9 &&
               policy_calls == 0 && budget_metrics.operation_intents == 8 &&
               !document.FindNode(graph, source_id)->properties.contains("budget-0"),
           "Mutation budget exhaustion must abort staging immediately before policy "
           "dispatch");

    CommandStack replacement_owner;
    CommandStack external_stack;
    GraphPolicy mutating_replacement;
    mutating_replacement.evaluate_batch = [&](const BatchPolicyContext&,
                                              std::span<const OperationIntent>) -> BatchPolicyDecision {
        return ReplaceBatch{[&] {
            auto external =
                external_stack.Execute(std::make_unique<SetNodePropertyCommand>(
                                           graph, source_id, "external-during-replacement", PropertyValue{true}),
                                       document, presentation, types);
            Expect(external.has_value(), "External replacement fixture mutation must execute");
            return std::make_unique<SetNodePropertyCommand>(graph, source_id, "must-not-replace-after-conflict",
                                                            PropertyValue{true});
        }};
    };
    auto replacement_conflict = replacement_owner.Execute(
        std::make_unique<SetNodePropertyCommand>(graph, source_id, "replacement-conflict-source", PropertyValue{true}),
        document, presentation, types, mutating_replacement);
    Expect(!replacement_conflict && replacement_conflict.error().code == ErrorCode::RevisionConflict &&
               document.FindNode(graph, source_id)->properties.contains("external-during-replacement") &&
               !document.FindNode(graph, source_id)->properties.contains("replacement-conflict-source") &&
               !document.FindNode(graph, source_id)->properties.contains("must-not-replace-after-conflict"),
           "Replacement factories must not silently adopt external owner revisions");

    CommandStack fast_path;
    fast_path.SetHistoryLimit(0);
    ResetTransactionMetrics();
    Execute(fast_path,
            std::make_unique<SetNodePropertyCommand>(graph, source_id, "empty-policy-fast", PropertyValue{true}),
            document, presentation, types, "Empty-policy fast path must execute");
    const auto empty_policy_metrics = GetTransactionMetrics();
    Expect(empty_policy_metrics.operation_intents == 0 && empty_policy_metrics.command_paths == 0 &&
               empty_policy_metrics.full_structure_validations == 0,
           "An empty policy must not build operation intents or command paths or "
           "run a full structure audit");
}

void TestRemovedGraphOwnershipClosure() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands;
    const GraphId parent = document.AllocateGraphId();
    const GraphId child = document.AllocateGraphId();
    const NodeId owner = document.AllocateNodeId();
    std::vector<std::unique_ptr<Command>> setup;
    setup.push_back(std::make_unique<AddGraphCommand>(Graph{
        .id = child,
        .display_name = "Owned child",
        .lifetime = GraphLifetime::Owned,
    }));
    setup.push_back(std::make_unique<AddGraphCommand>(Graph{
        .id = parent,
        .display_name = "Parent",
        .lifetime = GraphLifetime::Reusable,
    }));
    setup.push_back(std::make_unique<ModelOnlyNodeCommand>(parent, NodeInstance{
                                                                       .id = owner,
                                                                       .type = TypeId{"test.subgraph"},
                                                                       .subgraph =
                                                                           SubgraphReference{
                                                                               .ownership = SubgraphOwnership::Owned,
                                                                               .target = DocumentGraphTarget{child},
                                                                           },
                                                                       .role = NodeRole::Subgraph,
                                                                   }));
    Execute(commands, std::make_unique<CompoundCommand>("Owned graph fixture", std::move(setup)), document,
            presentation, types, "Owned graph fixture must execute");
    commands.Clear();

    const auto revision = document.ModelRevision();
    auto orphaned = commands.Execute(std::make_unique<DirectRemoveGraphsCommand>(std::vector<GraphId>{parent}),
                                     document, presentation, types);
    Expect(!orphaned && orphaned.error().code == ErrorCode::InvalidGraph && document.ModelRevision() == revision &&
               document.FindGraph(parent) != nullptr && document.FindGraph(child) != nullptr &&
               document.ValidateStructure().has_value(),
           "Removing an owner graph alone must not orphan its retained owned child");

    auto complete = commands.Execute(std::make_unique<DirectRemoveGraphsCommand>(std::vector<GraphId>{parent, child}),
                                     document, presentation, types);
    Expect(complete.has_value(), "Removing the complete owned graph closure must execute");
    Expect(document.FindGraph(parent) == nullptr && document.FindGraph(child) == nullptr &&
               document.ValidateStructure().has_value(),
           "Complete owned graph removal must retain a structurally valid document");
    Expect(commands.Undo(document, presentation, types).has_value() && document.FindGraph(parent) != nullptr &&
               document.FindGraph(child) != nullptr && document.ValidateStructure().has_value(),
           "Complete owned graph removal must restore atomically");
}

void TestPersistentAdjacencyRanges() {
    NodeMap rootless;
    NodeMap reserved;
    reserved.reserve(64);
    std::size_t empty_differences = 0;
    rootless.ForEachDifference(reserved,
                               [&](const NodeId, const NodeInstance*, const NodeInstance*) { ++empty_differences; });
    Expect(rootless.empty() && reserved.empty() && rootless == reserved && empty_differences == 0,
           "Rootless and reserved empty COW maps must compare without differences");
    NodeMap populated;
    populated.emplace(NodeId{1}, NodeInstance{.id = NodeId{1}, .type = TypeId{"persistent.root"}});
    std::size_t inserted_differences = 0;
    rootless.ForEachDifference(populated, [&](const NodeId id, const NodeInstance* before, const NodeInstance* after) {
        Expect(id == NodeId{1} && before == nullptr && after != nullptr,
               "Rootless COW difference must expose the inserted value");
        ++inserted_differences;
    });
    populated.clear();
    Expect(inserted_differences == 1 && populated.empty() && populated == rootless,
           "Cleared COW maps must return to the rootless empty state");

    GroupMemberSet aliased_members{NodeId{1}, NodeId{2}, NodeId{3}};
    aliased_members.Erase(aliased_members);
    Expect(aliased_members.empty(), "Aliased group-member erasure must retain "
                                    "iterator storage until completion");

    CowAdjacencyMap<LinkId> links;
    for (std::uint64_t value = 1; value <= 10'000; ++value) {
        Expect(links.insert_or_assign(LinkId{value}, LinkId{value}),
               "Persistent adjacency insertion must report a new key");
    }
    Expect(links.size() == 10'000 && links.contains(LinkId{1}) && links.contains(LinkId{10'000}),
           "Persistent adjacency must retain every inserted value");
    std::uint64_t previous = 0;
    for (const LinkId link : links) {
        Expect(link.Value() > previous, "Persistent adjacency iteration must remain key ordered");
        previous = link.Value();
    }

    CowAdjacencyMap<LinkId> snapshot = links;
    Expect(links.erase(LinkId{5'000}) == 1 && !links.contains(LinkId{5'000}) && snapshot.contains(LinkId{5'000}) &&
               snapshot.size() == 10'000,
           "Persistent adjacency mutations must preserve snapshot isolation");
    Expect(!links.insert_or_assign(LinkId{1}, LinkId{1}) && links.size() == 9'999,
           "Persistent adjacency no-op assignment must not change cardinality");
    Expect(links.insert_or_assign(LinkId{5'000}, LinkId{5'000}) && links == snapshot,
           "Persistent adjacency erase and restore must recover equivalent "
           "contents");

    std::vector<LinkId> randomized;
    randomized.reserve(2'000);
    for (std::uint64_t value = 1; value <= 2'000; ++value)
        randomized.push_back(LinkId{value});
    std::mt19937_64 random{0xC0FFEEULL};
    std::ranges::shuffle(randomized, random);
    CowAdjacencyMap<LinkId> rotated;
    for (const LinkId link : randomized)
        rotated.insert_or_assign(link, link);
    std::ranges::shuffle(randomized, random);
    for (std::size_t index = 0; index < 1'000; ++index) {
        Expect(rotated.erase(randomized[index]) == 1, "Persistent AVL randomized deletion must remove an existing key");
    }
    previous = 0;
    for (const LinkId link : rotated) {
        Expect(link.Value() > previous, "Persistent AVL rotations must retain ordered iteration");
        previous = link.Value();
    }
    Expect(rotated.size() == 1'000, "Persistent AVL randomized deletion must retain exact cardinality");

    CowAdjacencyMap<LinkId, ThrowingAdjacencyValue> throwing;
    throwing.insert_or_assign(LinkId{1}, ThrowingAdjacencyValue{1});
    throwing.insert_or_assign(LinkId{2}, ThrowingAdjacencyValue{2});
    ThrowingAdjacencyValue::throw_on_copy = true;
    bool copy_failed = false;
    try {
        throwing.insert_or_assign(LinkId{3}, ThrowingAdjacencyValue{3});
    } catch (const std::runtime_error&) {
        copy_failed = true;
    }
    ThrowingAdjacencyValue::throw_on_copy = false;
    Expect(copy_failed && throwing.size() == 2 && throwing.Find(LinkId{1})->value == 1 &&
               throwing.Find(LinkId{2})->value == 2 && throwing.Find(LinkId{3}) == nullptr,
           "Persistent AVL insertion failure must preserve the previously "
           "published root");
}

void TestIdExhaustion() {
    IdGenerator exhausted{std::numeric_limits<std::uint64_t>::max()};
    Expect(exhausted.Next<NodeId>() == NodeId{std::numeric_limits<std::uint64_t>::max()},
           "ID generator must emit its final representable ID");
    Expect(!exhausted.Next<NodeId>(), "ID generator must not wrap after exhaustion");
}

void TestRegistryRevisionOverflowGuard() {
    const auto final = Detail::NextRegistryRevision(std::numeric_limits<std::uint64_t>::max() - 1, "Test registry");
    const auto exhausted = Detail::NextRegistryRevision(std::numeric_limits<std::uint64_t>::max(), "Test registry");
    Expect(final && *final == std::numeric_limits<std::uint64_t>::max() && !exhausted &&
               exhausted.error().code == ErrorCode::GenerationOverflow,
           "Registry revisions must publish the final generation and reject "
           "wraparound");

    RegistryCatalog nodes;
    const TypeId converter{"registry.overflow-converter"};
    Expect(nodes
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = converter,
                   .display_name = "Overflow converter",
                   .static_pins =
                       {
                           PinDescriptor{.key = "input-a", .type = TypeId{"overflow.a"}},
                           PinDescriptor{
                               .key = "output-a",
                               .type = TypeId{"overflow.b"},
                               .direction = PinDirection::Output,
                           },
                           PinDescriptor{.key = "input-b", .type = TypeId{"overflow.c"}},
                           PinDescriptor{
                               .key = "output-b",
                               .type = TypeId{"overflow.d"},
                               .direction = PinDirection::Output,
                           },
                       },
               })
               .has_value(),
           "Overflow converter descriptor must register");
    const ConversionDescriptor conversion_a{
        .key = ConversionKey{TypeId{"overflow.a"}, TypeId{"overflow.b"}, PinKind::Data},
        .node_type = converter,
        .input_pin = "input-a",
        .output_pin = "output-a",
    };
    const ConversionDescriptor conversion_b{
        .key = ConversionKey{TypeId{"overflow.c"}, TypeId{"overflow.d"}, PinKind::Data},
        .node_type = converter,
        .input_pin = "input-b",
        .output_pin = "output-b",
    };

    Detail::RegistryAccess::SetRevisionsForTesting(nodes, std::numeric_limits<std::uint64_t>::max(), 0, 10, 1, false);
    auto node_overflow = nodes.RegisterNodeType(NodeTypeDescriptor{
        .type = TypeId{"registry.overflow-rejected"},
        .display_name = "Rejected overflow",
    });
    Expect(!node_overflow && node_overflow.error().code == ErrorCode::GenerationOverflow && nodes.Generation() == 10 &&
               !nodes.Find(TypeId{"registry.overflow-rejected"}),
           "Node revision overflow must leave the live generation unchanged");

    Detail::RegistryAccess::SetRevisionsForTesting(nodes, 1, 0, std::numeric_limits<std::uint64_t>::max(), 1, false);
    auto generation_overflow = nodes.RegisterConversion(conversion_a);
    Expect(!generation_overflow && generation_overflow.error().code == ErrorCode::GenerationOverflow &&
               nodes.ConversionRevision() == 0,
           "Common registry generation overflow must roll back conversion "
           "registration");

    Detail::RegistryAccess::SetRevisionsForTesting(nodes, 1, std::numeric_limits<std::uint64_t>::max(), 15, 1, false);
    auto type_overflow = nodes.RegisterConversion(conversion_a);
    Expect(!type_overflow && type_overflow.error().code == ErrorCode::GenerationOverflow && nodes.Generation() == 15 &&
               nodes.Check(TypeId{"overflow.a"}, TypeId{"overflow.b"}, PinKind::Data).status ==
                   ConnectionResult::Status::Rejected,
           "Type revision overflow must leave the live registry generation "
           "unchanged");

    Detail::RegistryAccess::SetRevisionsForTesting(nodes, 1, 0, 20, std::numeric_limits<std::uint64_t>::max(), false);
    auto final_registration = nodes.RegisterConversion(conversion_a);
    const std::uint64_t final_generation = nodes.Generation();
    auto exhausted_registration = nodes.RegisterConversion(conversion_b);
    Expect(final_registration && !exhausted_registration &&
               exhausted_registration.error().code == ErrorCode::GenerationOverflow &&
               nodes.Generation() == final_generation &&
               nodes.Check(TypeId{"overflow.c"}, TypeId{"overflow.d"}, PinKind::Data).status ==
                   ConnectionResult::Status::Rejected,
           "Conversion registration IDs must use the final ID once and never wrap");
}

void TestRegistrySnapshotsAndInvocationSafety() {
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    const GraphId graph = document.RootGraph();
    RegistryCatalog registry;
    std::size_t validation_calls = 0;
    std::size_t blocked_mutations = 0;
    bool document_mutated = false;
    bool document_mutation_succeeded = false;
    NodeTypeDescriptor descriptor{
        .type = TypeId{"registry.guarded"},
        .display_name = "Guarded",
    };
    descriptor.behavior = std::make_shared<const NodeBehavior>(
        NodeBehavior{.validate = [&](const NodeInstance&, std::span<const PinInstance>) {
            ++validation_calls;
            if (!document_mutated) {
                document_mutated = true;
                const NodeId added = document.AllocateNodeId();
                auto changed = commands.Execute(
                    std::make_unique<AddNodeCommand>(
                        graph, NodeCreation{.node = NodeInstance{.id = added, .type = TypeId{"registry.guarded"}}}),
                    document, presentation, registry);
                document_mutation_succeeded = changed.has_value();
            }
            auto removed = registry.UnregisterNodeType(TypeId{"registry.guarded"});
            if (!removed && removed.error().code == ErrorCode::CommandFailed) ++blocked_mutations;
            auto replaced = registry.ReplaceNodeType(NodeTypeDescriptor{
                .type = TypeId{"registry.guarded"},
                .display_name = "Guarded replacement",
            });
            if (!replaced && replaced.error().code == ErrorCode::CommandFailed) ++blocked_mutations;
            return std::vector<std::string>{};
        }});
    Expect(registry.RegisterNodeType(std::move(descriptor)).has_value(), "Guarded descriptor must register");

    const NodeTypeDescriptorPtr retained = registry.Find(TypeId{"registry.guarded"});
    const RegistrySnapshot snapshot = registry.Snapshot();
    for (std::size_t index = 0; index < 2; ++index) {
        const NodeId node = document.AllocateNodeId();
        Execute(commands,
                std::make_unique<AddNodeCommand>(
                    graph, NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"registry.guarded"}}}),
                document, presentation, registry, "Guarded validation node must execute");
    }
    Expect(ValidateGraph(document, graph, registry).empty() && validation_calls == 2 && blocked_mutations == 4 &&
               document_mutation_succeeded && document.FindGraph(graph)->nodes.size() == 3 &&
               registry.Find(TypeId{"registry.guarded"}),
           "Validation must pin document and registry generations across "
           "mutating callbacks");

    auto removed = registry.UnregisterNodeType(TypeId{"registry.guarded"});
    Expect(removed && *removed && !registry.Find(TypeId{"registry.guarded"}) && retained &&
               retained->display_name == "Guarded" && snapshot.Find(TypeId{"registry.guarded"}) == retained,
           "Owning descriptor handles and snapshots must survive unregister");
}

void TestConversionSnapshotsAndInvocationSafety() {
    RegistryCatalog nodes;
    std::weak_ptr<int> callback_lifetime;
    {
        auto callback_owner = std::make_shared<int>(17);
        callback_lifetime = callback_owner;
        NodeTypeDescriptor descriptor{
            .type = TypeId{"registry.convert.data"},
            .display_name = "Data conversion",
            .version = 3,
            .static_pins =
                {
                    PinDescriptor{.key = "input", .type = TypeId{"int"}},
                    PinDescriptor{
                        .key = "output",
                        .type = TypeId{"float"},
                        .direction = PinDirection::Output,
                    },
                },
            .default_properties = {{"scale", PropertyValue{2.0}}},
        };
        descriptor.behavior = std::make_shared<const NodeBehavior>(NodeBehavior{
            .validate = [callback_owner](const NodeInstance&,
                                         std::span<const PinInstance>) { return std::vector<std::string>{}; },
        });
        Expect(nodes.RegisterNodeType(std::move(descriptor)).has_value(), "Data conversion descriptor must register");
    }
    Expect(nodes
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"registry.convert.execution"},
                   .display_name = "Execution conversion",
                   .static_pins =
                       {
                           PinDescriptor{
                               .key = "input",
                               .type = TypeId{"int"},
                               .kind = PinKind::Execution,
                           },
                           PinDescriptor{
                               .key = "output",
                               .type = TypeId{"float"},
                               .direction = PinDirection::Output,
                               .kind = PinKind::Execution,
                           },
                       },
               })
               .has_value(),
           "Execution conversion descriptor must register");

    const ConversionDescriptor data_conversion{
        .key =
            ConversionKey{
                .source_type = TypeId{"int"},
                .destination_type = TypeId{"float"},
                .kind = PinKind::Data,
            },
        .node_type = TypeId{"registry.convert.data"},
        .input_pin = "input",
        .output_pin = "output",
    };
    const ConversionDescriptor execution_conversion{
        .key =
            ConversionKey{
                .source_type = TypeId{"int"},
                .destination_type = TypeId{"float"},
                .kind = PinKind::Execution,
            },
        .node_type = TypeId{"registry.convert.execution"},
        .input_pin = "input",
        .output_pin = "output",
    };

    const RegistrySnapshot empty = nodes.Snapshot();
    Expect(empty.ConversionRevision() == 0 && nodes.ConversionRevision() == 0,
           "Fresh type registry snapshots must start at revision zero");
    auto invalid = data_conversion;
    invalid.input_pin = "missing";
    Expect(!nodes.RegisterConversion(invalid) && nodes.ConversionRevision() == 0,
           "Failed type registration must not advance the revision");
    Expect(nodes.RegisterConversion(data_conversion).has_value() && nodes.ConversionRevision() == 1,
           "Successful type registration must publish one revision");
    const RegistrySnapshot data_generation = nodes.Snapshot();
    Expect(nodes.RegisterConversion(execution_conversion).has_value() && nodes.ConversionRevision() == 2,
           "Conversion keys must include pin kind");
    const RegistrySnapshot both_generations = nodes.Snapshot();
    Expect(!nodes.RegisterConversion(data_conversion) && nodes.ConversionRevision() == 2,
           "Duplicate type registration must not advance the revision");
    Expect(empty.Check(TypeId{"int"}, TypeId{"float"}, PinKind::Data).status == ConnectionResult::Status::Rejected &&
               data_generation.Check(TypeId{"int"}, TypeId{"float"}, PinKind::Data).status ==
                   ConnectionResult::Status::RequiresConversion &&
               data_generation.Check(TypeId{"int"}, TypeId{"float"}, PinKind::Execution).status ==
                   ConnectionResult::Status::Rejected &&
               both_generations.Check(TypeId{"int"}, TypeId{"float"}, PinKind::Execution).status ==
                   ConnectionResult::Status::RequiresConversion,
           "Type snapshots must retain immutable indexed generations");

    RegistrySnapshot detached;
    {
        RegistryCatalog owner;
        Expect(owner.RegisterNodeType(*nodes.Find(TypeId{"registry.convert.data"})).has_value(),
               "Detached registry node descriptor must register");
        Expect(owner.RegisterConversion(data_conversion).has_value(), "Detached snapshot conversion must register");
        detached = owner.Snapshot();
    }
    NodeTypeDescriptor callback_free_descriptor = *nodes.Find(TypeId{"registry.convert.data"});
    callback_free_descriptor.behavior.reset();
    Expect(nodes.ReplaceNodeType(std::move(callback_free_descriptor)).has_value() && !callback_lifetime.expired(),
           "Combined snapshots must retain their immutable descriptor behavior "
           "generation");
    GraphDocument recipe_document;
    const auto detached_recipe = detached.Check(TypeId{"int"}, TypeId{"float"}, PinKind::Data);
    Expect(detached_recipe.recipe.has_value(), "Detached snapshot must retain its exact conversion recipe");
    auto instantiated = detached_recipe.recipe->Instantiate(recipe_document);
    Expect(instantiated && instantiated->node.type == data_conversion.node_type &&
               instantiated->node.type_version == 3 && instantiated->node.properties.contains("scale") &&
               instantiated->pins.size() == 2,
           "Conversion snapshots must instantiate immutable recipes after owner "
           "destruction");

    const auto register_converter = [&](const char* node_type, const char* source, const char* destination) {
        Expect(nodes
                   .RegisterNodeType(NodeTypeDescriptor{
                       .type = TypeId{node_type},
                       .display_name = node_type,
                       .static_pins =
                           {
                               PinDescriptor{.key = "input", .type = TypeId{source}},
                               PinDescriptor{
                                   .key = "output",
                                   .type = TypeId{destination},
                                   .direction = PinDirection::Output,
                               },
                           },
                   })
                   .has_value(),
               "Invocation test conversion descriptor must register");
        return ConversionDescriptor{
            .key =
                ConversionKey{
                    .source_type = TypeId{source},
                    .destination_type = TypeId{destination},
                    .kind = PinKind::Data,
                },
            .node_type = TypeId{node_type},
            .input_pin = "input",
            .output_pin = "output",
        };
    };
    const ConversionDescriptor blocked_conversion = register_converter("registry.convert.blocked", "audio", "image");
    const ConversionDescriptor deferred_conversion = register_converter("registry.convert.deferred", "text", "bool");
    const ConversionDescriptor preview_conversion = register_converter("registry.convert.preview", "bytes", "json");
    const ConversionDescriptor reauth_pending_conversion =
        register_converter("registry.convert.reauth-pending", "matrix", "tensor");
    const ConversionDescriptor reauth_blocked_conversion =
        register_converter("registry.convert.reauth-blocked", "packet", "frame");

    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;
    std::vector<ErrorCode> mutation_errors;
    std::vector<std::uint64_t> applied_revisions;
    auto executed = commands.Execute(
        std::make_unique<RegistryMutationCommand>(nodes, blocked_conversion, mutation_errors, applied_revisions),
        document, presentation, nodes);
    Expect(executed && commands.Undo(document, presentation, nodes).has_value() &&
               commands.Redo(document, presentation, nodes).has_value() &&
               mutation_errors == std::vector<ErrorCode>{ErrorCode::CommandFailed, ErrorCode::CommandFailed} &&
               applied_revisions == std::vector<std::uint64_t>{2, 2} && nodes.ConversionRevision() == 2,
           "Execute and redo must pin one type generation and block registry "
           "mutation");

    GraphPolicy defer;
    defer.evaluate_batch = [](const BatchPolicyContext&, std::span<const OperationIntent>) -> BatchPolicyDecision {
        return DeferBatch{std::uint64_t{1}};
    };
    auto pending = commands.Execute(std::make_unique<SetSchemaVersionCommand>(3), document, presentation, nodes, defer);
    Expect(pending && pending->deferred && nodes.RegisterConversion(deferred_conversion).has_value() &&
               nodes.ConversionRevision() == 3 && commands.Cancel(pending->deferred->id).has_value(),
           "Deferred transactions must own a plain snapshot without blocking "
           "live registry mutation");

    GraphDocument reauth_document;
    GraphPresentation reauth_presentation;
    CommandStack reauth_commands;
    auto reauth_registry = std::make_unique<RegistryCatalog>();
    Expect(reauth_registry
                   ->RegisterNodeType(NodeTypeDescriptor{
                       .type = TypeId{"registry.convert.reauth-pending"},
                       .display_name = "Reauthorization pending conversion",
                       .static_pins =
                           {
                               PinDescriptor{.key = "input", .type = TypeId{"matrix"}},
                               PinDescriptor{
                                   .key = "output",
                                   .type = TypeId{"tensor"},
                                   .direction = PinDirection::Output,
                               },
                           },
                   })
                   .has_value() &&
               reauth_registry
                   ->RegisterNodeType(NodeTypeDescriptor{
                       .type = TypeId{"registry.convert.reauth-blocked"},
                       .display_name = "Reauthorization blocked conversion",
                       .static_pins =
                           {
                               PinDescriptor{.key = "input", .type = TypeId{"packet"}},
                               PinDescriptor{
                                   .key = "output",
                                   .type = TypeId{"frame"},
                                   .direction = PinDirection::Output,
                               },
                           },
                   })
                   .has_value(),
           "Reauthorization conversion descriptors must register");
    auto reauth_pending = reauth_commands.Execute(std::make_unique<SetSchemaVersionCommand>(2), reauth_document,
                                                  reauth_presentation, *reauth_registry, defer);
    Expect(reauth_pending && reauth_pending->deferred, "Reauthorization fixture must prepare a deferred transaction");
    auto moved_reauth_registry = std::make_unique<RegistryCatalog>(std::move(*reauth_registry));
    reauth_registry.reset();
    std::optional<ErrorCode> reauth_mutation_error;
    GraphPolicy reauthorize;
    reauthorize.evaluate_batch = [&](const BatchPolicyContext&,
                                     std::span<const OperationIntent>) -> BatchPolicyDecision {
        auto registered = moved_reauth_registry->RegisterConversion(reauth_blocked_conversion);
        if (!registered) reauth_mutation_error = registered.error().code;
        moved_reauth_registry.reset();
        return AllowBatch{};
    };
    auto resumed = reauth_commands.Resume(reauth_pending->deferred->id, reauth_document, reauth_presentation,
                                          ResumeMode::Reauthorize, reauthorize);
    Expect(resumed && reauth_document.SchemaVersion() == 2 && !moved_reauth_registry &&
               reauth_mutation_error == ErrorCode::CommandFailed,
           "Resume reauthorization must scope a lifetime-safe type registry "
           "invocation lease");

    Expect(nodes.RegisterNodeType(SourceDescriptor()).has_value() &&
               nodes.RegisterNodeType(SinkDescriptor()).has_value(),
           "Connection preview descriptors must register");
    GraphDocument preview_document;
    GraphPresentation preview_presentation;
    CommandStack preview_commands;
    auto source = nodes.Instantiate(preview_document, TypeId{"test.source"});
    auto sink = nodes.Instantiate(preview_document, TypeId{"test.sink"});
    Expect(source && sink, "Connection preview nodes must instantiate");
    const PinId output = source->pins.front().id;
    const PinId input = sink->pins.front().id;
    Execute(preview_commands, std::make_unique<AddNodeCommand>(preview_document.RootGraph(), std::move(*source)),
            preview_document, preview_presentation, nodes, "Connection preview source must be added");
    Execute(preview_commands, std::make_unique<AddNodeCommand>(preview_document.RootGraph(), std::move(*sink)),
            preview_document, preview_presentation, nodes, "Connection preview sink must be added");
    std::optional<ErrorCode> preview_mutation_error;
    GraphPolicy preview_policy;
    preview_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                            const OperationIntent&) -> OperationPolicyDecision {
        auto registered = nodes.RegisterConversion(preview_conversion);
        if (!registered) preview_mutation_error = registered.error().code;
        return AllowOperation{};
    };
    const std::uint64_t preview_revision = nodes.ConversionRevision();
    auto prepared = PrepareConnectionCommand(preview_document, preview_presentation, nodes,
                                             ConnectionRequest{preview_document.RootGraph(), output, input}, Vec2{},
                                             preview_policy);
    Expect(prepared && preview_mutation_error == ErrorCode::CommandFailed &&
               nodes.ConversionRevision() == preview_revision,
           "Connection preparation must hold one scoped type invocation generation");

    auto validation_registry = std::make_unique<RegistryCatalog>();
    const ConversionDescriptor validation_conversion = [&] {
        Expect(validation_registry
                   ->RegisterNodeType(NodeTypeDescriptor{
                       .type = TypeId{"registry.validation.convert"},
                       .display_name = "Validation conversion",
                       .static_pins =
                           {
                               PinDescriptor{.key = "input", .type = TypeId{"left"}},
                               PinDescriptor{
                                   .key = "output",
                                   .type = TypeId{"right"},
                                   .direction = PinDirection::Output,
                               },
                           },
                   })
                   .has_value(),
               "Validation conversion descriptor must register");
        return ConversionDescriptor{
            .key =
                ConversionKey{
                    .source_type = TypeId{"left"},
                    .destination_type = TypeId{"right"},
                    .kind = PinKind::Data,
                },
            .node_type = TypeId{"registry.validation.convert"},
            .input_pin = "input",
            .output_pin = "output",
        };
    }();
    std::optional<ErrorCode> validation_mutation_error;
    NodeTypeDescriptor guarded{
        .type = TypeId{"registry.validation.guarded"},
        .display_name = "Validation guarded",
    };
    guarded.behavior = std::make_shared<const NodeBehavior>(
        NodeBehavior{.validate = [&](const NodeInstance&, std::span<const PinInstance>) {
            auto moved = std::make_unique<RegistryCatalog>(std::move(*validation_registry));
            validation_registry.reset();
            auto registered = moved->RegisterConversion(validation_conversion);
            if (!registered) validation_mutation_error = registered.error().code;
            moved.reset();
            return std::vector<std::string>{};
        }});
    Expect(validation_registry->RegisterNodeType(std::move(guarded)).has_value(),
           "Guarded validation descriptor must register");
    GraphDocument validation_document;
    GraphPresentation validation_presentation;
    CommandStack validation_commands;
    const NodeId guarded_node = validation_document.AllocateNodeId();
    Execute(validation_commands,
            std::make_unique<AddNodeCommand>(validation_document.RootGraph(),
                                             NodeCreation{.node =
                                                              NodeInstance{
                                                                  .id = guarded_node,
                                                                  .type = TypeId{"registry.validation.guarded"},
                                                              }}),
            validation_document, validation_presentation, *validation_registry,
            "Guarded validation node must be added");
    const auto issues = ValidateGraph(validation_document, validation_document.RootGraph(), *validation_registry);
    Expect(issues.empty() && !validation_registry && validation_mutation_error == ErrorCode::CommandFailed,
           "Graph validation leases must survive type registry owner move and "
           "destruction");
}

void TestConversionIdentityAndLifecycle() {
    constexpr std::string_view ConverterType = "registry.shared-converter";
    const TypeId source_type{"identity.source"};
    const TypeId destination_type{"identity.destination"};
    RegistryCatalog nodes;
    const auto converter_descriptor = [&](const std::uint32_t version) {
        return NodeTypeDescriptor{
            .type = TypeId{ConverterType},
            .display_name = "Shared converter",
            .version = version,
            .static_pins =
                {
                    PinDescriptor{.key = "data-input", .type = source_type},
                    PinDescriptor{
                        .key = "data-output",
                        .type = destination_type,
                        .direction = PinDirection::Output,
                        .cardinality = PinCardinality::Multiple,
                    },
                    PinDescriptor{
                        .key = "execution-input",
                        .type = source_type,
                        .kind = PinKind::Execution,
                    },
                    PinDescriptor{
                        .key = "execution-output",
                        .type = destination_type,
                        .direction = PinDirection::Output,
                        .kind = PinKind::Execution,
                        .cardinality = PinCardinality::Multiple,
                    },
                },
            .default_properties = {{"recipe-version", PropertyValue{static_cast<std::int64_t>(version)}}},
        };
    };
    const ConversionDescriptor data_descriptor{
        .key = ConversionKey{source_type, destination_type, PinKind::Data},
        .node_type = TypeId{ConverterType},
        .input_pin = "data-input",
        .output_pin = "data-output",
    };
    const ConversionDescriptor execution_descriptor{
        .key = ConversionKey{source_type, destination_type, PinKind::Execution},
        .node_type = TypeId{ConverterType},
        .input_pin = "execution-input",
        .output_pin = "execution-output",
    };

    RegistryCatalog& types = nodes;
    Expect(nodes.RegisterNodeType(converter_descriptor(2)).has_value(), "Data converter generation must register");
    auto data_registration = types.RegisterConversion(data_descriptor);
    Expect(data_registration.has_value() && *data_registration,
           "Data conversion must return an owning registration token");
    const RegistrySnapshot data_snapshot = types.Snapshot();
    const auto data_check = data_snapshot.Check(source_type, destination_type, PinKind::Data);
    Expect(data_check.recipe && data_check.recipe->Descriptor() == data_descriptor &&
               data_check.recipe->Registration() == *data_registration,
           "Data lookup must return its exact immutable recipe");

    auto execution_registration = types.RegisterConversion(execution_descriptor);
    Expect(execution_registration.has_value() && *execution_registration &&
               *execution_registration != *data_registration,
           "Execution conversion must have a distinct registration identity");
    const auto execution_check = types.Check(source_type, destination_type, PinKind::Execution);
    Expect(execution_check.recipe && execution_check.recipe->Descriptor() == execution_descriptor &&
               *execution_check.recipe != *data_check.recipe,
           "Pin kind must select the exact recipe without descriptor fallback "
           "lookup");
    GraphDocument recipe_document;
    auto execution_creation = execution_check.recipe->Instantiate(recipe_document);
    Expect(execution_creation && execution_creation->node.type_version == 2 &&
               std::ranges::count_if(execution_creation->pins,
                                     [](const PinInstance& pin) { return pin.kind == PinKind::Execution; }) == 2,
           "Execution lookup must instantiate only the captured Execution pin "
           "schema");

    Expect(nodes.RegisterNodeType(NodeTypeDescriptor{
                                      .type = TypeId{"identity.execution-source"},
                                      .display_name = "Execution source",
                                      .static_pins = {PinDescriptor{
                                          .key = "value",
                                          .type = source_type,
                                          .direction = PinDirection::Output,
                                          .kind = PinKind::Execution,
                                          .cardinality = PinCardinality::Multiple,
                                      }},
                                  })
                   .has_value() &&
               nodes
                   .RegisterNodeType(NodeTypeDescriptor{
                       .type = TypeId{"identity.execution-sink"},
                       .display_name = "Execution sink",
                       .static_pins = {PinDescriptor{
                           .key = "value",
                           .type = destination_type,
                           .kind = PinKind::Execution,
                       }},
                   })
                   .has_value(),
           "Execution conversion endpoints must register");

    const auto add_execution_endpoints = [&](GraphDocument& document, GraphPresentation& presentation,
                                             CommandStack& commands) {
        auto source = nodes.Instantiate(document, TypeId{"identity.execution-source"});
        auto sink = nodes.Instantiate(document, TypeId{"identity.execution-sink"});
        Expect(source && sink, "Execution conversion endpoints must instantiate");
        const PinId output = source->pins.front().id;
        const PinId input = sink->pins.front().id;
        Execute(commands, std::make_unique<AddNodeCommand>(document.RootGraph(), std::move(*source)), document,
                presentation, types, "Execution source must be added");
        Execute(commands, std::make_unique<AddNodeCommand>(document.RootGraph(), std::move(*sink)), document,
                presentation, types, "Execution sink must be added");
        commands.Clear();
        return std::pair{output, input};
    };

    GraphDocument insertion_document;
    GraphPresentation insertion_presentation;
    CommandStack insertion_commands;
    const auto [insertion_output, insertion_input] =
        add_execution_endpoints(insertion_document, insertion_presentation, insertion_commands);
    auto tampered_creation = execution_check.recipe->Instantiate(insertion_document);
    Expect(tampered_creation.has_value(), "Tampered conversion fixture must instantiate");
    const PinId tampered_input = tampered_creation->pins.front().id;
    const PinId tampered_output = tampered_creation->pins.back().id;
    tampered_creation->pins.front().storage = PinStorage::Dynamic;
    auto tampered = insertion_commands.Execute(
        std::make_unique<InsertConversionCommand>(
            insertion_document.RootGraph(), *execution_check.recipe, std::move(*tampered_creation), NodePresentation{},
            Link{insertion_document.AllocateLinkId(), insertion_output, tampered_input},
            Link{insertion_document.AllocateLinkId(), tampered_output, insertion_input}),
        insertion_document, insertion_presentation, types);
    Expect(!tampered && tampered.error().code == ErrorCode::InvalidGraph &&
               insertion_document.FindGraph(insertion_document.RootGraph())->nodes.size() == 2 &&
               insertion_document.FindGraph(insertion_document.RootGraph())->links.empty(),
           "Public conversion commands must reject NodeCreation values that "
           "diverge from the exact recipe");
    auto insertion = PrepareConnectionCommand(
        insertion_document, insertion_presentation, types,
        ConnectionRequest{insertion_document.RootGraph(), insertion_output, insertion_input}, Vec2{100.0f, 20.0f});
    Expect(insertion.has_value(), "Execution conversion command must prepare");
    Execute(insertion_commands, std::move(*insertion), insertion_document, insertion_presentation, types,
            "Execution conversion command must execute");
    const auto* insertion_graph = insertion_document.FindGraph(insertion_document.RootGraph());
    Expect(insertion_graph->links.size() == 2 &&
               std::ranges::all_of(insertion_graph->links,
                                   [&](const auto& entry) {
                                       const auto* output = insertion_document.FindPin(insertion_document.RootGraph(),
                                                                                       entry.second.output);
                                       const auto* input = insertion_document.FindPin(insertion_document.RootGraph(),
                                                                                      entry.second.input);
                                       return output && input && output->kind == PinKind::Execution &&
                                              input->kind == PinKind::Execution;
                                   }),
           "Automatic insertion must materialize a valid two-leg Execution chain");

    const RegistrySnapshot execution_v2_snapshot = types.Snapshot();
    const ConversionRecipe execution_v2_recipe =
        *execution_v2_snapshot.Check(source_type, destination_type, PinKind::Execution).recipe;
    Expect(nodes.ReplaceNodeType(converter_descriptor(3)).has_value() && types.ConversionRevision() == 3,
           "Node replacement must atomically refresh every dependent conversion "
           "recipe");
    const auto execution_v3_check = types.Check(source_type, destination_type, PinKind::Execution);
    Expect(execution_v3_check.recipe && *execution_v3_check.recipe != execution_v2_recipe &&
               execution_v3_check.recipe->Registration() == *execution_registration,
           "Replacement must preserve registration identity and replace exact "
           "recipe identity");
    GraphDocument replacement_document;
    auto old_creation = execution_v2_recipe.Instantiate(replacement_document);
    auto new_creation = execution_v3_check.recipe->Instantiate(replacement_document);
    Expect(old_creation && new_creation && old_creation->node.type_version == 2 && new_creation->node.type_version == 3,
           "Old snapshots must retain v2 while live replacement materializes v3");

    Expect(types.HasConversionsForNodeType(TypeId{ConverterType}) &&
               types.RegistrationsForNodeType(TypeId{ConverterType}).size() == 2,
           "Reverse conversion index must expose all active registrations for a "
           "node type");
    auto data_unregistered = types.UnregisterConversion(*data_registration);
    Expect(data_unregistered && *data_unregistered && types.ConversionRevision() == 4 &&
               types.Check(source_type, destination_type, PinKind::Data).status == ConnectionResult::Status::Rejected &&
               data_snapshot.Check(source_type, destination_type, PinKind::Data).recipe == data_check.recipe,
           "Unregister must remove only the live recipe while old snapshots "
           "remain functional");
    Expect(types.UnregisterConversion(*data_registration).value_or(true) == false,
           "A stale conversion token must be an idempotent no-op");
    RegistryCatalog unrelated_registry;
    const auto foreign_unregister = unrelated_registry.UnregisterConversion(*execution_registration);
    Expect(!foreign_unregister && foreign_unregister.error().code == ErrorCode::RegistryMismatch,
           "A token from another catalog must fail with RegistryMismatch");

    GraphDocument stale_document;
    GraphPresentation stale_presentation;
    CommandStack stale_commands;
    const auto [stale_output, stale_input] =
        add_execution_endpoints(stale_document, stale_presentation, stale_commands);
    auto stale_plan =
        PrepareConnectionCommand(stale_document, stale_presentation, types,
                                 ConnectionRequest{stale_document.RootGraph(), stale_output, stale_input}, Vec2{});
    Expect(stale_plan.has_value(), "Replacement staleness fixture must prepare");
    Expect(nodes.ReplaceNodeType(converter_descriptor(4)).has_value(),
           "Fourth converter generation must replace the live recipe");
    auto stale_result = stale_commands.Execute(std::move(*stale_plan), stale_document, stale_presentation, types);
    Expect(!stale_result && stale_result.error().code == ErrorCode::RevisionConflict &&
               stale_document.FindGraph(stale_document.RootGraph())->links.empty(),
           "A prepared command must fail closed when its exact recipe was replaced");

    GraphDocument deferred_document;
    GraphPresentation deferred_presentation;
    CommandStack deferred_commands;
    const auto [deferred_output, deferred_input] =
        add_execution_endpoints(deferred_document, deferred_presentation, deferred_commands);
    auto deferred_plan = PrepareConnectionCommand(
        deferred_document, deferred_presentation, types,
        ConnectionRequest{deferred_document.RootGraph(), deferred_output, deferred_input}, Vec2{});
    GraphPolicy defer;
    defer.evaluate_batch = [](const BatchPolicyContext&, std::span<const OperationIntent>) -> BatchPolicyDecision {
        return DeferBatch{std::uint64_t{1}};
    };
    auto pending =
        deferred_commands.Execute(std::move(*deferred_plan), deferred_document, deferred_presentation, types, defer);
    Expect(pending && pending->deferred && deferred_document.FindGraph(deferred_document.RootGraph())->links.empty(),
           "Deferred conversion must retain an unpublished prepared transaction");
    Expect(nodes.ReplaceNodeType(converter_descriptor(5)).has_value(),
           "Live registry must advance while a conversion transaction is deferred");
    auto resumed = deferred_commands.Resume(pending->deferred->id, deferred_document, deferred_presentation,
                                            ResumeMode::CommitPrepared);
    Expect(!resumed && resumed.error().code == ErrorCode::RevisionConflict &&
               deferred_commands.Cancel(pending->deferred->id).has_value() &&
               deferred_document.FindGraph(deferred_document.RootGraph())->nodes.size() == 2,
           "Deferred commit must reject a prepared transaction after registry "
           "reload");
    GraphDocument live_v5_document;
    auto live_v5 = types.Check(source_type, destination_type, PinKind::Execution).recipe->Instantiate(live_v5_document);
    Expect(live_v5 && live_v5->node.type_version == 5,
           "Commands prepared after replacement must observe the live v5 recipe");

    NodeEditorWorkspace workspace;
    Expect(workspace
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = TypeId{"workspace.lifecycle-converter"},
                   .display_name = "Workspace lifecycle converter",
                   .static_pins =
                       {
                           PinDescriptor{.key = "input", .type = TypeId{"workspace.a"}},
                           PinDescriptor{
                               .key = "output",
                               .type = TypeId{"workspace.b"},
                               .direction = PinDirection::Output,
                           },
                       },
               })
               .has_value(),
           "Workspace lifecycle converter must register");
    auto workspace_registration = workspace.RegisterConversion(ConversionDescriptor{
        .key = ConversionKey{TypeId{"workspace.a"}, TypeId{"workspace.b"}, PinKind::Data},
        .node_type = TypeId{"workspace.lifecycle-converter"},
        .input_pin = "input",
        .output_pin = "output",
    });
    Expect(workspace_registration && workspace
                                         .ReplaceNodeType(NodeTypeDescriptor{
                                             .type = TypeId{"workspace.lifecycle-converter"},
                                             .display_name = "Workspace lifecycle converter v2",
                                             .version = 2,
                                             .static_pins =
                                                 {
                                                     PinDescriptor{.key = "input", .type = TypeId{"workspace.a"}},
                                                     PinDescriptor{
                                                         .key = "output",
                                                         .type = TypeId{"workspace.b"},
                                                         .direction = PinDirection::Output,
                                                     },
                                                 },
                                         })
                                         .has_value(),
           "Workspace must atomically reload a referenced node type and refresh "
           "its conversion recipe");
    GraphDocument workspace_recipe_document;
    const auto workspace_recipe =
        workspace.Registry().Check(TypeId{"workspace.a"}, TypeId{"workspace.b"}, PinKind::Data);
    auto workspace_creation = workspace_recipe.recipe->Instantiate(workspace_recipe_document);
    Expect(workspace_creation && workspace_creation->node.type_version == 2,
           "Workspace conversion replacement must capture the reloaded node "
           "generation");
    auto in_use = workspace.UnregisterNodeType(TypeId{"workspace.lifecycle-converter"});
    Expect(workspace_registration && !in_use && in_use.error().code == ErrorCode::TypeInUse &&
               workspace.UnregisterConversion(*workspace_registration).value_or(false) &&
               workspace.UnregisterNodeType(TypeId{"workspace.lifecycle-converter"}).value_or(false),
           "Workspace must reject plugin unload until all referencing conversion "
           "tokens are removed");
}

void TestRegistryCommandBoundaryAndAtomicUpdates() {
    NodeEditorWorkspace workspace;
    const TypeId guarded_type{"registry.command-guard"};
    const auto guarded_descriptor = [&](const std::uint32_t version) {
        return NodeTypeDescriptor{
            .type = guarded_type,
            .display_name = "Command guard",
            .version = version,
        };
    };
    Expect(workspace.RegisterNodeType(guarded_descriptor(1)).has_value(), "Command guard descriptor must register");

    std::vector<ErrorCode> callback_errors;
    const auto record = [&](auto result) {
        if (!result) callback_errors.push_back(result.error().code);
    };
    auto command = std::make_unique<RegistryCallbackCommand>(
        2,
        [&] {
            record(workspace.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"registry.command-register"},
                .display_name = "Blocked register",
            }));
        },
        [&] { record(workspace.ReplaceNodeType(guarded_descriptor(2))); });
    Expect(workspace.Execute(std::move(command)).has_value() && workspace.Undo().has_value() &&
               workspace.Redo().has_value() && callback_errors == std::vector<ErrorCode>(3, ErrorCode::CommandFailed) &&
               !workspace.Registry().Find(TypeId{"registry.command-register"}) &&
               workspace.Registry().Find(guarded_type)->version == 1,
           "Apply, revert, and redo callbacks must hold the node registry "
           "invocation lease");

    GraphPolicy leaf_policy;
    leaf_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                         const OperationIntent&) -> OperationPolicyDecision {
        record(workspace.UnregisterNodeType(guarded_type));
        return AllowOperation{};
    };
    Expect(workspace.Execute(std::make_unique<SetSchemaVersionCommand>(3), leaf_policy).has_value() &&
               callback_errors.back() == ErrorCode::CommandFailed && workspace.Registry().Find(guarded_type),
           "Leaf policy callbacks must not mutate the node registry");

    GraphPolicy batch_policy;
    batch_policy.evaluate_batch = [&](const BatchPolicyContext&,
                                      std::span<const OperationIntent>) -> BatchPolicyDecision {
        record(workspace.RegisterNodeType(NodeTypeDescriptor{
            .type = TypeId{"registry.batch-register"},
            .display_name = "Blocked batch register",
        }));
        return AllowBatch{};
    };
    Expect(workspace.Execute(std::make_unique<SetSchemaVersionCommand>(4), batch_policy).has_value() &&
               callback_errors.back() == ErrorCode::CommandFailed &&
               !workspace.Registry().Find(TypeId{"registry.batch-register"}),
           "Batch policy callbacks must not mutate the node registry");

    bool replace_once = true;
    GraphPolicy replacement_policy;
    replacement_policy.evaluate_batch = [&](const BatchPolicyContext&,
                                            std::span<const OperationIntent>) -> BatchPolicyDecision {
        if (!replace_once) return AllowBatch{};
        replace_once = false;
        return ReplaceBatch{[&]() -> std::unique_ptr<Command> {
            record(workspace.ReplaceNodeType(guarded_descriptor(2)));
            return std::make_unique<SetSchemaVersionCommand>(5);
        }};
    };
    Expect(workspace.Execute(std::make_unique<SetSchemaVersionCommand>(5), replacement_policy).has_value() &&
               callback_errors.back() == ErrorCode::CommandFailed &&
               workspace.Registry().Find(guarded_type)->version == 1,
           "Replacement factories must remain inside the registry invocation "
           "boundary");

    RegistryCatalog raw_registry;
    CommandStack raw_commands;
    GraphDocument raw_document;
    GraphPresentation raw_presentation;
    const TypeId raw_guarded{"registry.raw-command-guard"};
    Expect(raw_registry
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = raw_guarded,
                   .display_name = "Raw command guard",
               })
               .has_value(),
           "Raw command guard descriptor must register");
    std::vector<ErrorCode> raw_errors;
    const auto record_raw = [&](auto result) {
        if (!result) raw_errors.push_back(result.error().code);
    };
    auto raw_command = std::make_unique<RegistryCallbackCommand>(
        2,
        [&] {
            record_raw(raw_registry.RegisterNodeType(NodeTypeDescriptor{
                .type = TypeId{"registry.raw-register"},
                .display_name = "Raw blocked register",
            }));
        },
        [&] {
            record_raw(raw_registry.ReplaceNodeType(NodeTypeDescriptor{
                .type = raw_guarded,
                .display_name = "Raw blocked replacement",
                .version = 2,
            }));
        });
    Expect(raw_commands.Execute(std::move(raw_command), raw_document, raw_presentation, raw_registry).has_value() &&
               raw_commands.Undo(raw_document, raw_presentation, raw_registry).has_value() &&
               raw_commands.Redo(raw_document, raw_presentation, raw_registry).has_value() &&
               raw_errors == std::vector<ErrorCode>(3, ErrorCode::CommandFailed),
           "Raw node registry mutations must be blocked during apply, revert, "
           "and redo");

    GraphPolicy raw_leaf_policy;
    raw_leaf_policy.evaluate_operation = [&](const OperationPolicyContext&,
                                             const OperationIntent&) -> OperationPolicyDecision {
        record_raw(raw_registry.UnregisterNodeType(raw_guarded));
        return AllowOperation{};
    };
    Expect(raw_commands
                   .Execute(std::make_unique<SetSchemaVersionCommand>(3), raw_document, raw_presentation, raw_registry,
                            raw_leaf_policy)
                   .has_value() &&
               raw_errors.back() == ErrorCode::CommandFailed,
           "Raw node registry mutation must be blocked during leaf policy "
           "evaluation");

    GraphPolicy raw_batch_policy;
    raw_batch_policy.evaluate_batch = [&](const BatchPolicyContext&,
                                          std::span<const OperationIntent>) -> BatchPolicyDecision {
        record_raw(raw_registry.RegisterNodeType(NodeTypeDescriptor{
            .type = TypeId{"registry.raw-batch"},
            .display_name = "Raw batch blocked",
        }));
        return AllowBatch{};
    };
    Expect(raw_commands
                   .Execute(std::make_unique<SetSchemaVersionCommand>(4), raw_document, raw_presentation, raw_registry,
                            raw_batch_policy)
                   .has_value() &&
               raw_errors.back() == ErrorCode::CommandFailed,
           "Raw node registry mutation must be blocked during batch policy "
           "evaluation");

    bool raw_replace_once = true;
    GraphPolicy raw_replacement_policy;
    raw_replacement_policy.evaluate_batch = [&](const BatchPolicyContext&,
                                                std::span<const OperationIntent>) -> BatchPolicyDecision {
        if (!raw_replace_once) return AllowBatch{};
        raw_replace_once = false;
        return ReplaceBatch{[&]() -> std::unique_ptr<Command> {
            record_raw(raw_registry.ReplaceNodeType(NodeTypeDescriptor{
                .type = raw_guarded,
                .display_name = "Raw factory blocked",
                .version = 2,
            }));
            return std::make_unique<SetSchemaVersionCommand>(5);
        }};
    };
    Expect(raw_commands
                   .Execute(std::make_unique<SetSchemaVersionCommand>(5), raw_document, raw_presentation, raw_registry,
                            raw_replacement_policy)
                   .has_value() &&
               raw_errors.back() == ErrorCode::CommandFailed,
           "Raw node registry mutation must be blocked inside replacement "
           "factories");

    GraphDocument moved_document;
    GraphPresentation moved_presentation;
    CommandStack moved_commands;
    auto registry_owner = std::make_unique<RegistryCatalog>();
    std::unique_ptr<RegistryCatalog> moved_registry_owner;
    std::optional<ErrorCode> moved_mutation_error;
    auto move_owner = std::make_unique<RegistryCallbackCommand>(2, [&] {
        moved_registry_owner = std::make_unique<RegistryCatalog>(std::move(*registry_owner));
        registry_owner.reset();
        auto mutation = moved_registry_owner->RegisterNodeType(NodeTypeDescriptor{
            .type = TypeId{"registry.moved-owner"},
            .display_name = "Moved owner",
        });
        if (!mutation) moved_mutation_error = mutation.error().code;
        moved_registry_owner.reset();
    });
    Expect(moved_commands.Execute(std::move(move_owner), moved_document, moved_presentation, *registry_owner)
                   .has_value() &&
               !registry_owner && !moved_registry_owner && moved_mutation_error == ErrorCode::CommandFailed,
           "Node registry leases must survive public owner move and destruction "
           "during Apply");

    NodeEditorWorkspace catalog;
    const TypeId converter{"registry.atomic-converter"};
    const TypeId source{"registry.atomic-source"};
    const TypeId destination{"registry.atomic-destination"};
    const auto converter_descriptor = [&](const std::uint32_t version) {
        return NodeTypeDescriptor{
            .type = converter,
            .display_name = "Atomic converter",
            .version = version,
            .static_pins =
                {
                    PinDescriptor{.key = "data-in", .type = source},
                    PinDescriptor{
                        .key = "data-out",
                        .type = destination,
                        .direction = PinDirection::Output,
                    },
                    PinDescriptor{
                        .key = "exec-in",
                        .type = source,
                        .kind = PinKind::Execution,
                    },
                    PinDescriptor{
                        .key = "exec-in-2",
                        .type = source,
                        .kind = PinKind::Execution,
                    },
                    PinDescriptor{
                        .key = "exec-out",
                        .type = destination,
                        .direction = PinDirection::Output,
                        .kind = PinKind::Execution,
                    },
                },
        };
    };
    const ConversionDescriptor data_conversion{
        .key = ConversionKey{source, destination, PinKind::Data},
        .node_type = converter,
        .input_pin = "data-in",
        .output_pin = "data-out",
    };
    const ConversionDescriptor execution_conversion{
        .key = ConversionKey{source, destination, PinKind::Execution},
        .node_type = converter,
        .input_pin = "exec-in",
        .output_pin = "exec-out",
    };
    Expect(catalog.RegisterNodeType(converter_descriptor(1)).has_value(), "Atomic converter descriptor must register");
    auto data_token = catalog.RegisterConversion(data_conversion);
    auto execution_token = catalog.RegisterConversion(execution_conversion);
    Expect(data_token && execution_token, "Atomic converter recipes must register");

    const RegistrySnapshot old_snapshot = catalog.Registry().Snapshot();
    const std::uint64_t old_generation = catalog.Registry().Generation();
    auto failed_update = catalog.BeginUpdate();
    Expect(failed_update.has_value(), "Registry update must begin");
    Expect(failed_update->ReplaceNodeType(converter_descriptor(2)).has_value(), "Node replacement must stage");
    ConversionDescriptor invalid_execution = execution_conversion;
    invalid_execution.output_pin = "missing";
    Expect(failed_update->ReplaceConversion(*execution_token, invalid_execution).has_value(),
           "Invalid late recipe must stage for atomic validation");
    auto failed_commit = failed_update->Commit();
    GraphDocument old_recipe_document;
    auto live_after_failure =
        catalog.Registry().Check(source, destination, PinKind::Execution).recipe->Instantiate(old_recipe_document);
    Expect(!failed_commit && failed_commit.error().code == ErrorCode::InvalidArgument &&
               catalog.Registry().Generation() == old_generation && catalog.Registry().Find(converter)->version == 1 &&
               live_after_failure && live_after_failure->node.type_version == 1,
           "A failed late recipe must roll back the complete registry update");

    auto successful_update = catalog.BeginUpdate();
    Expect(successful_update && successful_update->ReplaceNodeType(converter_descriptor(2)).has_value(),
           "Atomic reload must stage");
    auto successful_commit = successful_update->Commit();
    GraphDocument new_recipe_document;
    auto live_v2 =
        catalog.Registry().Check(source, destination, PinKind::Execution).recipe->Instantiate(new_recipe_document);
    auto retained_v1 =
        old_snapshot.Check(source, destination, PinKind::Execution).recipe->Instantiate(old_recipe_document);
    Expect(successful_commit && successful_commit->generation == old_generation + 1 &&
               successful_commit->statistics.path_copies > 0 && successful_commit->statistics.recipes_built == 2 &&
               successful_commit->statistics.published_generations == 1 && live_v2 && live_v2->node.type_version == 2 &&
               retained_v1 && retained_v1->node.type_version == 1 && old_snapshot.Find(converter)->version == 1,
           "Atomic reload must clone each substate once and preserve the "
           "previous generation");

    auto swap_keys = catalog.BeginUpdate();
    ConversionDescriptor data_to_execution = execution_conversion;
    ConversionDescriptor execution_to_data = data_conversion;
    ConversionDescriptor changed_execution = execution_conversion;
    changed_execution.input_pin = "exec-in-2";
    Expect(swap_keys && swap_keys->ReplaceConversion(*data_token, data_to_execution).has_value() &&
               swap_keys->ReplaceConversion(*execution_token, changed_execution).has_value() &&
               swap_keys->ReplaceConversion(*execution_token, execution_to_data).has_value() &&
               swap_keys->Commit().has_value() &&
               catalog.Registry().Check(source, destination, PinKind::Data).recipe->Registration() ==
                   *execution_token &&
               catalog.Registry().Check(source, destination, PinKind::Execution).recipe->Registration() == *data_token,
           "A displaced recipe must survive its own intermediate no-op during an "
           "atomic key swap");

    auto first_update = catalog.BeginUpdate();
    auto conflicting_update = catalog.BeginUpdate();
    Expect(first_update && conflicting_update && first_update->ReplaceNodeType(converter_descriptor(3)).has_value() &&
               conflicting_update->ReplaceNodeType(converter_descriptor(4)).has_value() &&
               first_update->Commit().has_value(),
           "Concurrent registry updates must stage from one base generation");
    auto conflict = conflicting_update->Commit();
    Expect(!conflict && conflict.error().code == ErrorCode::RevisionConflict &&
               catalog.Registry().Find(converter)->version == 3,
           "A stale registry update must not overwrite a newer generation");

    auto unload = catalog.BeginUpdate();
    Expect(unload && unload->UnregisterConversion(*data_token).has_value() &&
               unload->UnregisterConversion(*execution_token).has_value() &&
               unload->UnregisterNodeType(converter).has_value() && unload->Commit().has_value() &&
               !catalog.Registry().Find(converter) &&
               catalog.Registry().Check(source, destination, PinKind::Data).status ==
                   ConnectionResult::Status::Rejected,
           "Plugin unload must remove recipes and their node type in one "
           "generation");
}

void TestRegistryNoOpsStructureAndSelectiveDependencies() {
    const TypeId converter{"registry.contract.converter"};
    const TypeId source{"registry.contract.source"};
    const TypeId destination{"registry.contract.destination"};
    const NodeTypeDescriptor converter_v1{
        .type = converter,
        .display_name = "Contract converter",
        .static_pins =
            {
                PinDescriptor{.key = "input", .type = source},
                PinDescriptor{.key = "alternate-input", .type = source},
                PinDescriptor{
                    .key = "output",
                    .type = destination,
                    .direction = PinDirection::Output,
                },
            },
    };
    const ConversionDescriptor conversion_v1{
        .key = ConversionKey{source, destination, PinKind::Data},
        .node_type = converter,
        .input_pin = "input",
        .output_pin = "output",
    };
    const ConversionDescriptor conversion_v2{
        .key = conversion_v1.key,
        .node_type = converter,
        .input_pin = "alternate-input",
        .output_pin = "output",
    };

    RegistryCatalog catalog;
    Expect(catalog.RegisterNodeType(converter_v1).has_value(), "No-op converter must register");
    auto registration = catalog.RegisterConversion(conversion_v1);
    Expect(registration.has_value(), "No-op conversion must register");
    const ConversionRecipe recipe = *catalog.Check(source, destination, PinKind::Data).recipe;
    const std::uint64_t generation = catalog.Generation();
    const std::uint64_t node_revision = catalog.NodeRevision();
    const std::uint64_t conversion_revision = catalog.ConversionRevision();
    Expect(catalog.ReplaceNodeType(converter_v1).has_value() &&
               catalog.ReplaceConversion(*registration, conversion_v1).has_value() &&
               catalog.Generation() == generation && catalog.NodeRevision() == node_revision &&
               catalog.ConversionRevision() == conversion_revision &&
               *catalog.Check(source, destination, PinKind::Data).recipe == recipe,
           "Identical replacements must preserve revisions, generation, and recipe "
           "identity");

    auto no_op = catalog.BeginUpdate();
    NodeTypeDescriptor converter_v2 = converter_v1;
    converter_v2.display_name = "Temporary converter name";
    Expect(no_op && no_op->ReplaceNodeType(converter_v2).has_value() &&
               no_op->ReplaceNodeType(converter_v1).has_value() &&
               no_op->ReplaceConversion(*registration, conversion_v2).has_value() &&
               no_op->ReplaceConversion(*registration, conversion_v1).has_value(),
           "Net no-op registry batch must stage");
    auto no_op_result = no_op->Commit();
    Expect(no_op_result && no_op_result->statistics.published_generations == 0 && catalog.Generation() == generation &&
               catalog.NodeRevision() == node_revision && catalog.ConversionRevision() == conversion_revision &&
               *catalog.Check(source, destination, PinKind::Data).recipe == recipe,
           "Net no-op registry batch must retain baseline roots and identities");

    RegistryCatalog first_identity;
    RegistryCatalog second_identity;
    for (RegistryCatalog* identity : {&first_identity, &second_identity}) {
        Expect(identity->RegisterNodeType(converter_v1).has_value() &&
                   identity->RegisterConversion(conversion_v1).has_value(),
               "Independent identity catalog must initialize");
    }
    const ConversionRecipe foreign = *first_identity.Check(source, destination, PinKind::Data).recipe;
    const auto foreign_result = second_identity.Snapshot().ValidateRecipe(foreign);
    Expect(first_identity.Generation() == second_identity.Generation() && !foreign_result &&
               foreign_result.error().code == ErrorCode::RegistryMismatch,
           "Equal numeric generations must not make foreign recipe identities "
           "interchangeable");
    GraphDocument foreign_document;
    GraphPresentation foreign_presentation;
    CommandStack foreign_commands;
    auto foreign_creation = foreign.Instantiate(foreign_document);
    Expect(foreign_creation.has_value(), "Foreign conversion command fixture must instantiate");
    const PinId foreign_input = foreign_creation->pins.front().id;
    const PinId foreign_output = foreign_creation->pins.back().id;
    auto foreign_command = foreign_commands.Execute(
        std::make_unique<InsertConversionCommand>(
            foreign_document.RootGraph(), foreign, std::move(*foreign_creation), NodePresentation{},
            Link{foreign_document.AllocateLinkId(), PinId{999}, foreign_input},
            Link{foreign_document.AllocateLinkId(), foreign_output, PinId{1'000}}),
        foreign_document, foreign_presentation, second_identity);
    Expect(!foreign_command && foreign_command.error().code == ErrorCode::RegistryMismatch &&
               first_identity.Generation() == second_identity.Generation(),
           "InsertConversionCommand must reject a foreign recipe even at an "
           "equal numeric generation");

    RegistryCatalog large;
    auto setup = large.BeginUpdate();
    constexpr std::size_t LargeCatalogSize = 4'096;
    for (std::size_t index = 0; index < LargeCatalogSize; ++index) {
        Expect(setup
                   ->RegisterNodeType(NodeTypeDescriptor{
                       .type = TypeId{"registry.large." + std::to_string(index)},
                       .display_name = "Large " + std::to_string(index),
                   })
                   .has_value(),
               "Large catalog descriptor must stage");
    }
    Expect(setup->Commit().has_value(), "Large catalog must publish");
    auto point = large.BeginUpdate();
    Expect(point
               ->ReplaceNodeType(NodeTypeDescriptor{
                   .type = TypeId{"registry.large.2048"},
                   .display_name = "Large changed",
                   .version = 2,
               })
               .has_value(),
           "Large catalog point update must stage");
    ResetTransactionMetrics();
    auto point_result = point->Commit();
    const TransactionMetrics registry_metrics = GetTransactionMetrics();
    Expect(point_result && point_result->statistics.path_copies > 0 && point_result->statistics.path_copies < 64 &&
               point_result->statistics.touched_records == 1 && registry_metrics.semantic_indexes.logical_bytes == 0 &&
               registry_metrics.copied_logical_bytes == 0,
           "Registry path copies must remain logarithmic and isolated from graph "
           "transaction metrics");

    RegistryCatalog dependencies;
    const TypeId type_a{"registry.dependency.a"};
    const TypeId type_b{"registry.dependency.b"};
    const TypeId type_c{"registry.dependency.c"};
    const TypeId type_d{"registry.dependency.prepared"};
    const auto descriptor = [](const TypeId& type, const std::uint32_t version) {
        return NodeTypeDescriptor{
            .type = type,
            .display_name = type.Value(),
            .version = version,
            .property_impacts = {{"history", PropertyImpact::RuntimeOnly}},
        };
    };
    Expect(dependencies.RegisterNodeType(descriptor(type_a, 1)).has_value() &&
               dependencies.RegisterNodeType(descriptor(type_b, 1)).has_value() &&
               dependencies.RegisterNodeType(descriptor(type_c, 1)).has_value() &&
               dependencies.RegisterNodeType(descriptor(type_d, 1)).has_value(),
           "Dependency node descriptors must register");

    const TypeId conversion_node{"registry.dependency.converter"};
    const TypeId ab_source{"registry.dependency.ab-source"};
    const TypeId ab_destination{"registry.dependency.ab-destination"};
    const TypeId cd_source{"registry.dependency.cd-source"};
    const TypeId cd_destination{"registry.dependency.cd-destination"};
    Expect(dependencies
               .RegisterNodeType(NodeTypeDescriptor{
                   .type = conversion_node,
                   .display_name = "Dependency converter",
                   .static_pins =
                       {
                           PinDescriptor{.key = "ab-in", .type = ab_source},
                           PinDescriptor{.key = "ab-in-2", .type = ab_source},
                           PinDescriptor{.key = "cd-in", .type = cd_source},
                           PinDescriptor{.key = "cd-in-2", .type = cd_source},
                           PinDescriptor{
                               .key = "ab-out",
                               .type = ab_destination,
                               .direction = PinDirection::Output,
                           },
                           PinDescriptor{
                               .key = "cd-out",
                               .type = cd_destination,
                               .direction = PinDirection::Output,
                           },
                       },
               })
               .has_value(),
           "Dependency conversion node must register");
    ConversionDescriptor ab{
        .key = ConversionKey{ab_source, ab_destination, PinKind::Data},
        .node_type = conversion_node,
        .input_pin = "ab-in",
        .output_pin = "ab-out",
    };
    ConversionDescriptor cd{
        .key = ConversionKey{cd_source, cd_destination, PinKind::Data},
        .node_type = conversion_node,
        .input_pin = "cd-in",
        .output_pin = "cd-out",
    };
    auto ab_registration = dependencies.RegisterConversion(ab);
    auto cd_registration = dependencies.RegisterConversion(cd);
    Expect(ab_registration && cd_registration, "Dependency conversions must register");

    GraphPolicy defer;
    defer.evaluate_batch = [](const BatchPolicyContext&, std::span<const OperationIntent>) -> BatchPolicyDecision {
        return DeferBatch{std::uint64_t{1}};
    };
    GraphDocument document;
    GraphPresentation presentation;
    CommandStack commands;

    auto no_read =
        commands.Execute(std::make_unique<SetSchemaVersionCommand>(2), document, presentation, dependencies, defer);
    Expect(no_read && no_read->deferred && dependencies.ReplaceNodeType(descriptor(type_c, 2)).has_value() &&
               commands.Resume(no_read->deferred->id, document, presentation, ResumeMode::CommitPrepared).has_value(),
           "Deferred command with no registry reads must survive an unrelated "
           "update");

    commands.Clear();
    auto stale_creation = dependencies.Instantiate(document, type_d);
    Expect(stale_creation && dependencies.ReplaceNodeType(descriptor(type_d, 2)).has_value(),
           "Prepared node stale-schema fixture must initialize");
    auto stale_creation_result =
        commands.Execute(std::make_unique<AddNodeCommand>(document.RootGraph(), std::move(*stale_creation)), document,
                         presentation, dependencies);
    Expect(!stale_creation_result && stale_creation_result.error().code == ErrorCode::InvalidGraph,
           "Add node must validate a prepared creation against the current "
           "descriptor schema");

    auto dynamic_creation = dependencies.Instantiate(document, type_d);
    Expect(dynamic_creation.has_value(), "Prepared dynamic-pin dependency fixture must instantiate");
    const PinId dynamic_pin = document.AllocatePinId();
    dynamic_creation->node.pins.push_back(dynamic_pin);
    dynamic_creation->pins.push_back(PinInstance{
        .id = dynamic_pin,
        .node = dynamic_creation->node.id,
        .key = "intentional-dynamic",
        .label = "Intentional dynamic",
        .type = TypeId{"float"},
        .storage = PinStorage::Dynamic,
    });
    auto prepared_node =
        commands.Execute(std::make_unique<AddNodeCommand>(document.RootGraph(), std::move(*dynamic_creation)), document,
                         presentation, dependencies, defer);
    Expect(prepared_node && prepared_node->deferred && dependencies.ReplaceNodeType(descriptor(type_d, 3)).has_value(),
           "Prepared add-node with an intentional dynamic pin must defer and "
           "track its descriptor");
    auto prepared_node_resume =
        commands.Resume(prepared_node->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!prepared_node_resume && prepared_node_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(prepared_node->deferred->id).has_value(),
           "Deferred add-node must conflict when its related descriptor is "
           "replaced");

    const NodeId property_node = document.AllocateNodeId();
    Expect(
        commands
            .Execute(std::make_unique<AddNodeCommand>(
                         document.RootGraph(), NodeCreation{.node = NodeInstance{.id = property_node, .type = type_a}}),
                     document, presentation, dependencies)
            .has_value(),
        "Property dependency node must execute");
    commands.Clear();
    auto positive =
        commands.Execute(std::make_unique<SetNodePropertyCommand>(document.RootGraph(), property_node, "history",
                                                                  PropertyValue{std::int64_t{1}}),
                         document, presentation, dependencies, defer);
    Expect(positive && positive->deferred && dependencies.ReplaceNodeType(descriptor(type_b, 2)).has_value() &&
               commands.Resume(positive->deferred->id, document, presentation, ResumeMode::CommitPrepared).has_value(),
           "Property type dependency must survive an unrelated descriptor update");

    auto related = commands.Execute(std::make_unique<SetNodePropertyCommand>(document.RootGraph(), property_node,
                                                                             "history", PropertyValue{std::int64_t{2}}),
                                    document, presentation, dependencies, defer);
    Expect(related && related->deferred && dependencies.ReplaceNodeType(descriptor(type_a, 2)).has_value(),
           "Related node dependency fixture must defer");
    auto related_resume = commands.Resume(related->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!related_resume && related_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(related->deferred->id).has_value(),
           "Related positive lookup change must conflict");

    const TypeId missing{"registry.dependency.missing"};
    auto negative = commands.Execute(std::make_unique<RegistryDependencyCommand>(missing, 4), document, presentation,
                                     dependencies, defer);
    Expect(negative && negative->deferred && dependencies.RegisterNodeType(descriptor(missing, 1)).has_value(),
           "Negative node dependency fixture must defer");
    auto negative_resume = commands.Resume(negative->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!negative_resume && negative_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(negative->deferred->id).has_value(),
           "Negative lookup becoming positive must conflict");

    auto conversion_unrelated = commands.Execute(std::make_unique<RegistryDependencyCommand>(ab.key, 4), document,
                                                 presentation, dependencies, defer);
    cd.input_pin = "cd-in-2";
    Expect(conversion_unrelated && conversion_unrelated->deferred &&
               dependencies.ReplaceConversion(*cd_registration, cd).has_value() &&
               commands.Resume(conversion_unrelated->deferred->id, document, presentation, ResumeMode::CommitPrepared)
                   .has_value(),
           "Conversion-key dependency must survive an unrelated conversion update");

    auto conversion_related = commands.Execute(std::make_unique<RegistryDependencyCommand>(ab.key, 5), document,
                                               presentation, dependencies, defer);
    ab.input_pin = "ab-in-2";
    Expect(conversion_related && conversion_related->deferred &&
               dependencies.ReplaceConversion(*ab_registration, ab).has_value(),
           "Related conversion dependency fixture must defer");
    auto conversion_resume =
        commands.Resume(conversion_related->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!conversion_resume && conversion_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(conversion_related->deferred->id).has_value(),
           "Related conversion recipe change must conflict");

    commands.Clear();
    Expect(
        commands.Execute(std::make_unique<RegistryDependencyCommand>(type_a, 5), document, presentation, dependencies)
            .has_value(),
        "History dependency command must execute");
    auto pending_undo =
        commands.Undo(document, presentation, defer, UndoPolicyMode::RespectCurrentPolicy, dependencies);
    Expect(
        pending_undo && pending_undo->deferred && dependencies.ReplaceNodeType(descriptor(type_b, 3)).has_value() &&
            commands.Resume(pending_undo->deferred->id, document, presentation, ResumeMode::CommitPrepared).has_value(),
        "Deferred undo must survive an unrelated descriptor update");
    auto pending_redo = commands.Redo(document, presentation, dependencies, defer);
    Expect(
        pending_redo && pending_redo->deferred && dependencies.ReplaceNodeType(descriptor(type_c, 3)).has_value() &&
            commands.Resume(pending_redo->deferred->id, document, presentation, ResumeMode::CommitPrepared).has_value(),
        "Deferred redo must survive an unrelated descriptor update");
    auto related_undo =
        commands.Undo(document, presentation, defer, UndoPolicyMode::RespectCurrentPolicy, dependencies);
    Expect(related_undo && related_undo->deferred && dependencies.ReplaceNodeType(descriptor(type_a, 3)).has_value(),
           "Related undo dependency fixture must defer");
    auto related_undo_resume =
        commands.Resume(related_undo->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!related_undo_resume && related_undo_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(related_undo->deferred->id).has_value(),
           "Related undo registry dependency must conflict");
    Expect(commands.Undo(document, presentation, dependencies).has_value(),
           "History command must enter redo state after the related undo fixture");
    auto related_redo = commands.Redo(document, presentation, dependencies, defer);
    Expect(related_redo && related_redo->deferred && dependencies.ReplaceNodeType(descriptor(type_a, 4)).has_value(),
           "Related redo dependency fixture must defer");
    auto related_redo_resume =
        commands.Resume(related_redo->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!related_redo_resume && related_redo_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(related_redo->deferred->id).has_value(),
           "Related redo registry dependency must conflict");

    commands.Clear();
    auto node_revision_unrelated = commands.Execute(
        std::make_unique<RegistryRevisionDependencyCommand>(RegistryRevisionDependencyCommand::Domain::Nodes, 6),
        document, presentation, dependencies, defer);
    NodeTypeDescriptor temporary_b = *dependencies.Find(type_b);
    temporary_b.display_name = "Temporary dependency B";
    const NodeTypeDescriptor original_b = *dependencies.Find(type_b);
    ConversionDescriptor changed_cd = cd;
    changed_cd.input_pin = changed_cd.input_pin == "cd-in" ? "cd-in-2" : "cd-in";
    auto mixed_domain_update = dependencies.BeginUpdate();
    Expect(node_revision_unrelated && node_revision_unrelated->deferred && mixed_domain_update &&
               mixed_domain_update->ReplaceNodeType(temporary_b).has_value() &&
               mixed_domain_update->ReplaceNodeType(original_b).has_value() &&
               mixed_domain_update->ReplaceConversion(*cd_registration, changed_cd).has_value() &&
               mixed_domain_update->Commit().has_value() &&
               commands.Resume(node_revision_unrelated->deferred->id, document, presentation,
                               ResumeMode::CommitPrepared)
                   .has_value(),
           "A net-no-op node domain must retain its root while another registry domain changes");
    cd = changed_cd;

    auto node_revision_dependency = commands.Execute(
        std::make_unique<RegistryRevisionDependencyCommand>(RegistryRevisionDependencyCommand::Domain::Nodes, 6),
        document, presentation, dependencies, defer);
    Expect(node_revision_dependency && node_revision_dependency->deferred &&
               dependencies.ReplaceNodeType(descriptor(type_b, 4)).has_value(),
           "Node-revision dependency fixture must defer");
    auto node_revision_resume =
        commands.Resume(node_revision_dependency->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!node_revision_resume && node_revision_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(node_revision_dependency->deferred->id).has_value(),
           "NodeRevision reads must track the exact node-domain root and revision");

    auto conversion_revision_dependency = commands.Execute(
        std::make_unique<RegistryRevisionDependencyCommand>(RegistryRevisionDependencyCommand::Domain::Conversions, 6),
        document, presentation, dependencies, defer);
    cd.input_pin = cd.input_pin == "cd-in" ? "cd-in-2" : "cd-in";
    Expect(conversion_revision_dependency && conversion_revision_dependency->deferred &&
               dependencies.ReplaceConversion(*cd_registration, cd).has_value(),
           "Conversion-revision dependency fixture must defer");
    auto conversion_revision_resume = commands.Resume(conversion_revision_dependency->deferred->id, document,
                                                      presentation, ResumeMode::CommitPrepared);
    Expect(!conversion_revision_resume && conversion_revision_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(conversion_revision_dependency->deferred->id).has_value(),
           "ConversionRevision reads must track the exact conversion-domain root "
           "and revision");

    auto generation_dependency = commands.Execute(
        std::make_unique<RegistryRevisionDependencyCommand>(RegistryRevisionDependencyCommand::Domain::Generation, 6),
        document, presentation, dependencies, defer);
    Expect(generation_dependency && generation_dependency->deferred &&
               dependencies.ReplaceNodeType(descriptor(type_c, 4)).has_value(),
           "Generation dependency fixture must defer");
    auto generation_resume =
        commands.Resume(generation_dependency->deferred->id, document, presentation, ResumeMode::CommitPrepared);
    Expect(!generation_resume && generation_resume.error().code == ErrorCode::RevisionConflict &&
               commands.Cancel(generation_dependency->deferred->id).has_value(),
           "Generation reads must track the exact combined published state");

    int replacement_attempt = 0;
    GraphPolicy replace_then_defer;
    replace_then_defer.evaluate_batch = [&](const BatchPolicyContext&,
                                            std::span<const OperationIntent>) -> BatchPolicyDecision {
        if (replacement_attempt++ == 0) {
            return ReplaceBatch{[&] { return std::make_unique<RegistryDependencyCommand>(type_b, 6); }};
        }
        return DeferBatch{std::uint64_t{2}};
    };
    auto replaced_dependency = commands.Execute(std::make_unique<RegistryDependencyCommand>(type_a, 6), document,
                                                presentation, dependencies, replace_then_defer);
    Expect(replaced_dependency && replaced_dependency->deferred && replacement_attempt == 2 &&
               dependencies.ReplaceNodeType(descriptor(type_a, 5)).has_value() &&
               commands.Resume(replaced_dependency->deferred->id, document, presentation, ResumeMode::CommitPrepared)
                   .has_value(),
            "A deferred replacement must retain only the final command attempt's "
            "registry dependencies");

    std::optional<RegistrySnapshot> escaped_snapshot;
    auto escaped_dependency = commands.Execute(
        std::make_unique<EscapedRegistrySnapshotCommand>(escaped_snapshot, 7), document, presentation, dependencies,
        defer);
    Expect(escaped_dependency && escaped_dependency->deferred && escaped_snapshot.has_value(),
           "Escaped tracked snapshot fixture must defer");
    (void)escaped_snapshot->Find(type_c);
    const NodeTypeDescriptor current_c = *dependencies.Find(type_c);
    NodeTypeDescriptor changed_c = current_c;
    ++changed_c.version;
    Expect(dependencies.ReplaceNodeType(std::move(changed_c)).has_value() &&
               commands.Resume(escaped_dependency->deferred->id, document, presentation, ResumeMode::CommitPrepared)
                   .has_value(),
           "A tracked snapshot must stop accepting dependencies after command staging completes");
}

void TestRandomizedCommandStateMachine() {
    GraphDocument document;
    GraphPresentation presentation;
    RegistryCatalog types;
    CommandStack commands{CommandStack::Options{
        .history_limit = 64,
        .max_policy_batch_operations = 256,
        .max_replacements = 4,
    }};
    const GraphId graph = document.RootGraph();
    const NodeId node = document.AllocateNodeId();
    Execute(commands,
            std::make_unique<AddNodeCommand>(
                graph, NodeCreation{.node = NodeInstance{.id = node, .type = TypeId{"state-machine"}}},
                NodePresentation{.position = {0.0f, 0.0f}}),
            document, presentation, types, "State-machine node must execute");

    std::mt19937 random{0xC0FFEEU};
    std::uint64_t sequence = 0;
    for (std::size_t step = 0; step < 500; ++step) {
        switch (random() % 5U) {
        case 0: {
            auto result = commands.Execute(
                std::make_unique<SetNodePropertyCommand>(graph, node, "random-" + std::to_string(sequence),
                                                         PropertyValue{static_cast<std::int64_t>(sequence)}),
                document, presentation, types);
            Expect(result.has_value(), "Random property execute must succeed");
            ++sequence;
            break;
        }
        case 1: {
            const Vec2 before = presentation.FindNode(node)->position;
            const Vec2 after{before.x + 1.0f, before.y - 1.0f};
            auto result =
                commands.Execute(std::make_unique<MoveNodesCommand>(graph, MoveNodesCommand::Positions{{node, before}},
                                                                    MoveNodesCommand::Positions{{node, after}}),
                                 document, presentation, types);
            Expect(result.has_value(), "Random presentation execute must succeed");
            break;
        }
        case 2:
            if (commands.CanUndo())
                Expect(commands.Undo(document, presentation, types).has_value(), "Random undo must succeed");
            break;
        case 3:
            if (commands.CanRedo())
                Expect(commands.Redo(document, presentation, types).has_value(), "Random redo must succeed");
            break;
        case 4: {
            GraphPolicy defer;
            defer.evaluate_batch = [](const BatchPolicyContext&,
                                      std::span<const OperationIntent>) -> BatchPolicyDecision {
                return DeferBatch{std::uint64_t{1}};
            };
            auto pending =
                commands.Execute(std::make_unique<SetNodePropertyCommand>(
                                     graph, node, "deferred-" + std::to_string(sequence), PropertyValue{true}),
                                 document, presentation, types, defer);
            Expect(pending && pending->deferred, "Random deferred execute must prepare a transaction");
            if ((random() & 1U) == 0) {
                Expect(commands.Cancel(pending->deferred->id).has_value(), "Random cancel must succeed");
            } else {
                Expect(commands.Resume(pending->deferred->id, document, presentation, ResumeMode::CommitPrepared)
                           .has_value(),
                       "Random resume must succeed");
            }
            ++sequence;
            break;
        }
        }
        if (step % 17 == 0) {
            Expect(document.ValidateStructure().has_value() &&
                       ValidateGraphPresentation(document, presentation).has_value(),
                   "Random state-machine prefix must satisfy full validation oracles");
        }
    }
}

} // namespace

int main() {
    TestCoreCommandsAndRevisions();
    TestGraphAndPinCommands();
    TestPresentationCommands();
    TestLargeGroupStyleMutationMetrics();
    TestPersistentLinkPresentationMutations();
    TestLinkPresentationExactUndoAndJournal();
    TestMultiChunkPersistentRoutes();
    TestCompatibilityOwnershipAndHistory();
    TestTypeCompletionPoliciesAndProtection();
    TestCompoundAtomicityAndRevisionConflict();
    TestTransactionCopyOnWrite();
    TestStageThreeCommandsFragmentsAndLayout();
    TestStageThreeEditorGestures();
    TestNodeUiExtensibility();
    TestMovedStateAndEditorReadOnly();
    TestStageFourSubgraphsAndIntergraphLinks();
    TestStageFiveEditorGeometryCacheAndRouting();
    TestBreakingPolicyArchitecture();
    TestRemovedGraphOwnershipClosure();
    TestPersistentAdjacencyRanges();
    TestRegistrySnapshotsAndInvocationSafety();
    TestConversionSnapshotsAndInvocationSafety();
    TestConversionIdentityAndLifecycle();
    TestRegistryCommandBoundaryAndAtomicUpdates();
    TestRegistryNoOpsStructureAndSelectiveDependencies();
    TestRandomizedCommandStateMachine();
    TestIdExhaustion();
    TestRegistryRevisionOverflowGuard();
    return EXIT_SUCCESS;
}
