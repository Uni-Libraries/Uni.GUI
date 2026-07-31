#include <uni/gui/nodes/nodes.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace Uni::GUI::Nodes;

constexpr std::size_t TraceLimit = 32;
constexpr std::size_t RecentIdLimit = 96;
constexpr std::size_t MinimumRegularNodes = 4;
constexpr std::size_t MaximumRegularNodes = 12;
constexpr std::size_t MaximumLocalLinks = 12;
constexpr std::size_t MaximumGroups = 3;
constexpr std::size_t MaximumRoutePointsPerLink = 4;

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

template<typename Id, typename Range>
std::set<Id> CollectIds(const Range& range) {
    std::set<Id> result;
    for (const Id id : range) result.insert(id);
    return result;
}

template<typename Id>
std::string IdText(const Id id) {
    return std::to_string(id.Value());
}

enum class Action {
    LeafProperty,
    MoveNode,
    ResizeNode,
    CollapseNode,
    ValidCompound,
    FailingCompound,
    PolicyDeny,
    StartDeferredCommit,
    StartDeferredAllow,
    StartDeferredDeny,
    StartDeferredRedefer,
    StartDeferredCancel,
    PendingBlocked,
    ResolveCommit,
    ResolveAllow,
    ResolveDeny,
    ResolveRedefer,
    ResolveCancel,
    Undo,
    Redo,
    AddNode,
    DeleteNode,
    Connect,
    DeleteLink,
    Reconnect,
    RouteInsert,
    RouteMove,
    RouteRemove,
    LinkColor,
    LinkRouter,
    LinkLock,
    LinkUnlock,
    GroupAdd,
    GroupRemove,
    GroupDelta,
    GroupMove,
    GroupStyle,
    GroupLock,
    GroupUnlock,
    NodeLock,
    NodeUnlock,
    NodeReadOnly,
    NodeWritable,
    SubgraphToggle,
    ConversionRegister,
    ConversionReplace,
    ConversionUnregister,
};

constexpr std::string_view ActionName(const Action action) {
    switch (action) {
    case Action::LeafProperty: return "leaf-property";
    case Action::MoveNode: return "move-node";
    case Action::ResizeNode: return "resize-node";
    case Action::CollapseNode: return "collapse-node";
    case Action::ValidCompound: return "valid-compound";
    case Action::FailingCompound: return "failing-compound";
    case Action::PolicyDeny: return "policy-deny";
    case Action::StartDeferredCommit: return "defer-commit-prepared";
    case Action::StartDeferredAllow: return "defer-reauthorize-allow";
    case Action::StartDeferredDeny: return "defer-reauthorize-deny";
    case Action::StartDeferredRedefer: return "defer-reauthorize-redefer";
    case Action::StartDeferredCancel: return "defer-cancel";
    case Action::PendingBlocked: return "pending-blocked-transition";
    case Action::ResolveCommit: return "resume-commit-prepared";
    case Action::ResolveAllow: return "resume-reauthorize-allow";
    case Action::ResolveDeny: return "resume-reauthorize-deny";
    case Action::ResolveRedefer: return "resume-reauthorize-redefer";
    case Action::ResolveCancel: return "cancel-pending";
    case Action::Undo: return "undo";
    case Action::Redo: return "redo";
    case Action::AddNode: return "add-node-and-pins";
    case Action::DeleteNode: return "delete-node";
    case Action::Connect: return "connect-pins";
    case Action::DeleteLink: return "delete-link";
    case Action::Reconnect: return "reconnect-link";
    case Action::RouteInsert: return "route-insert";
    case Action::RouteMove: return "route-move";
    case Action::RouteRemove: return "route-remove";
    case Action::LinkColor: return "link-color";
    case Action::LinkRouter: return "link-router";
    case Action::LinkLock: return "link-lock";
    case Action::LinkUnlock: return "link-unlock";
    case Action::GroupAdd: return "group-add";
    case Action::GroupRemove: return "group-remove";
    case Action::GroupDelta: return "group-member-delta";
    case Action::GroupMove: return "group-move";
    case Action::GroupStyle: return "group-style";
    case Action::GroupLock: return "group-lock";
    case Action::GroupUnlock: return "group-unlock";
    case Action::NodeLock: return "node-lock";
    case Action::NodeUnlock: return "node-unlock";
    case Action::NodeReadOnly: return "node-read-only";
    case Action::NodeWritable: return "node-writable";
    case Action::SubgraphToggle: return "subgraph-toggle";
    case Action::ConversionRegister: return "conversion-register";
    case Action::ConversionReplace: return "conversion-replace";
    case Action::ConversionUnregister: return "conversion-unregister";
    }
    return "unknown";
}

constexpr Action CoveragePrefix[] = {
    Action::LeafProperty,
    Action::MoveNode,
    Action::ResizeNode,
    Action::CollapseNode,
    Action::ValidCompound,
    Action::FailingCompound,
    Action::PolicyDeny,
    Action::StartDeferredCommit,
    Action::PendingBlocked,
    Action::ResolveCommit,
    Action::StartDeferredAllow,
    Action::PendingBlocked,
    Action::ResolveAllow,
    Action::StartDeferredDeny,
    Action::PendingBlocked,
    Action::ResolveDeny,
    Action::ResolveAllow,
    Action::StartDeferredRedefer,
    Action::PendingBlocked,
    Action::ResolveRedefer,
    Action::ResolveAllow,
    Action::StartDeferredCancel,
    Action::PendingBlocked,
    Action::ResolveCancel,
    Action::Undo,
    Action::Redo,
    Action::AddNode,
    Action::Connect,
    Action::Reconnect,
    Action::DeleteLink,
    Action::DeleteNode,
    Action::RouteInsert,
    Action::RouteMove,
    Action::RouteRemove,
    Action::GroupDelta,
    Action::GroupMove,
    Action::GroupStyle,
    Action::GroupLock,
    Action::GroupUnlock,
    Action::NodeLock,
    Action::NodeUnlock,
    Action::NodeReadOnly,
    Action::NodeWritable,
    Action::SubgraphToggle,
    Action::Undo,
    Action::Redo,
    Action::Undo,
    Action::LinkColor,
    Action::LinkRouter,
    Action::LinkLock,
    Action::LinkUnlock,
    Action::GroupAdd,
    Action::GroupRemove,
    Action::ConversionRegister,
    Action::ConversionReplace,
    Action::ConversionUnregister,
};

struct Coverage final {
    std::size_t leaf_mutations{0};
    std::size_t valid_compounds{0};
    std::size_t failed_compounds{0};
    std::size_t denied_operations{0};
    std::size_t deferred_operations{0};
    std::size_t pending_blocked_transitions{0};
    std::size_t resume_commits{0};
    std::size_t resume_allows{0};
    std::size_t resume_denials{0};
    std::size_t resume_redeferrals{0};
    std::size_t cancellations{0};
    std::size_t undos{0};
    std::size_t redos{0};
    std::size_t node_additions{0};
    std::size_t node_deletions{0};
    std::size_t connections{0};
    std::size_t link_deletions{0};
    std::size_t reconnects{0};
    std::size_t route_inserts{0};
    std::size_t route_moves{0};
    std::size_t route_removes{0};
    std::size_t group_deltas{0};
    std::size_t group_styles{0};
    std::size_t group_geometry{0};
    std::size_t presentation_mutations{0};
    std::size_t protection_mutations{0};
    std::size_t subgraph_mutations{0};
    std::size_t index_checks{0};
    std::size_t validation_passes{0};
    std::size_t conversion_registrations{0};
    std::size_t conversion_replacements{0};
    std::size_t conversion_unregistrations{0};
};

class StateMachine final {
public:
    StateMachine(const std::uint64_t seed, const std::size_t steps, std::ostream* full_trace = nullptr)
        : m_seed(seed),
          m_steps(steps),
          m_random(seed),
          m_full_trace(full_trace),
          m_commands(CommandStack::Options{
              .history_limit = steps + 64,
              .max_policy_batch_operations = 256,
              .max_replacements = 4,
          }) {
        Setup();
    }

    void Run() {
        VerifyState(true);
        for (m_step = 0; m_step < m_steps; ++m_step) {
            const Action action = ChooseAction();
            Record(std::string{ActionName(action)});
            Perform(action);
            VerifyState(m_step % 113 == 0 || m_step + 1 == m_steps);
        }
        VerifyCoverage();
    }

    [[nodiscard]] const Coverage& GetCoverage() const noexcept { return m_coverage; }

private:
    enum class PendingPlan { Commit, Allow, DenyThenAllow, RedeferThenAllow, Cancel };

    struct PendingState final {
        PendingPlan plan{PendingPlan::Commit};
        DeferredOperationId id;
        std::string baseline;
        bool blocked_checked{false};
        bool denied_once{false};
        bool redeferred_once{false};
    };

    struct LinkModel final {
        LinkStyle style;
        std::vector<RoutePoint> route;
        bool operator==(const LinkModel&) const = default;
    };

    struct GroupModel final {
        GraphId graph;
        GroupGeometry geometry;
        GroupStyle style;
        std::set<NodeId> members;
        GroupProtection protection;
        bool operator==(const GroupModel&) const = default;
    };

    struct PresentationModel final {
        std::map<NodeId, NodePresentation> nodes;
        std::map<LinkId, LinkModel> links;
        std::map<GroupId, GroupModel> groups;
        bool operator==(const PresentationModel&) const = default;
    };

    struct HistorySnapshot final {
        std::string canonical;
        PresentationModel presentation;
    };

    struct HistoryModel final {
        std::string current;
        std::vector<HistorySnapshot> undo;
        std::vector<HistorySnapshot> redo;
    };

    struct RetainedConversionSnapshot final {
        RegistrySnapshot snapshot;
        ConnectionResult::Status status{ConnectionResult::Status::Rejected};
        std::optional<ConversionRecipe> recipe;
    };

    using ModelMutation = std::function<void(PresentationModel&)>;

    template<typename Value>
    [[noreturn]] void Fail(const Value& message) const {
        std::ostringstream output;
        output << "seed=0x" << std::hex << m_seed << std::dec << " step=" << m_step
               << ": " << message << '\n';
        output << "last actions:";
        const std::size_t first = m_step + 1 >= m_trace.size() ? m_step + 1 - m_trace.size() : 0;
        for (std::size_t index = 0; index < m_trace.size(); ++index) {
            output << '\n' << "  [" << first + index << "] " << m_trace[index];
        }
        throw TestFailure(output.str());
    }

    template<typename Value>
    void Require(const bool condition, const Value& message) const {
        if (!condition) Fail(message);
    }

    void Record(std::string action) {
        if (m_full_trace != nullptr) {
            *m_full_trace << '[' << m_step << "] " << action << '\n';
        }
        if (m_trace.size() == TraceLimit) m_trace.pop_front();
        m_trace.push_back(std::move(action));
    }

    [[nodiscard]] std::string CanonicalState() const {
        auto serialized = SerializeGraphDocumentJson(m_document, m_presentation);
        if (!serialized) Fail("canonical serialization failed: " + serialized.error().message);
        return std::move(*serialized);
    }

    [[nodiscard]] PresentationModel CapturePresentationModel() const {
        PresentationModel model;
        for (const auto& [id, state] : m_presentation.Nodes()) model.nodes.emplace(id, state);
        for (const auto& [id, state] : m_presentation.Links()) {
            model.links.emplace(id, LinkModel{state.Style(), state.Route().ToVector()});
        }
        for (const auto& [id, state] : m_presentation.Groups()) {
            model.groups.emplace(id, GroupModel{
                .graph = state.graph,
                .geometry = state.geometry,
                .style = *state.style,
                .members = CollectIds<NodeId>(state.members),
                .protection = state.protection,
            });
        }
        return model;
    }

    [[nodiscard]] static LinkModel& EnsureLink(PresentationModel& model, const LinkId link) {
        return model.links.try_emplace(link).first->second;
    }

    static void NormalizeEmptyLink(PresentationModel& model, const LinkId link) {
        const auto found = model.links.find(link);
        if (found != model.links.end() && found->second.style == LinkStyle{} &&
            found->second.route.empty()) {
            model.links.erase(found);
        }
    }

    void VerifyPresentationModel() const {
        Require(m_presentation.Nodes().size() == m_model.nodes.size(),
            "node presentation count diverged from independent model");
        Require(m_presentation.Links().size() == m_model.links.size(),
            "link presentation count diverged from independent model");
        Require(m_presentation.Groups().size() == m_model.groups.size(),
            "group presentation count diverged from independent model");
        for (const auto& [id, expected] : m_model.nodes) {
            const NodePresentation* actual = m_presentation.FindNode(id);
            Require(actual != nullptr && *actual == expected,
                "node presentation diverged for " + IdText(id));
        }
        for (const auto& [id, expected] : m_model.links) {
            const LinkPresentation* actual = m_presentation.FindLink(id);
            Require(actual != nullptr && actual->Style() == expected.style &&
                        actual->Route().ToVector() == expected.route,
                "link presentation diverged for " + IdText(id));
        }
        for (const auto& [id, expected] : m_model.groups) {
            const GroupPresentation* actual = m_presentation.FindGroup(id);
            Require(actual != nullptr && actual->graph == expected.graph &&
                        actual->geometry == expected.geometry && actual->style &&
                        *actual->style == expected.style &&
                        CollectIds<NodeId>(actual->members) == expected.members &&
                        actual->protection == expected.protection,
                "group presentation diverged for " + IdText(id));
        }
    }

    void SetupExecute(std::unique_ptr<Command> command, const std::string_view description) {
        auto result = m_commands.Execute(
            std::move(command), m_document, m_presentation, m_registry);
        if (!result) Fail(std::string{description} + ": " + result.error().message);
        Require(result->model_changed || result->presentation_changed,
                std::string{description} + " was unexpectedly a no-op");
    }

    void Setup() {
        auto registered = m_registry.RegisterNodeType(NodeTypeDescriptor{
            .type = TypeId{"state.node"},
            .display_name = "State node",
            .category = "Tests",
            .static_pins = {
                PinDescriptor{
                    .key = "input",
                    .label = "Input",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Input,
                },
                PinDescriptor{
                    .key = "output",
                    .label = "Output",
                    .type = TypeId{"number"},
                    .direction = PinDirection::Output,
                    .cardinality = PinCardinality::Multiple,
                },
            },
        });
        Require(registered.has_value(), "state node descriptor registration failed");
        for (const char* converter : {"state.converter-a", "state.converter-b"}) {
            Require(m_registry.RegisterNodeType(NodeTypeDescriptor{
                        .type = TypeId{converter},
                        .display_name = converter,
                        .static_pins = {
                            PinDescriptor{.key = "input", .type = TypeId{"state.source"}},
                            PinDescriptor{
                                .key = "output",
                                .type = TypeId{"state.destination"},
                                .direction = PinDirection::Output,
                            },
                        },
                    }).has_value(),
                "state conversion descriptor registration failed");
        }

        m_root = m_document.RootGraph();
        for (std::size_t index = 0; index < 6; ++index) AddSetupNode(index);

        const auto regular = RegularNodes();
        for (std::size_t index = 0; index < 3; ++index) {
            const PinId output = PinFor(regular[index * 2], PinDirection::Output);
            const PinId input = PinFor(regular[index * 2 + 1], PinDirection::Input);
            const LinkId link = m_document.AllocateLinkId();
            Remember(m_recent_links, m_seen_links, link);
            RememberConnection(output, input);
            SetupExecute(
                std::make_unique<ConnectPinsCommand>(m_root, Link{.id = link, .output = output, .input = input}),
                "setup local link");
            if (index == 0) {
                const RoutePointId point = m_presentation.AllocateRoutePointId();
                Remember(m_recent_route_points, m_seen_route_points, point);
                SetupExecute(
                    std::make_unique<SetLinkPresentationCommand>(
                        link,
                        LinkPresentation{
                            LinkStyle{.router = BezierLinkRouterType(), .color = 0xFF102030U},
                            PersistentRoutePointSequence{{point, {120.0f, 40.0f}}},
                        }),
                    "setup routed link");
            }
        }

        m_group = m_presentation.AllocateGroupId();
        Remember(m_recent_groups, m_seen_groups, m_group);
        SetupExecute(
            std::make_unique<AddGroupCommand>(GroupPresentation{
                .id = m_group,
                .graph = m_root,
                .geometry = GroupGeometry{.position = {-20.0f, -10.0f}, .size = {500.0f, 240.0f}},
                .style = MakeGroupStyle(GroupStyle{.title = "State group"}),
                .members = {regular[0], regular[1], regular[2]},
            }),
            "setup group");

        m_child = m_document.AllocateGraphId();
        m_caller = m_document.AllocateNodeId();
        Remember(m_recent_graphs, m_seen_graphs, m_child);
        Remember(m_recent_nodes, m_seen_nodes, m_caller);
        std::vector<std::unique_ptr<Command>> subgraph_setup;
        subgraph_setup.push_back(std::make_unique<AddGraphCommand>(Graph{
            .id = m_child,
            .display_name = "Reusable child",
        }));
        subgraph_setup.push_back(std::make_unique<AddNodeCommand>(
            m_root,
            NodeCreation{.node = NodeInstance{
                .id = m_caller,
                .type = TypeId{"state.subgraph"},
                .display_name = "Subgraph caller",
            }},
            NodePresentation{.position = {520.0f, 40.0f}}));
        subgraph_setup.push_back(std::make_unique<SetNodeSubgraphCommand>(
            m_root,
            m_caller,
            SubgraphReference{
                .ownership = SubgraphOwnership::Referenced,
                .target = DocumentGraphTarget{m_child},
            }));
        subgraph_setup.push_back(std::make_unique<SetGraphInterfaceCommand>(
            m_child,
            GraphInterface{
                .version = 1,
                .pins = {
                    GraphInterfacePin{
                        .key = "in",
                        .label = "In",
                        .type = TypeId{"number"},
                        .direction = PinDirection::Input,
                    },
                    GraphInterfacePin{
                        .key = "out",
                        .label = "Out",
                        .type = TypeId{"number"},
                        .direction = PinDirection::Output,
                    },
                },
            }));
        SetupExecute(
            std::make_unique<CompoundCommand>("Setup reusable subgraph", std::move(subgraph_setup)),
            "setup reusable subgraph");

        m_owned_child = m_document.AllocateGraphId();
        m_owned_caller = m_document.AllocateNodeId();
        Remember(m_recent_graphs, m_seen_graphs, m_owned_child);
        Remember(m_recent_nodes, m_seen_nodes, m_owned_caller);
        std::vector<std::unique_ptr<Command>> owned_setup;
        owned_setup.push_back(std::make_unique<AddGraphCommand>(Graph{
            .id = m_owned_child,
            .display_name = "Owned child",
            .lifetime = GraphLifetime::Owned,
        }));
        owned_setup.push_back(std::make_unique<AddNodeCommand>(
            m_root,
            NodeCreation{.node = NodeInstance{
                .id = m_owned_caller,
                .type = TypeId{"state.owned-subgraph"},
                .display_name = "Owned subgraph caller",
            }},
            NodePresentation{.position = {520.0f, 180.0f}}));
        owned_setup.push_back(std::make_unique<SetNodeSubgraphCommand>(
            m_root,
            m_owned_caller,
            SubgraphReference{
                .ownership = SubgraphOwnership::Owned,
                .target = DocumentGraphTarget{m_owned_child},
            }));
        SetupExecute(
            std::make_unique<CompoundCommand>("Setup owned subgraph", std::move(owned_setup)),
            "setup owned subgraph");

        m_left = m_document.AllocateGraphId();
        m_right = m_document.AllocateGraphId();
        Remember(m_recent_graphs, m_seen_graphs, m_left);
        Remember(m_recent_graphs, m_seen_graphs, m_right);
        SetupExecute(std::make_unique<AddGraphCommand>(m_left), "setup left intergraph area");
        SetupExecute(std::make_unique<AddGraphCommand>(m_right), "setup right intergraph area");

        const NodeId sender = m_document.AllocateNodeId();
        const PinId sender_pin = m_document.AllocatePinId();
        const NodeId receiver = m_document.AllocateNodeId();
        const PinId receiver_pin = m_document.AllocatePinId();
        Remember(m_recent_nodes, m_seen_nodes, sender);
        Remember(m_recent_nodes, m_seen_nodes, receiver);
        Remember(m_recent_pins, m_seen_pins, sender_pin);
        Remember(m_recent_pins, m_seen_pins, receiver_pin);
        SetupExecute(
            std::make_unique<AddNodeCommand>(
                m_left,
                NodeCreation{
                    .node = NodeInstance{
                        .id = sender,
                        .type = TypeId{"state.intergraph-output"},
                        .role = NodeRole::IntergraphOutput,
                    },
                    .pins = {PinInstance{
                        .id = sender_pin,
                        .node = sender,
                        .key = "channel",
                        .label = "Channel",
                        .type = TypeId{"number"},
                        .direction = PinDirection::Input,
                        .storage = PinStorage::Dynamic,
                    }},
                }),
            "setup intergraph sender");
        SetupExecute(
            std::make_unique<AddNodeCommand>(
                m_right,
                NodeCreation{
                    .node = NodeInstance{
                        .id = receiver,
                        .type = TypeId{"state.intergraph-input"},
                        .role = NodeRole::IntergraphInput,
                    },
                    .pins = {PinInstance{
                        .id = receiver_pin,
                        .node = receiver,
                        .key = "channel",
                        .label = "Channel",
                        .type = TypeId{"number"},
                        .direction = PinDirection::Output,
                        .storage = PinStorage::Dynamic,
                    }},
                }),
            "setup intergraph receiver");
        m_intergraph = m_document.AllocateIntergraphLinkId();
        Remember(m_recent_intergraph_links, m_seen_intergraph_links, m_intergraph);
        SetupExecute(
            std::make_unique<ConnectIntergraphCommand>(IntergraphLink{
                .id = m_intergraph,
                .source = {m_left, sender, sender_pin},
                .destination = {m_right, receiver, receiver_pin},
            }),
            "setup intergraph link");

        m_commands.Clear();
        m_history.current = CanonicalState();
        m_model = CapturePresentationModel();
    }

    void AddSetupNode(const std::size_t index) {
        auto creation = m_registry.Instantiate(m_document, TypeId{"state.node"}, "Node " + std::to_string(index));
        Require(creation.has_value(), "setup node instantiation failed");
        RememberCreation(*creation);
        SetupExecute(
            std::make_unique<AddNodeCommand>(
                m_root,
                std::move(*creation),
                NodePresentation{.position = {static_cast<float>(index * 140), static_cast<float>((index % 2) * 120)}}),
            "setup regular node");
    }

    void RememberCreation(const NodeCreation& creation) {
        Remember(m_recent_nodes, m_seen_nodes, creation.node.id);
        for (const PinInstance& pin : creation.pins) Remember(m_recent_pins, m_seen_pins, pin.id);
    }

    template<typename Id>
    static void Remember(std::deque<Id>& recent, std::set<Id>& seen, const Id id) {
        if (!id || !seen.insert(id).second) return;
        recent.push_back(id);
        if (recent.size() > RecentIdLimit) recent.pop_front();
    }

    void RememberConnection(const PinId output, const PinId input) {
        const auto pair = std::pair{output, input};
        if (!m_seen_connections.insert(pair).second) return;
        m_recent_connections.push_back(pair);
        if (m_recent_connections.size() > RecentIdLimit) m_recent_connections.pop_front();
    }

    [[nodiscard]] std::vector<NodeId> RegularNodes() const {
        std::vector<NodeId> result;
        const Graph* graph = m_document.FindGraph(m_root);
        if (graph == nullptr) return result;
        for (const auto& [id, node] : graph->nodes) {
            if (node.role == NodeRole::Regular && node.type == TypeId{"state.node"}) result.push_back(id);
        }
        return result;
    }

    [[nodiscard]] PinId PinFor(const NodeId node, const PinDirection direction) const {
        const NodeInstance* value = m_document.FindNode(m_root, node);
        if (value == nullptr) return {};
        for (const PinId pin : value->pins) {
            const PinInstance* state = m_document.FindPin(m_root, pin);
            if (state != nullptr && state->direction == direction) return pin;
        }
        return {};
    }

    template<typename Value>
    [[nodiscard]] const Value& Pick(const std::vector<Value>& values) {
        Require(!values.empty(), "attempted to choose from an empty candidate set");
        return values[static_cast<std::size_t>(m_random() % values.size())];
    }

    [[nodiscard]] std::vector<NodeId> WritableNodes() const {
        std::vector<NodeId> result;
        for (const NodeId id : RegularNodes()) {
            const NodeInstance* node = m_document.FindNode(m_root, id);
            if (node != nullptr && !node->read_only) result.push_back(id);
        }
        return result;
    }

    [[nodiscard]] std::vector<NodeId> MovableNodes() const {
        std::vector<NodeId> result;
        for (const NodeId id : RegularNodes()) {
            const NodePresentation* state = m_presentation.FindNode(id);
            if (state != nullptr && !state->locked) result.push_back(id);
        }
        return result;
    }

    [[nodiscard]] std::vector<LinkId> LocalLinks() const {
        std::vector<LinkId> result;
        const Graph* graph = m_document.FindGraph(m_root);
        if (graph == nullptr) return result;
        for (const auto& [id, link] : graph->links) {
            (void)link;
            result.push_back(id);
        }
        return result;
    }

    [[nodiscard]] std::vector<LinkId> MutableLinks(const bool require_presentation = false) const {
        std::vector<LinkId> result;
        const Graph* graph = m_document.FindGraph(m_root);
        if (graph == nullptr) return result;
        for (const auto& [id, link] : graph->links) {
            const LinkPresentation* state = m_presentation.FindLink(id);
            if (!link.read_only && (!require_presentation || state != nullptr) &&
                (state == nullptr || !state->Style().locked)) {
                result.push_back(id);
            }
        }
        return result;
    }

    [[nodiscard]] bool LinkConnectionWritable(const Link& link) const {
        for (const PinId pin_id : {link.output, link.input}) {
            const auto owner = m_document.FindPinOwner(pin_id);
            const PinInstance* pin = owner ? m_document.FindPin(owner->graph, pin_id) : nullptr;
            const NodeInstance* node = owner ? m_document.FindNode(owner->graph, owner->node) : nullptr;
            if (!owner || pin == nullptr || node == nullptr || pin->read_only || node->read_only) return false;
        }
        return true;
    }

    [[nodiscard]] std::vector<GroupId> MutableGroups() const {
        std::vector<GroupId> result;
        for (const auto& [id, group] : m_presentation.Groups()) {
            if (group.graph == m_root && !group.protection.locked) result.push_back(id);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::pair<PinId, PinId>> ConnectionCandidates() const {
        std::vector<PinId> outputs;
        std::vector<PinId> inputs;
        const Graph* graph = m_document.FindGraph(m_root);
        if (graph == nullptr) return {};
        for (const auto& [id, pin] : graph->pins) {
            const NodeInstance* owner = m_document.FindNode(m_root, pin.node);
            if (owner == nullptr || owner->role != NodeRole::Regular || owner->type != TypeId{"state.node"} ||
                owner->read_only || pin.read_only) {
                continue;
            }
            if (pin.direction == PinDirection::Output) outputs.push_back(id);
            if (pin.direction == PinDirection::Input && m_document.IncidentLinks(id).empty()) inputs.push_back(id);
        }
        std::vector<std::pair<PinId, PinId>> result;
        for (const PinId output : outputs) {
            const auto output_owner = m_document.FindPinOwner(output);
            for (const PinId input : inputs) {
                const auto input_owner = m_document.FindPinOwner(input);
                if (output_owner && input_owner && output_owner->node != input_owner->node &&
                    !m_document.FindLinkBetween(output, input)) {
                    result.emplace_back(output, input);
                }
            }
        }
        return result;
    }

    [[nodiscard]] Action ChooseAction() {
        if (m_pending) {
            if (!m_pending->blocked_checked) return Action::PendingBlocked;
            switch (m_pending->plan) {
            case PendingPlan::Commit: return Action::ResolveCommit;
            case PendingPlan::Allow: return Action::ResolveAllow;
            case PendingPlan::DenyThenAllow:
                return m_pending->denied_once ? Action::ResolveAllow : Action::ResolveDeny;
            case PendingPlan::RedeferThenAllow:
                return m_pending->redeferred_once ? Action::ResolveAllow : Action::ResolveRedefer;
            case PendingPlan::Cancel: return Action::ResolveCancel;
            }
        }
        if (m_step < std::size(CoveragePrefix)) return CoveragePrefix[m_step];

        switch (m_random() % 33U) {
        case 0: return Action::LeafProperty;
        case 1: return Action::MoveNode;
        case 2: return Action::ResizeNode;
        case 3: return Action::CollapseNode;
        case 4: return Action::ValidCompound;
        case 5: return Action::FailingCompound;
        case 6: return Action::PolicyDeny;
        case 7: return Action::StartDeferredCommit;
        case 8: return Action::StartDeferredAllow;
        case 9: return Action::StartDeferredDeny;
        case 10: return Action::StartDeferredRedefer;
        case 11: return Action::StartDeferredCancel;
        case 12: return Action::Undo;
        case 13: return Action::Redo;
        case 14: return RegularNodes().size() < MaximumRegularNodes ? Action::AddNode : Action::DeleteNode;
        case 15: return LocalLinks().size() < MaximumLocalLinks ? Action::Connect : Action::DeleteLink;
        case 16: return Action::Reconnect;
        case 17: return Action::RouteInsert;
        case 18: return Action::RouteMove;
        case 19: return Action::RouteRemove;
        case 20: return Action::LinkColor;
        case 21: return Action::LinkRouter;
        case 22: return Action::GroupDelta;
        case 23: return Action::GroupMove;
        case 24: return Action::GroupStyle;
        case 25: return m_presentation.Groups().size() < MaximumGroups ? Action::GroupAdd : Action::GroupRemove;
        case 26: return Action::NodeLock;
        case 27: return Action::NodeReadOnly;
        case 28: return Action::SubgraphToggle;
        case 29: return Action::ConversionRegister;
        case 30: return Action::ConversionReplace;
        case 31: return Action::ConversionUnregister;
        default: return (m_random() & 1U) != 0 ? Action::LinkLock : Action::GroupLock;
        }
    }

    void Perform(const Action action) {
        switch (action) {
        case Action::LeafProperty: LeafProperty(); break;
        case Action::MoveNode: MoveNode(); break;
        case Action::ResizeNode: ResizeNode(); break;
        case Action::CollapseNode: CollapseNode(); break;
        case Action::ValidCompound: ValidCompound(); break;
        case Action::FailingCompound: FailingCompound(); break;
        case Action::PolicyDeny: PolicyDeny(); break;
        case Action::StartDeferredCommit: StartDeferred(PendingPlan::Commit); break;
        case Action::StartDeferredAllow: StartDeferred(PendingPlan::Allow); break;
        case Action::StartDeferredDeny: StartDeferred(PendingPlan::DenyThenAllow); break;
        case Action::StartDeferredRedefer: StartDeferred(PendingPlan::RedeferThenAllow); break;
        case Action::StartDeferredCancel: StartDeferred(PendingPlan::Cancel); break;
        case Action::PendingBlocked: CheckPendingBlocksLaterTransition(); break;
        case Action::ResolveCommit: ResolveCommit(); break;
        case Action::ResolveAllow: ResolveAllow(); break;
        case Action::ResolveDeny: ResolveDeny(); break;
        case Action::ResolveRedefer: ResolveRedefer(); break;
        case Action::ResolveCancel: ResolveCancel(); break;
        case Action::Undo: Undo(); break;
        case Action::Redo: Redo(); break;
        case Action::AddNode: AddNode(); break;
        case Action::DeleteNode: DeleteNode(); break;
        case Action::Connect: Connect(); break;
        case Action::DeleteLink: DeleteLink(); break;
        case Action::Reconnect: Reconnect(); break;
        case Action::RouteInsert: RouteInsert(); break;
        case Action::RouteMove: RouteMove(); break;
        case Action::RouteRemove: RouteRemove(); break;
        case Action::LinkColor: LinkColor(); break;
        case Action::LinkRouter: LinkRouter(); break;
        case Action::LinkLock: LinkLock(); break;
        case Action::LinkUnlock: LinkUnlock(); break;
        case Action::GroupAdd: GroupAdd(); break;
        case Action::GroupRemove: GroupRemove(); break;
        case Action::GroupDelta: GroupDelta(); break;
        case Action::GroupMove: GroupMove(); break;
        case Action::GroupStyle: GroupStyleMutation(); break;
        case Action::GroupLock: GroupLock(); break;
        case Action::GroupUnlock: GroupUnlock(); break;
        case Action::NodeLock: NodeLock(); break;
        case Action::NodeUnlock: NodeUnlock(); break;
        case Action::NodeReadOnly: NodeReadOnly(); break;
        case Action::NodeWritable: NodeWritable(); break;
        case Action::SubgraphToggle: SubgraphToggle(); break;
        case Action::ConversionRegister: ConversionRegister(); break;
        case Action::ConversionReplace: ConversionReplace(); break;
        case Action::ConversionUnregister: ConversionUnregister(); break;
        }
    }

    void AcceptCommitted(const std::string& before,
                         const PresentationModel& before_model,
                         PresentationModel expected_model,
                         const CommandResult& result) {
        const std::string after = CanonicalState();
        if (result.model_changed || result.presentation_changed) {
            Require(after != before, "changed command retained the same canonical state");
            m_history.undo.push_back(HistorySnapshot{before, before_model});
            m_history.redo.clear();
            m_history.current = after;
            m_model = std::move(expected_model);
        } else {
            Require(after == before, "no-op command changed canonical state");
            Require(expected_model == before_model,
                "modeled transition expected a change from a no-op command");
        }
    }

    void ExecuteCommitted(std::unique_ptr<Command> command,
                          const GraphPolicy& policy = {},
                          const ModelMutation& mutate_model = {}) {
        Require(!m_pending, "committed execute requested while model is pending");
        const std::string before = CanonicalState();
        const PresentationModel before_model = m_model;
        PresentationModel expected_model = before_model;
        if (mutate_model) mutate_model(expected_model);
        auto result = m_commands.Execute(
            std::move(command), m_document, m_presentation, m_registry, policy);
        if (!result) Fail("command failed unexpectedly: " + result.error().message);
        Require(!result->deferred, "committed execute unexpectedly deferred");
        AcceptCommitted(before, before_model, std::move(expected_model), *result);
    }

    void ExecuteModeled(std::unique_ptr<Command> command, ModelMutation mutate_model) {
        ExecuteCommitted(std::move(command), {}, mutate_model);
    }

    void ExecuteRejected(std::unique_ptr<Command> command, const GraphPolicy& policy = {}) {
        const std::string before = CanonicalState();
        const bool can_undo = m_commands.CanUndo();
        const bool can_redo = m_commands.CanRedo();
        auto result = m_commands.Execute(
            std::move(command), m_document, m_presentation, m_registry, policy);
        Require(!result, "intentionally failing command unexpectedly succeeded");
        Require(CanonicalState() == before, "failed command mutated canonical committed state");
        Require(m_commands.CanUndo() == can_undo && m_commands.CanRedo() == can_redo,
                "failed command changed history capabilities");
    }

    void LeafProperty() {
        const auto candidates = WritableNodes();
        if (candidates.empty()) return NodeWritable();
        const NodeId node = Pick(candidates);
        ExecuteCommitted(std::make_unique<SetNodePropertyCommand>(
            m_root,
            node,
            "leaf",
            PropertyValue{static_cast<std::int64_t>(++m_mutation_serial)}));
        ++m_coverage.leaf_mutations;
    }

    void MoveNode() {
        const auto candidates = MovableNodes();
        if (candidates.empty()) return NodeUnlock();
        const NodeId node = Pick(candidates);
        const Vec2 before = m_presentation.FindNode(node)->position;
        const Vec2 after{before.x + static_cast<float>(1 + m_random() % 7U),
                         before.y - static_cast<float>(1 + m_random() % 5U)};
        ExecuteModeled(std::make_unique<MoveNodesCommand>(
                           m_root,
                           MoveNodesCommand::Positions{{node, before}},
                           MoveNodesCommand::Positions{{node, after}}),
            [node, after](PresentationModel& model) { model.nodes.at(node).position = after; });
        ++m_coverage.presentation_mutations;
    }

    void ResizeNode() {
        const auto candidates = MovableNodes();
        if (candidates.empty()) return NodeUnlock();
        const NodeId node = Pick(candidates);
        const Vec2 before = m_presentation.FindNode(node)->size;
        const Vec2 after{before.x + 3.0f, before.y + 2.0f};
        ExecuteModeled(std::make_unique<ResizeNodeCommand>(node, after),
            [node, after](PresentationModel& model) { model.nodes.at(node).size = after; });
        ++m_coverage.presentation_mutations;
    }

    void CollapseNode() {
        const auto candidates = MovableNodes();
        if (candidates.empty()) return NodeUnlock();
        const NodeId node = Pick(candidates);
        const bool collapsed = !m_presentation.FindNode(node)->collapsed;
        ExecuteModeled(std::make_unique<SetNodeCollapsedCommand>(node, collapsed),
            [node, collapsed](PresentationModel& model) { model.nodes.at(node).collapsed = collapsed; });
        ++m_coverage.presentation_mutations;
    }

    void ValidCompound() {
        const auto candidates = WritableNodes();
        if (candidates.empty()) return NodeWritable();
        const NodeId node = Pick(candidates);
        std::vector<std::unique_ptr<Command>> children;
        children.push_back(std::make_unique<SetNodePropertyCommand>(
            m_root, node, "compound-a", PropertyValue{static_cast<std::int64_t>(++m_mutation_serial)}));
        children.push_back(std::make_unique<SetNodePropertyCommand>(
            m_root, node, "compound-b", PropertyValue{static_cast<std::int64_t>(++m_mutation_serial)}));
        ExecuteCommitted(std::make_unique<CompoundCommand>("State machine compound", std::move(children)));
        ++m_coverage.valid_compounds;
    }

    void FailingCompound() {
        const auto candidates = WritableNodes();
        if (candidates.empty()) return NodeWritable();
        const NodeId node = Pick(candidates);
        std::vector<std::unique_ptr<Command>> children;
        children.push_back(std::make_unique<SetNodePropertyCommand>(
            m_root, node, "must-rollback", PropertyValue{static_cast<std::int64_t>(++m_mutation_serial)}));
        children.push_back(std::make_unique<SetNodePropertyCommand>(
            m_root,
            NodeId{std::numeric_limits<std::uint64_t>::max()},
            "missing",
            PropertyValue{true}));
        ExecuteRejected(std::make_unique<CompoundCommand>("Intentional rollback", std::move(children)));
        ++m_coverage.failed_compounds;
    }

    void PolicyDeny() {
        const auto candidates = WritableNodes();
        if (candidates.empty()) return NodeWritable();
        GraphPolicy deny;
        deny.evaluate_batch = [](const BatchPolicyContext&, std::span<const OperationIntent>)
            -> BatchPolicyDecision { return DenyBatch{"state machine denial"}; };
        ExecuteRejected(
            std::make_unique<SetNodePropertyCommand>(
                m_root,
                Pick(candidates),
                "denied",
                PropertyValue{static_cast<std::int64_t>(++m_mutation_serial)}),
            deny);
        ++m_coverage.denied_operations;
    }

    void StartDeferred(const PendingPlan plan) {
        Require(!m_pending, "attempted to start a second pending operation");
        const auto candidates = WritableNodes();
        if (candidates.empty()) return NodeWritable();
        const std::string before = CanonicalState();
        GraphPolicy defer;
        defer.evaluate_batch = [ticket = m_mutation_serial + 1](
                                   const BatchPolicyContext&,
                                   std::span<const OperationIntent>) -> BatchPolicyDecision {
            return DeferBatch{ticket};
        };
        auto result = m_commands.Execute(
            std::make_unique<SetNodePropertyCommand>(
                m_root,
                Pick(candidates),
                "pending",
                PropertyValue{static_cast<std::int64_t>(++m_mutation_serial)}),
            m_document,
            m_presentation,
            m_registry,
            defer);
        if (!result) Fail("deferred execute failed: " + result.error().message);
        Require(result->deferred.has_value() && m_commands.HasPending(),
                "deferred execute did not retain pending state");
        Require(CanonicalState() == before, "deferred execute exposed staged state as committed");
        m_pending = PendingState{.plan = plan, .id = result->deferred->id, .baseline = before};
        ++m_coverage.deferred_operations;
    }

    void CheckPendingBlocksLaterTransition() {
        Require(m_pending.has_value(), "pending-block check has no pending operation");
        const DeferredOperationId id = m_pending->id;
        auto execute = m_commands.Execute(
            std::make_unique<SetNodePropertyCommand>(
                m_root, RegularNodes().front(), "blocked", PropertyValue{true}),
            m_document,
            m_presentation,
            m_registry);
        auto undo = m_commands.Undo(m_document, m_presentation, m_registry);
        auto redo = m_commands.Redo(m_document, m_presentation, m_registry);
        Require(!execute && execute.error().code == ErrorCode::OperationPending &&
                    !undo && undo.error().code == ErrorCode::OperationPending &&
                    !redo && redo.error().code == ErrorCode::OperationPending,
                "pending state did not block a later execute/undo/redo transition");
        Require(m_commands.HasPending() && m_commands.PendingOperation() != nullptr &&
                    m_commands.PendingOperation()->id == id && CanonicalState() == m_pending->baseline,
                "blocked transition consumed pending work or changed committed state");
        m_pending->blocked_checked = true;
        ++m_coverage.pending_blocked_transitions;
    }

    void CommitPendingResult(Result<CommandResult> result, const std::string_view operation) {
        Require(m_pending.has_value(), "pending completion has no model state");
        if (!result) Fail(std::string{operation} + " failed: " + result.error().message);
        Require(!result->deferred && !m_commands.HasPending(),
                std::string{operation} + " did not consume pending state");
        const std::string before = m_pending->baseline;
        m_pending.reset();
        AcceptCommitted(before, m_model, m_model, *result);
    }

    void ResolveCommit() {
        Require(m_pending && m_pending->plan == PendingPlan::Commit, "wrong pending plan for CommitPrepared");
        const DeferredOperationId id = m_pending->id;
        CommitPendingResult(
            m_commands.Resume(id, m_document, m_presentation, ResumeMode::CommitPrepared),
            "CommitPrepared resume");
        ++m_coverage.resume_commits;
    }

    void ResolveAllow() {
        Require(m_pending.has_value(), "allow resume has no pending operation");
        GraphPolicy allow;
        allow.evaluate_batch = [](const BatchPolicyContext& context, std::span<const OperationIntent>)
            -> BatchPolicyDecision {
            return context.pass == PolicyEvaluationPass::Resume
                ? BatchPolicyDecision{AllowBatch{}}
                : BatchPolicyDecision{DenyBatch{"wrong policy pass"}};
        };
        const DeferredOperationId id = m_pending->id;
        CommitPendingResult(
            m_commands.Resume(id, m_document, m_presentation, ResumeMode::Reauthorize, allow),
            "allowed reauthorization");
        ++m_coverage.resume_allows;
    }

    void ResolveDeny() {
        Require(m_pending && m_pending->plan == PendingPlan::DenyThenAllow,
                "wrong pending plan for denied reauthorization");
        GraphPolicy deny;
        deny.evaluate_batch = [](const BatchPolicyContext& context, std::span<const OperationIntent>)
            -> BatchPolicyDecision {
            return context.pass == PolicyEvaluationPass::Resume
                ? BatchPolicyDecision{DenyBatch{"denied on resume"}}
                : BatchPolicyDecision{AllowBatch{}};
        };
        auto result = m_commands.Resume(
            m_pending->id, m_document, m_presentation, ResumeMode::Reauthorize, deny);
        Require(!result && result.error().code == ErrorCode::PolicyRejected && m_commands.HasPending() &&
                    CanonicalState() == m_pending->baseline,
                "denied reauthorization mutated state or discarded pending work");
        m_pending->denied_once = true;
        ++m_coverage.resume_denials;
    }

    void ResolveRedefer() {
        Require(m_pending && m_pending->plan == PendingPlan::RedeferThenAllow,
                "wrong pending plan for repeated deferral");
        GraphPolicy redefer;
        redefer.evaluate_batch = [](const BatchPolicyContext& context, std::span<const OperationIntent>)
            -> BatchPolicyDecision {
            return context.pass == PolicyEvaluationPass::Resume
                ? BatchPolicyDecision{DeferBatch{std::uint64_t{0xD3F3U}}}
                : BatchPolicyDecision{AllowBatch{}};
        };
        auto result = m_commands.Resume(
            m_pending->id, m_document, m_presentation, ResumeMode::Reauthorize, redefer);
        Require(result && result->deferred && m_commands.HasPending() &&
                    CanonicalState() == m_pending->baseline,
                "re-deferred resume committed or discarded pending state");
        m_pending->id = result->deferred->id;
        m_pending->redeferred_once = true;
        ++m_coverage.resume_redeferrals;
    }

    void ResolveCancel() {
        Require(m_pending && m_pending->plan == PendingPlan::Cancel, "wrong pending plan for cancellation");
        const std::string before = m_pending->baseline;
        auto result = m_commands.Cancel(m_pending->id);
        if (!result) Fail("pending cancellation failed: " + result.error().message);
        Require(!m_commands.HasPending() && CanonicalState() == before,
                "cancellation changed committed state or retained pending work");
        m_pending.reset();
        ++m_coverage.cancellations;
    }

    void Undo() {
        if (m_history.undo.empty()) return LeafProperty();
        const HistorySnapshot before{m_history.current, m_model};
        const HistorySnapshot expected = m_history.undo.back();
        auto result = m_commands.Undo(m_document, m_presentation, m_registry);
        if (!result) Fail("undo failed: " + result.error().message);
        const std::string after = CanonicalState();
        Require(after == expected.canonical, "undo did not restore the canonical history snapshot");
        m_history.undo.pop_back();
        m_history.redo.push_back(before);
        m_history.current = after;
        m_model = expected.presentation;
        ++m_coverage.undos;
    }

    void Redo() {
        if (m_history.redo.empty()) return LeafProperty();
        const HistorySnapshot before{m_history.current, m_model};
        const HistorySnapshot expected = m_history.redo.back();
        auto result = m_commands.Redo(m_document, m_presentation, m_registry);
        if (!result) Fail("redo failed: " + result.error().message);
        const std::string after = CanonicalState();
        Require(after == expected.canonical, "redo did not restore the canonical history snapshot");
        m_history.redo.pop_back();
        m_history.undo.push_back(before);
        m_history.current = after;
        m_model = expected.presentation;
        ++m_coverage.redos;
    }

    void AddNode() {
        if (RegularNodes().size() >= MaximumRegularNodes) return DeleteNode();
        auto creation = m_registry.Instantiate(
            m_document, TypeId{"state.node"}, "Random " + std::to_string(++m_mutation_serial));
        if (!creation) Fail("random node instantiation failed: " + creation.error().message);
        RememberCreation(*creation);
        const NodeId id = creation->node.id;
        const NodePresentation node_presentation{.position = {
            static_cast<float>(m_random() % 600U),
            static_cast<float>(m_random() % 400U),
        }};
        ExecuteModeled(std::make_unique<AddNodeCommand>(
                           m_root, std::move(*creation), node_presentation),
            [id, node_presentation](PresentationModel& model) {
                model.nodes.emplace(id, node_presentation);
            });
        Require(m_document.FindNode(m_root, id) != nullptr, "added node is not visible");
        ++m_coverage.node_additions;
    }

    [[nodiscard]] bool NodeCanBeDeleted(const NodeId id) const {
        const NodeInstance* node = m_document.FindNode(m_root, id);
        const NodePresentation* state = m_presentation.FindNode(id);
        if (node == nullptr || state == nullptr || node->read_only || state->locked) return false;
        for (const GroupId group : m_presentation.GroupsForNode(id)) {
            const GroupPresentation* value = m_presentation.FindGroup(group);
            if (value != nullptr && value->protection.locked) return false;
        }
        for (const PinId pin : node->pins) {
            for (const LinkId link : m_document.IncidentLinks(pin)) {
                const Link* semantic = m_document.FindLink(m_root, link);
                const LinkPresentation* presentation = m_presentation.FindLink(link);
                if ((semantic != nullptr && semantic->read_only) ||
                    (semantic != nullptr && !LinkConnectionWritable(*semantic)) ||
                    (presentation != nullptr && presentation->Style().locked)) return false;
            }
        }
        return true;
    }

    void DeleteNode() {
        const auto all = RegularNodes();
        if (all.size() <= MinimumRegularNodes) return AddNode();
        std::vector<NodeId> candidates;
        for (const NodeId id : all) {
            if (NodeCanBeDeleted(id)) candidates.push_back(id);
        }
        if (candidates.empty()) return LeafProperty();
        const NodeId id = Pick(candidates);
        const NodeInstance before = *m_document.FindNode(m_root, id);
        std::set<LinkId> removed_links;
        for (const PinId pin : before.pins) Remember(m_recent_pins, m_seen_pins, pin);
        for (const PinId pin : before.pins) {
            const auto incident = m_document.IncidentLinks(pin);
            removed_links.insert(incident.begin(), incident.end());
        }
        ExecuteModeled(std::make_unique<DeleteElementsCommand>(m_root, std::vector<NodeId>{id}),
            [id, removed_links](PresentationModel& model) {
                model.nodes.erase(id);
                for (const LinkId link : removed_links) model.links.erase(link);
                for (auto& [group, state] : model.groups) {
                    (void)group;
                    state.members.erase(id);
                }
            });
        Require(m_document.FindNodeGraph(id) == GraphId{}, "deleted node retained an owner index");
        ++m_coverage.node_deletions;
    }

    void Connect() {
        if (LocalLinks().size() >= MaximumLocalLinks) return DeleteLink();
        const auto candidates = ConnectionCandidates();
        if (candidates.empty()) return AddNode();
        const auto [output, input] = Pick(candidates);
        const LinkId link = m_document.AllocateLinkId();
        Remember(m_recent_links, m_seen_links, link);
        RememberConnection(output, input);
        ExecuteCommitted(std::make_unique<ConnectPinsCommand>(
            m_root, Link{.id = link, .output = output, .input = input}));
        ++m_coverage.connections;
    }

    void DeleteLink() {
        std::vector<LinkId> candidates;
        for (const LinkId link : MutableLinks()) {
            if (LinkConnectionWritable(*m_document.FindLink(m_root, link))) candidates.push_back(link);
        }
        if (candidates.empty()) return LeafProperty();
        const LinkId link = Pick(candidates);
        const Link value = *m_document.FindLink(m_root, link);
        RememberConnection(value.output, value.input);
        ExecuteModeled(std::make_unique<DeleteElementsCommand>(
                           m_root, std::vector<NodeId>{}, std::vector<LinkId>{link}),
            [link](PresentationModel& model) { model.links.erase(link); });
        ++m_coverage.link_deletions;
    }

    void Reconnect() {
        const auto links = MutableLinks();
        const Graph* graph = m_document.FindGraph(m_root);
        if (links.empty() || graph == nullptr) return Connect();
        struct Candidate final { LinkId link; PinId output; PinId input; };
        std::vector<Candidate> candidates;
        for (const LinkId link_id : links) {
            const Link& link = graph->links.at(link_id);
            const auto output_owner = m_document.FindPinOwner(link.output);
            const auto input_owner = m_document.FindPinOwner(link.input);
            const PinInstance* output_pin = m_document.FindPin(m_root, link.output);
            const PinInstance* input_pin = m_document.FindPin(m_root, link.input);
            const NodeInstance* output_node = output_owner
                ? m_document.FindNode(m_root, output_owner->node)
                : nullptr;
            const NodeInstance* input_node = input_owner
                ? m_document.FindNode(m_root, input_owner->node)
                : nullptr;
            if (!output_owner || !input_owner || output_pin == nullptr || input_pin == nullptr ||
                output_pin->read_only || input_pin->read_only || output_node == nullptr ||
                input_node == nullptr || output_node->read_only || input_node->read_only) continue;
            for (const auto& [pin_id, pin] : graph->pins) {
                const NodeInstance* owner = m_document.FindNode(m_root, pin.node);
                if (pin.direction == PinDirection::Input && pin_id != link.input &&
                    m_document.IncidentLinks(pin_id).empty() && owner != nullptr &&
                    owner->role == NodeRole::Regular && owner->type == TypeId{"state.node"} &&
                    !owner->read_only && !pin.read_only && output_owner->node != pin.node) {
                    candidates.push_back({link_id, link.output, pin_id});
                }
            }
        }
        if (candidates.empty()) return Connect();
        const Candidate candidate = Pick(candidates);
        const Link before = *m_document.FindLink(m_root, candidate.link);
        RememberConnection(before.output, before.input);
        RememberConnection(candidate.output, candidate.input);
        ExecuteCommitted(std::make_unique<ReconnectLinkCommand>(
            m_root, candidate.link, candidate.output, candidate.input, true));
        ++m_coverage.reconnects;
    }

    void RouteInsert() {
        std::vector<LinkId> candidates;
        for (const LinkId link : MutableLinks()) {
            const LinkPresentation* state = m_presentation.FindLink(link);
            if (state == nullptr || state->Route().size() < MaximumRoutePointsPerLink) candidates.push_back(link);
        }
        if (candidates.empty()) return LeafProperty();
        const LinkId link = Pick(candidates);
        const LinkPresentation* state = m_presentation.FindLink(link);
        const std::size_t size = state != nullptr ? state->Route().size() : 0;
        const RoutePointId point = m_presentation.AllocateRoutePointId();
        Remember(m_recent_route_points, m_seen_route_points, point);
        const RoutePoint value{point, {
            static_cast<float>(m_random() % 500U),
            static_cast<float>(m_random() % 300U),
        }};
        const std::size_t index = static_cast<std::size_t>(m_random() % (size + 1));
        ExecuteModeled(std::make_unique<InsertRoutePointCommand>(link, value, index),
            [link, value, index](PresentationModel& model) {
                auto& route = EnsureLink(model, link).route;
                route.insert(route.begin() + static_cast<std::ptrdiff_t>(index), value);
            });
        ++m_coverage.route_inserts;
    }

    void RouteMove() {
        std::vector<std::pair<LinkId, RoutePointId>> candidates;
        for (const LinkId link : MutableLinks(true)) {
            for (const RoutePoint& point : m_presentation.FindLink(link)->Route()) {
                candidates.emplace_back(link, point.id);
            }
        }
        if (candidates.empty()) return RouteInsert();
        const auto [link, point] = Pick(candidates);
        const Vec2 before = m_presentation.FindRoutePoint(link, point)->position;
        const Vec2 after{before.x + 2.0f, before.y + 3.0f};
        ExecuteModeled(std::make_unique<MoveRoutePointCommand>(link, point, after),
            [link, point, after](PresentationModel& model) {
                auto& route = model.links.at(link).route;
                std::ranges::find(route, point, &RoutePoint::id)->position = after;
            });
        ++m_coverage.route_moves;
    }

    void RouteRemove() {
        std::vector<std::pair<LinkId, RoutePointId>> candidates;
        for (const LinkId link : MutableLinks(true)) {
            for (const RoutePoint& point : m_presentation.FindLink(link)->Route()) {
                candidates.emplace_back(link, point.id);
            }
        }
        if (candidates.empty()) return LeafProperty();
        const auto [link, point] = Pick(candidates);
        ExecuteModeled(std::make_unique<RemoveRoutePointsCommand>(
                           std::vector<RoutePointRef>{{link, point}}),
            [link, point](PresentationModel& model) {
                std::erase_if(model.links.at(link).route,
                    [point](const RoutePoint& value) { return value.id == point; });
                NormalizeEmptyLink(model, link);
            });
        ++m_coverage.route_removes;
    }

    void LinkColor() {
        const auto candidates = MutableLinks();
        if (candidates.empty()) return Connect();
        const LinkId link = Pick(candidates);
        const auto current = m_presentation.FindLink(link) != nullptr
            ? m_presentation.FindLink(link)->Style().color
            : std::optional<std::uint32_t>{};
        const std::uint32_t color = 0xFF000000U | static_cast<std::uint32_t>(++m_mutation_serial & 0x00FFFFFFU);
        const std::optional<std::uint32_t> value = current == color
            ? std::optional<std::uint32_t>{color ^ 0x00010101U}
            : std::optional<std::uint32_t>{color};
        ExecuteModeled(std::make_unique<SetLinkColorCommand>(link, value),
            [link, value](PresentationModel& model) { EnsureLink(model, link).style.color = value; });
        ++m_coverage.presentation_mutations;
    }

    void LinkRouter() {
        const auto candidates = MutableLinks();
        if (candidates.empty()) return Connect();
        const LinkId link = Pick(candidates);
        const TypeId current = m_presentation.FindLink(link) != nullptr
            ? m_presentation.FindLink(link)->Style().router
            : TypeId{};
        const TypeId value = current == OrthogonalLinkRouterType()
            ? BezierLinkRouterType()
            : OrthogonalLinkRouterType();
        ExecuteModeled(std::make_unique<SetLinkRouterCommand>(link, value),
            [link, value](PresentationModel& model) { EnsureLink(model, link).style.router = value; });
        ++m_coverage.presentation_mutations;
    }

    void LinkLock() {
        std::vector<LinkId> candidates;
        for (const LinkId link : LocalLinks()) {
            const LinkPresentation* state = m_presentation.FindLink(link);
            if (state != nullptr && !state->Style().locked) candidates.push_back(link);
        }
        if (candidates.empty()) return LinkColor();
        const LinkId link = Pick(candidates);
        ExecuteModeled(std::make_unique<SetLinkLockedCommand>(link, true),
            [link](PresentationModel& model) { model.links.at(link).style.locked = true; });
        ++m_coverage.protection_mutations;
    }

    void LinkUnlock() {
        std::vector<LinkId> candidates;
        for (const LinkId link : LocalLinks()) {
            const LinkPresentation* state = m_presentation.FindLink(link);
            if (state != nullptr && state->Style().locked) candidates.push_back(link);
        }
        if (candidates.empty()) return LinkLock();
        const LinkId link = Pick(candidates);
        ExecuteModeled(std::make_unique<SetLinkLockedCommand>(link, false),
            [link](PresentationModel& model) {
                model.links.at(link).style.locked = false;
                NormalizeEmptyLink(model, link);
            });
        ++m_coverage.protection_mutations;
    }

    void GroupAdd() {
        if (m_presentation.Groups().size() >= MaximumGroups) {
            if (MutableGroups().empty()) return GroupUnlock();
            return GroupRemove();
        }
        const GroupId group = m_presentation.AllocateGroupId();
        Remember(m_recent_groups, m_seen_groups, group);
        const auto regular = RegularNodes();
        GroupMemberSet members;
        if (!regular.empty()) members.Insert(Pick(regular));
        GroupPresentation value{
            .id = group,
            .graph = m_root,
            .geometry = GroupGeometry{
                .position = {static_cast<float>(m_random() % 200U), static_cast<float>(m_random() % 200U)},
                .size = {240.0f, 140.0f},
            },
            .style = MakeGroupStyle(GroupStyle{.title = "Group " + IdText(group)}),
            .members = std::move(members),
        };
        const GroupModel expected{
            .graph = value.graph,
            .geometry = value.geometry,
            .style = *value.style,
            .members = CollectIds<NodeId>(value.members),
            .protection = value.protection,
        };
        ExecuteModeled(std::make_unique<AddGroupCommand>(std::move(value)),
            [group, expected](PresentationModel& model) { model.groups.emplace(group, expected); });
        ++m_coverage.group_geometry;
    }

    void GroupRemove() {
        const auto candidates = MutableGroups();
        if (candidates.empty()) return GroupUnlock();
        if (m_presentation.Groups().size() <= 1) return GroupAdd();
        const GroupId group = Pick(candidates);
        ExecuteModeled(std::make_unique<RemoveGroupCommand>(group),
            [group](PresentationModel& model) { model.groups.erase(group); });
        ++m_coverage.group_geometry;
    }

    void GroupDelta() {
        const auto groups = MutableGroups();
        const auto nodes = RegularNodes();
        if (groups.empty() || nodes.empty()) return GroupAdd();
        const GroupId group = Pick(groups);
        const NodeId node = Pick(nodes);
        const bool contains = m_presentation.FindGroup(group)->members.contains(node);
        ExecuteModeled(std::make_unique<ChangeGroupMembersCommand>(
                           group,
                           contains ? std::vector<NodeId>{} : std::vector<NodeId>{node},
                           contains ? std::vector<NodeId>{node} : std::vector<NodeId>{}),
            [group, node, contains](PresentationModel& model) {
                if (contains) model.groups.at(group).members.erase(node);
                else model.groups.at(group).members.insert(node);
            });
        ++m_coverage.group_deltas;
    }

    void GroupMove() {
        const auto groups = MutableGroups();
        if (groups.empty()) return GroupUnlock();
        const GroupId group = Pick(groups);
        const Vec2 before = m_presentation.FindGroup(group)->geometry.position;
        const Vec2 after{before.x + 4.0f, before.y - 3.0f};
        ExecuteModeled(std::make_unique<MoveGroupCommand>(group, after),
            [group, after](PresentationModel& model) { model.groups.at(group).geometry.position = after; });
        ++m_coverage.group_geometry;
    }

    void GroupStyleMutation() {
        const auto groups = MutableGroups();
        if (groups.empty()) return GroupUnlock();
        const GroupId group = Pick(groups);
        const GroupStyle& before = *m_presentation.FindGroup(group)->style;
        const GroupStyle after{
            .title = "Style " + std::to_string(++m_mutation_serial),
            .body = "state-machine",
            .color = before.color ^ 0x00010101U,
            .kind = before.kind == GroupKind::Group ? GroupKind::Comment : GroupKind::Group,
        };
        ExecuteModeled(std::make_unique<SetGroupStyleCommand>(group, after),
            [group, after](PresentationModel& model) { model.groups.at(group).style = after; });
        ++m_coverage.group_styles;
    }

    void GroupLock() {
        const auto groups = MutableGroups();
        if (groups.empty()) return GroupUnlock();
        const GroupId group = Pick(groups);
        ExecuteModeled(std::make_unique<SetGroupLockedCommand>(group, true),
            [group](PresentationModel& model) { model.groups.at(group).protection.locked = true; });
        ++m_coverage.protection_mutations;
    }

    void GroupUnlock() {
        std::vector<GroupId> groups;
        for (const auto& [id, group] : m_presentation.Groups()) {
            if (group.graph == m_root && group.protection.locked) groups.push_back(id);
        }
        if (groups.empty()) return GroupLock();
        const GroupId group = Pick(groups);
        ExecuteModeled(std::make_unique<SetGroupLockedCommand>(group, false),
            [group](PresentationModel& model) { model.groups.at(group).protection.locked = false; });
        ++m_coverage.protection_mutations;
    }

    void NodeLock() {
        std::vector<NodeId> candidates;
        for (const NodeId node : RegularNodes()) {
            const NodePresentation* state = m_presentation.FindNode(node);
            if (state != nullptr && !state->locked) candidates.push_back(node);
        }
        if (candidates.empty()) return NodeUnlock();
        const NodeId node = Pick(candidates);
        ExecuteModeled(std::make_unique<SetNodeLockedCommand>(node, true),
            [node](PresentationModel& model) { model.nodes.at(node).locked = true; });
        ++m_coverage.protection_mutations;
    }

    void NodeUnlock() {
        std::vector<NodeId> candidates;
        for (const NodeId node : RegularNodes()) {
            const NodePresentation* state = m_presentation.FindNode(node);
            if (state != nullptr && state->locked) candidates.push_back(node);
        }
        if (candidates.empty()) return NodeLock();
        const NodeId node = Pick(candidates);
        ExecuteModeled(std::make_unique<SetNodeLockedCommand>(node, false),
            [node](PresentationModel& model) { model.nodes.at(node).locked = false; });
        ++m_coverage.protection_mutations;
    }

    void NodeReadOnly() {
        std::vector<NodeId> candidates;
        for (const NodeId node : RegularNodes()) {
            if (!m_document.FindNode(m_root, node)->read_only) candidates.push_back(node);
        }
        if (candidates.empty()) return NodeWritable();
        ExecuteCommitted(std::make_unique<SetNodeReadOnlyCommand>(m_root, Pick(candidates), true));
        ++m_coverage.protection_mutations;
    }

    void NodeWritable() {
        std::vector<NodeId> candidates;
        for (const NodeId node : RegularNodes()) {
            if (m_document.FindNode(m_root, node)->read_only) candidates.push_back(node);
        }
        if (candidates.empty()) return LeafProperty();
        ExecuteCommitted(std::make_unique<SetNodeReadOnlyCommand>(m_root, Pick(candidates), false));
        ++m_coverage.protection_mutations;
    }

    void SubgraphToggle() {
        const NodeInstance* caller = m_document.FindNode(m_root, m_caller);
        Require(caller != nullptr, "subgraph caller disappeared from bounded fixture");
        ExecuteCommitted(std::make_unique<SetNodeSubgraphCommand>(
            m_root,
            m_caller,
            caller->subgraph
                ? std::optional<SubgraphReference>{}
                : std::optional<SubgraphReference>{SubgraphReference{
                      .ownership = SubgraphOwnership::Referenced,
                      .target = DocumentGraphTarget{m_child},
                  }}));
        ++m_coverage.subgraph_mutations;
    }

    [[nodiscard]] ConversionDescriptor Conversion(const bool alternate) const {
        return ConversionDescriptor{
            .key = ConversionKey{
                TypeId{"state.source"},
                TypeId{"state.destination"},
                PinKind::Data,
            },
            .node_type = TypeId{alternate ? "state.converter-b" : "state.converter-a"},
            .input_pin = "input",
            .output_pin = "output",
        };
    }

    void RetainConversionSnapshot() {
        RegistrySnapshot snapshot = m_registry.Snapshot();
        const auto result = snapshot.Check(
            TypeId{"state.source"}, TypeId{"state.destination"}, PinKind::Data);
        m_conversion_snapshots.push_back(RetainedConversionSnapshot{
            .snapshot = std::move(snapshot),
            .status = result.status,
            .recipe = result.recipe,
        });
        if (m_conversion_snapshots.size() > 16) m_conversion_snapshots.pop_front();
    }

    void ConversionRegister() {
        if (m_conversion_registration) {
            const std::uint64_t revision = m_registry.ConversionRevision();
            auto duplicate = m_registry.RegisterConversion(Conversion(m_conversion_alternate));
            Require(!duplicate && duplicate.error().code == ErrorCode::DuplicateId &&
                        m_registry.ConversionRevision() == revision,
                "duplicate conversion registration changed the live generation");
            return;
        }
        RetainConversionSnapshot();
        auto registered = m_registry.RegisterConversion(Conversion(false));
        Require(registered.has_value(), "conversion registration failed");
        m_conversion_registration = *registered;
        m_conversion_alternate = false;
        ++m_conversion_revision;
        ++m_coverage.conversion_registrations;
    }

    void ConversionReplace() {
        if (!m_conversion_registration) return ConversionRegister();
        RetainConversionSnapshot();
        const auto before = m_registry.Check(
            TypeId{"state.source"}, TypeId{"state.destination"}, PinKind::Data);
        const bool replacement = !m_conversion_alternate;
        auto replaced = m_registry.ReplaceConversion(*m_conversion_registration, Conversion(replacement));
        Require(replaced.has_value(), "conversion replacement failed");
        const auto after = m_registry.Check(
            TypeId{"state.source"}, TypeId{"state.destination"}, PinKind::Data);
        Require(before.recipe && after.recipe && *before.recipe != *after.recipe &&
                    after.recipe->Registration() == *m_conversion_registration,
            "conversion replacement did not preserve token and replace recipe identity");
        m_conversion_alternate = replacement;
        ++m_conversion_revision;
        ++m_coverage.conversion_replacements;
    }

    void ConversionUnregister() {
        if (!m_conversion_registration) return ConversionRegister();
        RetainConversionSnapshot();
        const ConversionRegistrationToken stale = *m_conversion_registration;
        auto removed = m_registry.UnregisterConversion(stale);
        Require(removed && *removed, "conversion unregister failed");
        ++m_conversion_revision;
        const std::uint64_t revision = m_registry.ConversionRevision();
        Require(m_registry.UnregisterConversion(stale).value_or(true) == false &&
                    m_registry.ConversionRevision() == revision,
            "stale conversion unregister changed the live generation");
        m_conversion_registration.reset();
        ++m_coverage.conversion_unregistrations;
    }

    void VerifyConversionModel() {
        Require(m_registry.ConversionRevision() == m_conversion_revision,
            "type registry revision diverged from conversion model");
        const auto live = m_registry.Check(
            TypeId{"state.source"}, TypeId{"state.destination"}, PinKind::Data);
        if (m_conversion_registration) {
            Require(live.status == ConnectionResult::Status::RequiresConversion && live.recipe &&
                        live.recipe->Registration() == *m_conversion_registration &&
                        live.recipe->Descriptor() == Conversion(m_conversion_alternate),
                "live conversion recipe diverged from conversion model");
            const auto registrations = m_registry.RegistrationsForNodeType(
                TypeId{m_conversion_alternate ? "state.converter-b" : "state.converter-a"});
            Require(registrations == std::vector<ConversionRegistrationToken>{*m_conversion_registration},
                "conversion reverse index diverged from conversion model");
        } else {
            Require(live.status == ConnectionResult::Status::Rejected && !live.recipe,
                "inactive conversion remained live");
            Require(!m_registry.HasConversionsForNodeType(TypeId{"state.converter-a"}) &&
                        !m_registry.HasConversionsForNodeType(TypeId{"state.converter-b"}),
                "inactive conversion remained in the reverse index");
        }
        for (const RetainedConversionSnapshot& snapshot : m_conversion_snapshots) {
            const auto retained = snapshot.snapshot.Check(
                TypeId{"state.source"}, TypeId{"state.destination"}, PinKind::Data);
            Require(retained.status == snapshot.status && retained.recipe == snapshot.recipe,
                "retained conversion snapshot changed its immutable result");
        }
    }

    void VerifyState(const bool full_validation) {
        Require(CanonicalState() == m_history.current,
                "live canonical state diverged from the state-machine model");
        VerifyPresentationModel();
        Require(m_commands.CanUndo() == !m_history.undo.empty(),
                "CanUndo diverged from canonical history model");
        Require(m_commands.CanRedo() == !m_history.redo.empty(),
                "CanRedo diverged from canonical history model");
        Require(m_commands.HasPending() == m_pending.has_value(),
                "pending command state diverged from the state-machine model");
        VerifyIndexes();
        VerifyConversionModel();
        if (full_validation) VerifyValidation();
    }

    void VerifyIndexes() {
        std::map<NodeId, GraphId> node_owners;
        std::map<PinId, PinOwner> pin_owners;
        std::map<LinkId, GraphId> link_owners;
        std::map<PinId, std::set<LinkId>> incidence;
        std::map<std::pair<PinId, PinId>, LinkId> connections;
        std::map<GraphId, std::map<NodeId, SubgraphCallSite>> callers;
        std::map<GraphId, std::size_t> owned_callers;
        std::map<GraphId, std::set<GraphId>> dependencies;
        std::map<PinId, IntergraphLinkId> intergraph_for_pin;
        std::map<GraphId, std::set<IntergraphLinkId>> intergraph_for_graph;
        std::map<GraphId, std::set<NodeId>> boundary_inputs;
        std::map<GraphId, std::set<NodeId>> boundary_outputs;
        std::map<RoutePointId, LinkId> route_owners;
        std::map<NodeId, std::set<GroupId>> groups_for_node;
        std::map<GraphId, std::set<GroupId>> groups_for_graph;
        std::set<GraphId> graph_ids;
        std::vector<PinId> current_outputs;
        std::vector<PinId> current_inputs;

        for (const auto& graph_reference : m_document.Graphs()) {
            const Graph& graph = graph_reference.get();
            graph_ids.insert(graph.id);
            Remember(m_recent_graphs, m_seen_graphs, graph.id);
            Require(m_document.FindGraph(graph.id) == &graph, "FindGraph disagrees with Graphs scan");
            for (const auto& [node_id, node] : graph.nodes) {
                Remember(m_recent_nodes, m_seen_nodes, node_id);
                Require(node_owners.emplace(node_id, graph.id).second, "duplicate node in primary graph scan");
                Require(m_document.FindNode(graph.id, node_id) == &node, "FindNode disagrees with graph node map");
                if (node.role == NodeRole::BoundaryInput) boundary_inputs[graph.id].insert(node_id);
                if (node.role == NodeRole::BoundaryOutput) boundary_outputs[graph.id].insert(node_id);
                if (node.subgraph) {
                    if (const auto* target = std::get_if<DocumentGraphTarget>(&node.subgraph->target)) {
                        callers[target->graph].emplace(node_id, SubgraphCallSite{
                            .graph = graph.id,
                            .node = node_id,
                            .ownership = node.subgraph->ownership,
                        });
                        if (node.subgraph->ownership == SubgraphOwnership::Owned) ++owned_callers[target->graph];
                        dependencies[graph.id].insert(target->graph);
                    }
                }
            }
            for (const auto& [pin_id, pin] : graph.pins) {
                Remember(m_recent_pins, m_seen_pins, pin_id);
                Require(pin_owners.emplace(pin_id, PinOwner{graph.id, pin.node}).second,
                        "duplicate pin in primary graph scan");
                incidence[pin_id];
                Require(m_document.FindPin(graph.id, pin_id) == &pin, "FindPin disagrees with graph pin map");
                if (pin.direction == PinDirection::Output) current_outputs.push_back(pin_id);
                if (pin.direction == PinDirection::Input) current_inputs.push_back(pin_id);
            }
            for (const auto& [link_id, link] : graph.links) {
                Remember(m_recent_links, m_seen_links, link_id);
                Require(link_owners.emplace(link_id, graph.id).second, "duplicate link in primary graph scan");
                incidence[link.output].insert(link_id);
                incidence[link.input].insert(link_id);
                Require(connections.emplace(std::pair{link.output, link.input}, link_id).second,
                        "duplicate connection pair in primary graph scan");
                RememberConnection(link.output, link.input);
                Require(m_document.FindLink(graph.id, link_id) == &link, "FindLink disagrees with graph link map");
            }
        }

        for (const auto& [id, link] : m_document.IntergraphLinks()) {
            Remember(m_recent_intergraph_links, m_seen_intergraph_links, id);
            Require(id == link.id, "intergraph map key differs from stored ID");
            Require(intergraph_for_pin.emplace(link.source.pin, id).second,
                    "duplicate intergraph source endpoint in primary scan");
            Require(intergraph_for_pin.emplace(link.destination.pin, id).second,
                    "duplicate intergraph destination endpoint in primary scan");
            intergraph_for_graph[link.source.graph].insert(id);
            intergraph_for_graph[link.destination.graph].insert(id);
            dependencies[link.source.graph].insert(link.destination.graph);
            Require(m_document.FindIntergraphLink(id) == &link,
                    "FindIntergraphLink disagrees with primary intergraph map");
        }

        for (const auto& [node_id, state] : m_presentation.Nodes()) {
            Remember(m_recent_nodes, m_seen_nodes, node_id);
            Require(node_owners.contains(node_id), "orphan node presentation in primary scan");
            Require(m_presentation.FindNode(node_id) == &state,
                    "presentation FindNode disagrees with node presentation map");
        }
        for (const auto& [link_id, state] : m_presentation.Links()) {
            Remember(m_recent_links, m_seen_links, link_id);
            Require(link_owners.contains(link_id), "orphan link presentation in primary scan");
            Require(m_presentation.FindLink(link_id) == &state,
                    "presentation FindLink disagrees with link presentation map");
            for (const RoutePoint& point : state.Route()) {
                Remember(m_recent_route_points, m_seen_route_points, point.id);
                Require(route_owners.emplace(point.id, link_id).second,
                        "duplicate route point in primary presentation scan");
            }
        }
        for (const auto& [group_id, group] : m_presentation.Groups()) {
            Remember(m_recent_groups, m_seen_groups, group_id);
            Require(group.id == group_id, "group map key differs from stored ID");
            Require(graph_ids.contains(group.graph), "group references a missing graph");
            groups_for_graph[group.graph].insert(group_id);
            for (const NodeId member : group.members) groups_for_node[member].insert(group_id);
            Require(m_presentation.FindGroup(group_id) == &group,
                    "presentation FindGroup disagrees with group map");
        }

        std::set<NodeId> node_candidates(m_recent_nodes.begin(), m_recent_nodes.end());
        for (const auto& [id, graph] : node_owners) {
            (void)graph;
            node_candidates.insert(id);
        }
        for (const NodeId id : node_candidates) {
            const GraphId expected = node_owners.contains(id) ? node_owners.at(id) : GraphId{};
            Require(m_document.FindNodeGraph(id) == expected,
                    "FindNodeGraph mismatch for node " + IdText(id));
            const std::set<GroupId> expected_groups = groups_for_node.contains(id)
                ? groups_for_node.at(id)
                : std::set<GroupId>{};
            Require(CollectIds<GroupId>(m_presentation.GroupsForNode(id)) == expected_groups,
                    "GroupsForNode mismatch for node " + IdText(id));
            const bool expected_presentation = m_presentation.Nodes().contains(id);
            Require((m_presentation.FindNode(id) != nullptr) == expected_presentation,
                    "presentation FindNode stale/missing entry for node " + IdText(id));
        }

        std::set<PinId> pin_candidates(m_recent_pins.begin(), m_recent_pins.end());
        for (const auto& [id, owner] : pin_owners) {
            (void)owner;
            pin_candidates.insert(id);
        }
        for (const PinId id : pin_candidates) {
            const auto expected_owner = pin_owners.contains(id)
                ? std::optional<PinOwner>{pin_owners.at(id)}
                : std::nullopt;
            Require(m_document.FindPinOwner(id) == expected_owner,
                    "FindPinOwner mismatch for pin " + IdText(id));
            const std::set<LinkId> expected_incidence = incidence.contains(id)
                ? incidence.at(id)
                : std::set<LinkId>{};
            Require(CollectIds<LinkId>(m_document.IncidentLinks(id)) == expected_incidence,
                    "IncidentLinks mismatch for pin " + IdText(id));
            const IntergraphLinkId expected_intergraph = intergraph_for_pin.contains(id)
                ? intergraph_for_pin.at(id)
                : IntergraphLinkId{};
            Require(m_document.IntergraphLinkForPin(id) == expected_intergraph,
                    "IntergraphLinkForPin mismatch for pin " + IdText(id));
        }

        std::set<LinkId> link_candidates(m_recent_links.begin(), m_recent_links.end());
        for (const auto& [id, graph] : link_owners) {
            (void)graph;
            link_candidates.insert(id);
        }
        for (const LinkId id : link_candidates) {
            const GraphId expected = link_owners.contains(id) ? link_owners.at(id) : GraphId{};
            Require(m_document.FindLinkGraph(id) == expected,
                    "FindLinkGraph mismatch for link " + IdText(id));
            Require((m_presentation.FindLink(id) != nullptr) == m_presentation.Links().contains(id),
                    "presentation FindLink stale/missing entry for link " + IdText(id));
        }

        for (const PinId output : current_outputs) {
            for (const PinId input : current_inputs) {
                const auto key = std::pair{output, input};
                const LinkId expected = connections.contains(key) ? connections.at(key) : LinkId{};
                Require(m_document.FindLinkBetween(output, input) == expected,
                        "FindLinkBetween mismatch for current pin pair");
            }
        }
        for (const auto& pair : m_recent_connections) {
            const LinkId expected = connections.contains(pair) ? connections.at(pair) : LinkId{};
            Require(m_document.FindLinkBetween(pair.first, pair.second) == expected,
                    "FindLinkBetween mismatch for recent pin pair");
        }

        for (const GraphId graph : graph_ids) {
            std::map<NodeId, SubgraphCallSite> actual_callers;
            for (const SubgraphCallSite& caller : m_document.SubgraphCallers(graph)) {
                actual_callers.emplace(caller.node, caller);
            }
            const auto expected_callers = callers.contains(graph)
                ? callers.at(graph)
                : std::map<NodeId, SubgraphCallSite>{};
            Require(actual_callers == expected_callers,
                    "SubgraphCallers mismatch for graph " + IdText(graph));
            Require(m_document.OwnedSubgraphCallerCount(graph) == owned_callers[graph],
                    "OwnedSubgraphCallerCount mismatch for graph " + IdText(graph));
            const std::set<IntergraphLinkId> expected_intergraph = intergraph_for_graph.contains(graph)
                ? intergraph_for_graph.at(graph)
                : std::set<IntergraphLinkId>{};
            Require(CollectIds<IntergraphLinkId>(m_document.IntergraphLinksForGraph(graph)) == expected_intergraph,
                    "IntergraphLinksForGraph mismatch for graph " + IdText(graph));
            Require(CollectIds<NodeId>(m_document.BoundaryNodes(graph, NodeRole::BoundaryInput)) ==
                        boundary_inputs[graph],
                    "BoundaryInput query mismatch for graph " + IdText(graph));
            Require(CollectIds<NodeId>(m_document.BoundaryNodes(graph, NodeRole::BoundaryOutput)) ==
                        boundary_outputs[graph],
                    "BoundaryOutput query mismatch for graph " + IdText(graph));
            Require(m_document.BoundaryNodes(graph, NodeRole::Regular).empty(),
                    "BoundaryNodes accepted a non-boundary role");
            const std::set<GroupId> expected_groups = groups_for_graph.contains(graph)
                ? groups_for_graph.at(graph)
                : std::set<GroupId>{};
            Require(CollectIds<GroupId>(m_presentation.GroupsForGraph(graph)) == expected_groups,
                    "GroupsForGraph mismatch for graph " + IdText(graph));
        }

        for (const GraphId from : graph_ids) {
            for (const GraphId target : graph_ids) {
                bool expected = from == target;
                std::vector<GraphId> pending{from};
                std::set<GraphId> visited;
                while (!expected && !pending.empty()) {
                    const GraphId graph = pending.back();
                    pending.pop_back();
                    if (!visited.insert(graph).second) continue;
                    for (const GraphId destination : dependencies[graph]) {
                        if (destination == target) {
                            expected = true;
                            break;
                        }
                        pending.push_back(destination);
                    }
                }
                Require(m_document.HasDependencyPath(from, target) == expected,
                        "HasDependencyPath mismatch for graph pair");
            }
        }

        std::set<RoutePointId> route_candidates(
            m_recent_route_points.begin(), m_recent_route_points.end());
        for (const auto& [point, link] : route_owners) {
            (void)link;
            route_candidates.insert(point);
        }
        for (const RoutePointId point : route_candidates) {
            const LinkId expected = route_owners.contains(point) ? route_owners.at(point) : LinkId{};
            Require(m_presentation.RoutePointOwner(point) == expected,
                    "RoutePointOwner mismatch for route point " + IdText(point));
            if (expected) {
                Require(m_presentation.FindRoutePoint(expected, point) != nullptr,
                        "FindRoutePoint missed a primary route point");
            } else {
                for (const auto& [link, state] : m_presentation.Links()) {
                    (void)state;
                    Require(m_presentation.FindRoutePoint(link, point) == nullptr,
                            "FindRoutePoint returned a removed route point");
                }
            }
        }

        std::set<GroupId> group_candidates(m_recent_groups.begin(), m_recent_groups.end());
        for (const auto& [id, group] : m_presentation.Groups()) {
            (void)group;
            group_candidates.insert(id);
        }
        for (const GroupId group : group_candidates) {
            Require((m_presentation.FindGroup(group) != nullptr) == m_presentation.Groups().contains(group),
                    "FindGroup stale/missing entry for group " + IdText(group));
        }

        std::set<IntergraphLinkId> intergraph_candidates(
            m_recent_intergraph_links.begin(), m_recent_intergraph_links.end());
        for (const auto& [id, link] : m_document.IntergraphLinks()) {
            (void)link;
            intergraph_candidates.insert(id);
        }
        for (const IntergraphLinkId id : intergraph_candidates) {
            Require((m_document.FindIntergraphLink(id) != nullptr) == m_document.IntergraphLinks().contains(id),
                    "FindIntergraphLink stale/missing entry");
        }

        const Graph* root = m_document.FindGraph(m_root);
        Require(root != nullptr && RegularNodes().size() <= MaximumRegularNodes,
                "bounded state machine exceeded regular-node limit");
        Require(root->links.size() <= MaximumLocalLinks,
                "bounded state machine exceeded local-link limit");
        Require(m_presentation.Groups().size() <= MaximumGroups,
                "bounded state machine exceeded group limit");
        for (const auto& [link, state] : m_presentation.Links()) {
            (void)link;
            Require(state.Route().size() <= MaximumRoutePointsPerLink,
                    "bounded state machine exceeded per-link route limit");
        }
        ++m_coverage.index_checks;
    }

    void VerifyValidation() {
        auto structure = m_document.ValidateStructure();
        if (!structure) Fail("ValidateStructure failed: " + structure.error().message);
        auto presentation = ValidateGraphPresentation(m_document, m_presentation);
        if (!presentation) Fail("ValidateGraphPresentation failed: " + presentation.error().message);
        for (const auto& graph : m_document.Graphs()) {
            const auto issues = ValidateGraph(m_document, graph.get().id, m_registry);
            const auto error = std::ranges::find_if(issues, [](const ValidationIssue& issue) {
                return issue.severity == ValidationSeverity::Error;
            });
            if (error != issues.end()) {
                Fail("ValidateGraph failed for graph " + IdText(graph.get().id) + ": " + error->message);
            }
        }
        ++m_coverage.validation_passes;
    }

    void VerifyCoverage() const {
        const auto require = [&](const bool condition, const std::string_view name) {
            if (!condition) Fail("required randomized coverage was not reached: " + std::string{name});
        };
        require(m_coverage.leaf_mutations > 0, "execute leaf mutations");
        require(m_coverage.valid_compounds > 0, "valid compound");
        require(m_coverage.failed_compounds > 0, "failing compound rollback");
        require(m_coverage.denied_operations > 0, "policy denial");
        require(m_coverage.deferred_operations >= 5, "all defer plans");
        require(m_coverage.pending_blocked_transitions >= 5, "pending across later transition");
        require(m_coverage.resume_commits > 0, "CommitPrepared resume");
        require(m_coverage.resume_allows >= 3, "reauthorization allow");
        require(m_coverage.resume_denials > 0, "reauthorization denial");
        require(m_coverage.resume_redeferrals > 0, "reauthorization repeated deferral");
        require(m_coverage.cancellations > 0, "pending cancellation");
        require(m_coverage.undos > 0 && m_coverage.redos > 0, "undo and redo");
        require(m_coverage.node_additions > 0 && m_coverage.node_deletions > 0,
                "node/pin creation and deletion");
        require(m_coverage.connections > 0 && m_coverage.link_deletions > 0 && m_coverage.reconnects > 0,
                "link connect/delete/reconnect");
        require(m_coverage.route_inserts > 0 && m_coverage.route_moves > 0 && m_coverage.route_removes > 0,
                "persistent route insert/move/remove");
        require(m_coverage.group_deltas > 0 && m_coverage.group_styles > 0 && m_coverage.group_geometry > 0,
                "groups, member deltas, style, and geometry");
        require(m_coverage.presentation_mutations > 0 && m_coverage.protection_mutations > 0,
                "presentation mutations and protection");
        require(m_coverage.subgraph_mutations > 0, "subgraph caller/dependency transitions");
        require(m_coverage.conversion_registrations > 0 &&
                    m_coverage.conversion_replacements > 0 &&
                    m_coverage.conversion_unregistrations > 0,
                "conversion registration lifecycle");
        require(m_coverage.index_checks >= m_steps, "differential index oracle after every transition");
        require(m_coverage.validation_passes > 1, "periodic full validators");
    }

    std::uint64_t m_seed;
    std::size_t m_steps;
    std::size_t m_step{0};
    std::uint64_t m_mutation_serial{0};
    std::mt19937_64 m_random;
    std::ostream* m_full_trace{nullptr};
    std::deque<std::string> m_trace;

    GraphDocument m_document;
    GraphPresentation m_presentation;
    RegistryCatalog m_registry;
    CommandStack m_commands;
    GraphId m_root;
    GraphId m_child;
    GraphId m_owned_child;
    GraphId m_left;
    GraphId m_right;
    NodeId m_caller;
    NodeId m_owned_caller;
    GroupId m_group;
    IntergraphLinkId m_intergraph;

    HistoryModel m_history;
    PresentationModel m_model;
    std::optional<PendingState> m_pending;
    Coverage m_coverage;
    std::optional<ConversionRegistrationToken> m_conversion_registration;
    bool m_conversion_alternate{false};
    std::uint64_t m_conversion_revision{0};
    std::deque<RetainedConversionSnapshot> m_conversion_snapshots;

    std::deque<GraphId> m_recent_graphs;
    std::deque<NodeId> m_recent_nodes;
    std::deque<PinId> m_recent_pins;
    std::deque<LinkId> m_recent_links;
    std::deque<GroupId> m_recent_groups;
    std::deque<RoutePointId> m_recent_route_points;
    std::deque<IntergraphLinkId> m_recent_intergraph_links;
    std::deque<std::pair<PinId, PinId>> m_recent_connections;
    std::set<GraphId> m_seen_graphs;
    std::set<NodeId> m_seen_nodes;
    std::set<PinId> m_seen_pins;
    std::set<LinkId> m_seen_links;
    std::set<GroupId> m_seen_groups;
    std::set<RoutePointId> m_seen_route_points;
    std::set<IntergraphLinkId> m_seen_intergraph_links;
    std::set<std::pair<PinId, PinId>> m_seen_connections;
};

[[nodiscard]] std::uint64_t ParseUnsigned(const std::string_view value, const std::string_view option) {
    int base = 10;
    std::string_view digits = value;
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        base = 16;
        digits.remove_prefix(2);
    }
    std::uint64_t parsed = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), parsed, base);
    if (error != std::errc{} || end != digits.data() + digits.size() || digits.empty()) {
        throw TestFailure("invalid value for " + std::string{option} + ": " + std::string{value});
    }
    return parsed;
}

} // namespace

int main(const int argc, char** argv) {
    std::uint64_t seed = 0xC0FFEEULL;
    std::size_t steps = 5'000;
    std::string trace_file;
    try {
        for (int index = 1; index < argc; ++index) {
            const std::string_view option{argv[index]};
            if ((option == "--seed" || option == "--steps") && index + 1 < argc) {
                const std::uint64_t value = ParseUnsigned(argv[++index], option);
                if (option == "--seed") seed = value;
                else steps = static_cast<std::size_t>(value);
                continue;
            }
            if (option == "--trace-file" && index + 1 < argc) {
                trace_file = argv[++index];
                continue;
            }
            throw TestFailure(
                "usage: unigui_nodes_state_machine_tests [--seed UINT64] [--steps 1..100000] "
                "[--trace-file PATH]");
        }
        if (steps < std::size(CoveragePrefix) || steps > 100'000) {
            throw TestFailure("--steps must be between " + std::to_string(std::size(CoveragePrefix)) +
                              " and 100000");
        }
        std::ofstream trace;
        if (!trace_file.empty()) {
            trace.open(trace_file, std::ios::out | std::ios::trunc);
            if (!trace) throw TestFailure("cannot open --trace-file: " + trace_file);
            trace << "seed=0x" << std::hex << seed << std::dec << " steps=" << steps << '\n';
        }
        StateMachine machine{seed, steps, trace_file.empty() ? nullptr : &trace};
        machine.Run();
        const Coverage& coverage = machine.GetCoverage();
        std::cout << "PASS seed=0x" << std::hex << seed << std::dec
                  << " steps=" << steps
                  << " index_checks=" << coverage.index_checks
                  << " validation_passes=" << coverage.validation_passes << '\n';
        return 0;
    } catch (const TestFailure& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL: seed=0x" << std::hex << seed << std::dec
                  << " unhandled exception: " << exception.what() << '\n';
        return 1;
    }
}
