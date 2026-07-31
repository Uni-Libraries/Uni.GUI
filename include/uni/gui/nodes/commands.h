#pragma once

#include <uni/gui/export.h>
#include <uni/gui/nodes/presentation.h>
#include <uni/gui/nodes/registry.h>
#include <uni/gui/nodes/selection.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Uni::GUI::Nodes {

class InsertConversionCommand;
class ReconnectLinkCommand;

struct Revisions final {
    std::uint64_t model{0};
    std::uint64_t presentation{0};

    bool operator==(const Revisions&) const = default;
};

struct CommandResult final {
    bool model_changed{false};
    bool presentation_changed{false};
    Revisions revisions;
    std::optional<DeferredOperation> deferred;
};

struct GraphFragmentNode final {
    NodeCreation creation;
    NodePresentation presentation;
};

struct GraphFragmentLink final {
    Link link;
    std::optional<LinkPresentation> presentation;
};

struct GraphFragment final {
    std::vector<GraphFragmentNode> nodes;
    std::vector<GraphFragmentLink> links;
    std::vector<GroupPresentation> groups;
    struct OwnedGraph final {
        Graph graph;
        NodePresentationMap nodes;
        LinkPresentationMap links;
        std::vector<GroupPresentation> groups;
    };
    std::vector<OwnedGraph> owned_graphs;
    std::vector<IntergraphLink> intergraph_links;
    Vec2 origin;
};

struct FragmentIdRemap final {
    std::unordered_map<GraphId, GraphId, IdHash> graphs;
    std::unordered_map<NodeId, NodeId, IdHash> nodes;
    std::unordered_map<PinId, PinId, IdHash> pins;
    std::unordered_map<LinkId, LinkId, IdHash> links;
    std::unordered_map<GroupId, GroupId, IdHash> groups;
    std::unordered_map<RoutePointId, RoutePointId, IdHash> route_points;
    std::unordered_map<IntergraphLinkId, IntergraphLinkId, IdHash> intergraph_links;
};

struct PreparedGraphFragment final {
    GraphId graph;
    GraphFragment fragment;
    FragmentIdRemap remap;
    std::vector<NodeTypeDescriptorPtr> prepared_descriptors;
};

[[nodiscard]] UNI_GUI_EXPORT Result<GraphFragment> CaptureGraphFragment(const GraphDocument& document,
                                                                        const GraphPresentation& presentation,
                                                                        const GraphSelection& selection);

[[nodiscard]] UNI_GUI_EXPORT Result<PreparedGraphFragment>
PrepareGraphFragmentPaste(GraphDocument& document, GraphPresentation& presentation, const RegistryCatalog& registry,
                          const GraphFragment& fragment, GraphId graph, Vec2 position);

class UNI_GUI_EXPORT GraphTransaction final {
  public:
    ~GraphTransaction();
    GraphTransaction(GraphTransaction&& other);
    GraphTransaction& operator=(GraphTransaction&& other);
    GraphTransaction(const GraphTransaction&) = delete;
    GraphTransaction& operator=(const GraphTransaction&) = delete;

    [[nodiscard]] const GraphDocument& Document() const noexcept;
    [[nodiscard]] const GraphPresentation& Presentation() const noexcept;

    [[nodiscard]] Result<void> SetSchemaVersion(std::uint32_t version);
    [[nodiscard]] Result<void> SetRootGraph(GraphId graph);
    [[nodiscard]] GraphId AllocateGraphId() noexcept;
    [[nodiscard]] NodeId AllocateNodeId() noexcept;
    [[nodiscard]] PinId AllocatePinId() noexcept;
    [[nodiscard]] LinkId AllocateLinkId() noexcept;
    [[nodiscard]] IntergraphLinkId AllocateIntergraphLinkId() noexcept;
    [[nodiscard]] Result<GraphId> AddGraph(Graph graph);
    [[nodiscard]] Result<Graph> RemoveGraph(GraphId graph);
    [[nodiscard]] Result<void> RestoreGraph(Graph graph);

    [[nodiscard]] Result<void> AddNode(GraphId graph, NodeInstance node, std::span<const PinInstance> pins);
    [[nodiscard]] Result<RemovedNode> RemoveNode(GraphId graph, NodeId node);
    [[nodiscard]] Result<void> RestoreNode(GraphId graph, RemovedNode removed);
    [[nodiscard]] Result<void> AddDynamicPin(GraphId graph, PinInstance pin, std::size_t index);
    [[nodiscard]] Result<RemovedPin> RemoveDynamicPin(GraphId graph, PinId pin);
    [[nodiscard]] Result<void> RestoreDynamicPin(GraphId graph, RemovedPin removed);
    [[nodiscard]] Result<void> UpdateDynamicPin(GraphId graph, PinInstance pin);
    [[nodiscard]] Result<void> ReorderDynamicPins(GraphId graph, NodeId node, std::vector<PinId> order);
    [[nodiscard]] Result<void> AddLink(GraphId graph, Link link, std::optional<Link> replacing = std::nullopt);
    [[nodiscard]] Result<Link> RemoveLink(GraphId graph, LinkId link);
    [[nodiscard]] Result<void> SetNodeProperty(GraphId graph, NodeId node, std::string key,
                                               std::optional<PropertyValue> value);
    [[nodiscard]] Result<void> SetNodeDisplayName(GraphId graph, NodeId node, std::string name);
    [[nodiscard]] Result<void> SetNodeSubgraph(GraphId graph, NodeId node, std::optional<SubgraphReference> subgraph);
    [[nodiscard]] Result<void> AddIntergraphLink(IntergraphLink link);
    [[nodiscard]] Result<IntergraphLink> RemoveIntergraphLink(IntergraphLinkId link);
    [[nodiscard]] Result<void> SetGraphReadOnly(GraphId graph, bool read_only);
    [[nodiscard]] Result<void> SetNodeReadOnly(GraphId graph, NodeId node, bool read_only);
    [[nodiscard]] Result<void> SetPinReadOnly(GraphId graph, PinId pin, bool read_only);
    [[nodiscard]] Result<void> SetLinkReadOnly(GraphId graph, LinkId link, bool read_only);

    [[nodiscard]] Result<void> SetNodePresentation(NodeId node, std::optional<NodePresentation> value);
    [[nodiscard]] Result<void> SetLinkPresentation(LinkId link, std::optional<LinkPresentation> value);
    [[nodiscard]] Result<void> SetLinkRouter(LinkId link, TypeId router);
    [[nodiscard]] Result<void> SetLinkColor(LinkId link, std::optional<std::uint32_t> color);
    [[nodiscard]] Result<void> SetLinkRoute(LinkId link, PersistentRoutePointSequence route);
    [[nodiscard]] Result<void> InsertRoutePoint(LinkId link, RoutePoint point, std::size_t index);
    [[nodiscard]] Result<void> MoveRoutePoint(LinkId link, RoutePointId point, Vec2 position);
    [[nodiscard]] Result<void> RemoveRoutePoints(LinkId link, std::span<const RoutePointId> points);
    [[nodiscard]] Result<void> AddGroup(GroupPresentation group);
    [[nodiscard]] Result<GroupPresentation> RemoveGroup(GroupId group);
    [[nodiscard]] Result<void> SetGroupPosition(GroupId group, Vec2 position);
    [[nodiscard]] Result<void> SetGroupSize(GroupId group, Vec2 size);
    [[nodiscard]] Result<void> SetGroupCollapsed(GroupId group, bool collapsed);
    [[nodiscard]] Result<void> SetGroupZOrder(GroupId group, std::uint64_t z_order);
    [[nodiscard]] Result<void> SetGroupStyle(GroupId group, GroupStyleHandle style);
    [[nodiscard]] Result<void> SetGroupMembers(GroupId group, GroupMemberSet members);
    [[nodiscard]] Result<void> AddGroupMembers(GroupId group, std::span<const NodeId> members);
    [[nodiscard]] Result<void> RemoveGroupMembers(GroupId group, std::span<const NodeId> members);
    [[nodiscard]] Result<void> SetNodeLocked(NodeId node, bool locked);
    [[nodiscard]] Result<void> SetLinkLocked(LinkId link, bool locked);
    [[nodiscard]] Result<void> SetGroupLocked(GroupId group, bool locked);
    [[nodiscard]] const RegistrySnapshot& Registry() const noexcept;

  private:
    GraphTransaction(GraphDocument& document, GraphPresentation& presentation, RegistrySnapshot registry,
                     bool enforce_protection, bool record_operations, std::size_t max_operations);
    [[nodiscard]] Result<CommandResult> Commit();
    [[nodiscard]] Result<void> RebindOwners(GraphDocument& document, GraphPresentation& presentation);
    [[nodiscard]] const std::vector<OperationIntent>& Operations() const noexcept;
    [[nodiscard]] const GraphDocument& BaselineDocument() const noexcept;
    [[nodiscard]] const GraphPresentation& BaselinePresentation() const noexcept;
    [[nodiscard]] bool OperationLimitExceeded() const noexcept;
    void PushCommandScope(std::string_view name, std::size_t child);
    void PopCommandScope() noexcept;
    [[nodiscard]] Result<ConnectionResult> AuthorizeConnection(const ConnectionRequest& request);
    [[nodiscard]] Result<void> AddPlannedLink(GraphId graph, Link link);
    [[nodiscard]] Result<Link> RemovePlannedLink(GraphId graph, LinkId link);
    [[nodiscard]] PropertyImpact ResolvePropertyImpact(const TypeId& type, std::string_view key) const noexcept;
    [[nodiscard]] Result<void> SetNodePropertyWithImpact(GraphId graph, NodeId node, std::string key,
                                                         std::optional<PropertyValue> value, PropertyImpact impact);
    [[nodiscard]] Result<void> ReplaceGraph(Graph graph);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    friend class CommandStack;
    friend class CompoundCommand;
    friend class InsertConversionCommand;
    friend class ReconnectLinkCommand;
    friend class SetNodePropertyCommand;
    friend class SetNodeSubgraphCommand;
    friend class SetGraphInterfaceCommand;
};

class UNI_GUI_EXPORT Command {
  public:
    virtual ~Command();
    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

  private:
    [[nodiscard]] virtual Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) = 0;
    [[nodiscard]] virtual Result<void> Revert(GraphTransaction& transaction) = 0;
    [[nodiscard]] virtual bool TryMerge(const Command& newer);

    friend class CommandStack;
    friend class CompoundCommand;
};

class UNI_GUI_EXPORT CommandStack final {
  public:
    struct Options final {
        std::size_t history_limit{256};
        std::size_t max_policy_batch_operations{262'144};
        std::size_t max_replacements{16};
    };

    CommandStack();
    explicit CommandStack(Options options);
    ~CommandStack();
    CommandStack(CommandStack&&) = delete;
    CommandStack& operator=(CommandStack&&) = delete;
    CommandStack(const CommandStack&) = delete;
    CommandStack& operator=(const CommandStack&) = delete;

    [[nodiscard]] Result<CommandResult> Execute(std::unique_ptr<Command> command, GraphDocument& document,
                                                GraphPresentation& presentation, const RegistryCatalog& registry,
                                                const GraphPolicy& policy = {});
    [[nodiscard]] Result<CommandResult> Undo(GraphDocument& document, GraphPresentation& presentation,
                                             const RegistryCatalog& registry);
    [[nodiscard]] Result<CommandResult> Undo(GraphDocument& document, GraphPresentation& presentation,
                                             const GraphPolicy& policy, UndoPolicyMode mode,
                                             const RegistryCatalog& registry);
    [[nodiscard]] Result<CommandResult> Redo(GraphDocument& document, GraphPresentation& presentation,
                                             const RegistryCatalog& registry, const GraphPolicy& policy = {});
    [[nodiscard]] Result<CommandResult> Resume(DeferredOperationId operation, GraphDocument& document,
                                               GraphPresentation& presentation, ResumeMode mode,
                                               const GraphPolicy& policy = {});
    [[nodiscard]] Result<void> Cancel(DeferredOperationId operation);

    void Clear() noexcept;
    void SetHistoryLimit(std::size_t limit);
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool HasPending() const noexcept;
    [[nodiscard]] bool IsBusy() const noexcept;
    [[nodiscard]] const DeferredOperation* PendingOperation() const noexcept;
    [[nodiscard]] std::string_view UndoName() const noexcept;
    [[nodiscard]] std::string_view RedoName() const noexcept;

  private:
    [[nodiscard]] bool BeginExclusiveOperation() noexcept;
    void EndExclusiveOperation() noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_exclusive_operation{false};

    friend class NodeEditorWorkspace;
};

class UNI_GUI_EXPORT CompoundCommand final : public Command {
  public:
    CompoundCommand(std::string name, std::vector<std::unique_ptr<Command>> commands);
    ~CompoundCommand() override;
    [[nodiscard]] std::string_view Name() const noexcept override;

  private:
    [[nodiscard]] Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) override;
    [[nodiscard]] Result<void> Revert(GraphTransaction& transaction) override;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

#define UNI_GUI_NODE_COMMAND_DECLARATION(Class)                                                                        \
    ~Class() override;                                                                                                 \
    [[nodiscard]] std::string_view Name() const noexcept override;                                                     \
                                                                                                                       \
  private:                                                                                                             \
    [[nodiscard]] Result<void> Apply(GraphTransaction&, const RegistrySnapshot&) override;                             \
    [[nodiscard]] Result<void> Revert(GraphTransaction&) override;                                                     \
    struct Impl;                                                                                                       \
    std::unique_ptr<Impl> m_impl

class UNI_GUI_EXPORT SetSchemaVersionCommand final : public Command {
  public:
    explicit SetSchemaVersionCommand(std::uint32_t version);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetSchemaVersionCommand);
};

class UNI_GUI_EXPORT SetRootGraphCommand final : public Command {
  public:
    explicit SetRootGraphCommand(GraphId graph);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetRootGraphCommand);
};

class UNI_GUI_EXPORT AddGraphCommand final : public Command {
  public:
    explicit AddGraphCommand(Graph graph);
    explicit AddGraphCommand(GraphId graph);
    UNI_GUI_NODE_COMMAND_DECLARATION(AddGraphCommand);
};

class UNI_GUI_EXPORT RemoveGraphCommand final : public Command {
  public:
    explicit RemoveGraphCommand(GraphId graph);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveGraphCommand);
};

class UNI_GUI_EXPORT AddNodeCommand final : public Command {
  public:
    AddNodeCommand(GraphId graph, NodeCreation creation, NodePresentation presentation = {});
    UNI_GUI_NODE_COMMAND_DECLARATION(AddNodeCommand);
};

class UNI_GUI_EXPORT DeleteElementsCommand final : public Command {
  public:
    DeleteElementsCommand(GraphId graph, std::vector<NodeId> nodes, std::vector<LinkId> links = {});
    UNI_GUI_NODE_COMMAND_DECLARATION(DeleteElementsCommand);
};

class UNI_GUI_EXPORT ConnectPinsCommand final : public Command {
  public:
    ConnectPinsCommand(GraphId graph, Link link);
    UNI_GUI_NODE_COMMAND_DECLARATION(ConnectPinsCommand);
};

[[nodiscard]] UNI_GUI_EXPORT Result<std::unique_ptr<Command>>
PrepareConnectionCommand(GraphDocument& document, const GraphPresentation& presentation,
                         const RegistryCatalog& registry, ConnectionRequest request, Vec2 conversion_position,
                         const GraphPolicy& policy = {}, std::span<const PinInstance> pending_pins = {},
                         std::span<const NodeInstance> pending_nodes = {});

class UNI_GUI_EXPORT InsertConversionCommand final : public Command {
  public:
    InsertConversionCommand(GraphId graph, ConversionRecipe recipe, NodeCreation conversion,
                            NodePresentation presentation, Link first, Link second, LinkId replacing = {});
    UNI_GUI_NODE_COMMAND_DECLARATION(InsertConversionCommand);
};

class UNI_GUI_EXPORT ReconnectLinkCommand final : public Command {
  public:
    ReconnectLinkCommand(GraphId graph, LinkId link, PinId output, PinId input, bool preserve_route = false);
    UNI_GUI_NODE_COMMAND_DECLARATION(ReconnectLinkCommand);
};

class UNI_GUI_EXPORT PasteGraphFragmentCommand final : public Command {
  public:
    explicit PasteGraphFragmentCommand(PreparedGraphFragment fragment);
    UNI_GUI_NODE_COMMAND_DECLARATION(PasteGraphFragmentCommand);
};

class UNI_GUI_EXPORT AddDynamicPinCommand final : public Command {
  public:
    AddDynamicPinCommand(GraphId graph, PinInstance pin, std::size_t index);
    UNI_GUI_NODE_COMMAND_DECLARATION(AddDynamicPinCommand);
};

class UNI_GUI_EXPORT RemoveDynamicPinCommand final : public Command {
  public:
    RemoveDynamicPinCommand(GraphId graph, PinId pin);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveDynamicPinCommand);
};

class UNI_GUI_EXPORT UpdateDynamicPinCommand final : public Command {
  public:
    UpdateDynamicPinCommand(GraphId graph, PinInstance pin);
    UNI_GUI_NODE_COMMAND_DECLARATION(UpdateDynamicPinCommand);
};

class UNI_GUI_EXPORT ReorderDynamicPinsCommand final : public Command {
  public:
    ReorderDynamicPinsCommand(GraphId graph, NodeId node, std::vector<PinId> order);
    UNI_GUI_NODE_COMMAND_DECLARATION(ReorderDynamicPinsCommand);
};

class UNI_GUI_EXPORT MoveNodesCommand final : public Command {
  public:
    using Positions = std::unordered_map<NodeId, Vec2, IdHash>;
    MoveNodesCommand(GraphId graph, Positions before, Positions after);
    UNI_GUI_NODE_COMMAND_DECLARATION(MoveNodesCommand);
};

class UNI_GUI_EXPORT SetNodePropertyCommand final : public Command {
  public:
    struct Edit final {
        std::uint64_t merge_key{0};
        bool begin{false};
        bool final{true};
    };

    SetNodePropertyCommand(GraphId graph, NodeId node, std::string key, std::optional<PropertyValue> value);
    SetNodePropertyCommand(GraphId graph, NodeId node, std::string key, std::optional<PropertyValue> value, Edit edit);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodePropertyCommand);
    [[nodiscard]] bool TryMerge(const Command& newer) override;
};

class UNI_GUI_EXPORT SetNodeDisplayNameCommand final : public Command {
  public:
    SetNodeDisplayNameCommand(GraphId graph, NodeId node, std::string name);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeDisplayNameCommand);
};

class UNI_GUI_EXPORT SetNodeSubgraphCommand final : public Command {
  public:
    SetNodeSubgraphCommand(GraphId graph, NodeId node, std::optional<SubgraphReference> subgraph);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeSubgraphCommand);
};

class UNI_GUI_EXPORT SetGraphInterfaceCommand final : public Command {
  public:
    SetGraphInterfaceCommand(GraphId graph, GraphInterface interface);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGraphInterfaceCommand);
};

class UNI_GUI_EXPORT ConnectIntergraphCommand final : public Command {
  public:
    explicit ConnectIntergraphCommand(IntergraphLink link);
    UNI_GUI_NODE_COMMAND_DECLARATION(ConnectIntergraphCommand);
};

class UNI_GUI_EXPORT DisconnectIntergraphCommand final : public Command {
  public:
    explicit DisconnectIntergraphCommand(IntergraphLinkId link);
    UNI_GUI_NODE_COMMAND_DECLARATION(DisconnectIntergraphCommand);
};

class UNI_GUI_EXPORT SetGraphReadOnlyCommand final : public Command {
  public:
    SetGraphReadOnlyCommand(GraphId graph, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGraphReadOnlyCommand);
};

class UNI_GUI_EXPORT SetNodeReadOnlyCommand final : public Command {
  public:
    SetNodeReadOnlyCommand(GraphId graph, NodeId node, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeReadOnlyCommand);
};

class UNI_GUI_EXPORT SetPinReadOnlyCommand final : public Command {
  public:
    SetPinReadOnlyCommand(GraphId graph, PinId pin, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetPinReadOnlyCommand);
};

class UNI_GUI_EXPORT SetLinkReadOnlyCommand final : public Command {
  public:
    SetLinkReadOnlyCommand(GraphId graph, LinkId link, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkReadOnlyCommand);
};

class UNI_GUI_EXPORT SetNodePresentationCommand final : public Command {
  public:
    SetNodePresentationCommand(NodeId node, NodePresentation presentation);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodePresentationCommand);
};

class UNI_GUI_EXPORT SetNodeZOrderCommand final : public Command {
  public:
    using Orders = std::unordered_map<NodeId, std::uint64_t, IdHash>;
    explicit SetNodeZOrderCommand(Orders orders);
    SetNodeZOrderCommand(NodeId node, std::uint64_t z_order);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeZOrderCommand);
};

class UNI_GUI_EXPORT ResizeNodeCommand final : public Command {
  public:
    ResizeNodeCommand(NodeId node, Vec2 size);
    UNI_GUI_NODE_COMMAND_DECLARATION(ResizeNodeCommand);
};

class UNI_GUI_EXPORT SetNodeCollapsedCommand final : public Command {
  public:
    SetNodeCollapsedCommand(NodeId node, bool collapsed);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeCollapsedCommand);
};

class UNI_GUI_EXPORT SetNodeLockedCommand final : public Command {
  public:
    SetNodeLockedCommand(NodeId node, bool locked);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeLockedCommand);
};

class UNI_GUI_EXPORT SetLinkPresentationCommand final : public Command {
  public:
    SetLinkPresentationCommand(LinkId link, LinkPresentation presentation);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkPresentationCommand);
};

class UNI_GUI_EXPORT SetLinkRouterCommand final : public Command {
  public:
    SetLinkRouterCommand(LinkId link, TypeId router);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkRouterCommand);
};

class UNI_GUI_EXPORT SetLinkColorCommand final : public Command {
  public:
    SetLinkColorCommand(LinkId link, std::optional<std::uint32_t> color);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkColorCommand);
};

class UNI_GUI_EXPORT SetLinkRoutePointsCommand final : public Command {
  public:
    SetLinkRoutePointsCommand(LinkId link, PersistentRoutePointSequence route_points);
    SetLinkRoutePointsCommand(LinkId link, std::vector<RoutePoint> route_points);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkRoutePointsCommand);
};

class UNI_GUI_EXPORT InsertRoutePointCommand final : public Command {
  public:
    InsertRoutePointCommand(LinkId link, RoutePoint point, std::size_t index);
    UNI_GUI_NODE_COMMAND_DECLARATION(InsertRoutePointCommand);
};

class UNI_GUI_EXPORT MoveRoutePointCommand final : public Command {
  public:
    MoveRoutePointCommand(LinkId link, RoutePointId point, Vec2 position);
    UNI_GUI_NODE_COMMAND_DECLARATION(MoveRoutePointCommand);
};

class UNI_GUI_EXPORT RemoveRoutePointsCommand final : public Command {
  public:
    explicit RemoveRoutePointsCommand(std::vector<RoutePointRef> points);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveRoutePointsCommand);
};

class UNI_GUI_EXPORT SetLinkLockedCommand final : public Command {
  public:
    SetLinkLockedCommand(LinkId link, bool locked);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkLockedCommand);
};

class UNI_GUI_EXPORT AddGroupCommand final : public Command {
  public:
    explicit AddGroupCommand(GroupPresentation group);
    UNI_GUI_NODE_COMMAND_DECLARATION(AddGroupCommand);
};

class UNI_GUI_EXPORT RemoveGroupCommand final : public Command {
  public:
    explicit RemoveGroupCommand(GroupId group);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveGroupCommand);
};

class UNI_GUI_EXPORT SetGroupStyleCommand final : public Command {
  public:
    SetGroupStyleCommand(GroupId group, GroupStyle style);
    SetGroupStyleCommand(GroupId group, GroupStyleHandle style);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupStyleCommand);
};

class UNI_GUI_EXPORT SetGroupMembersCommand final : public Command {
  public:
    SetGroupMembersCommand(GroupId group, GroupMemberSet members);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupMembersCommand);
};

class UNI_GUI_EXPORT ChangeGroupMembersCommand final : public Command {
  public:
    ChangeGroupMembersCommand(GroupId group, std::vector<NodeId> added, std::vector<NodeId> removed);
    UNI_GUI_NODE_COMMAND_DECLARATION(ChangeGroupMembersCommand);
};

class UNI_GUI_EXPORT SetGroupZOrderCommand final : public Command {
  public:
    SetGroupZOrderCommand(GroupId group, std::uint64_t z_order);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupZOrderCommand);
};

class UNI_GUI_EXPORT MoveGroupCommand final : public Command {
  public:
    MoveGroupCommand(GroupId group, Vec2 position);
    UNI_GUI_NODE_COMMAND_DECLARATION(MoveGroupCommand);
};

class UNI_GUI_EXPORT ResizeGroupCommand final : public Command {
  public:
    ResizeGroupCommand(GroupId group, Vec2 size);
    UNI_GUI_NODE_COMMAND_DECLARATION(ResizeGroupCommand);
};

class UNI_GUI_EXPORT SetGroupCollapsedCommand final : public Command {
  public:
    SetGroupCollapsedCommand(GroupId group, bool collapsed);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupCollapsedCommand);
};

class UNI_GUI_EXPORT SetGroupLockedCommand final : public Command {
  public:
    SetGroupLockedCommand(GroupId group, bool locked);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupLockedCommand);
};

#undef UNI_GUI_NODE_COMMAND_DECLARATION

} // namespace Uni::GUI::Nodes
