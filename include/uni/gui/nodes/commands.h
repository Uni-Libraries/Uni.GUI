#pragma once

/// @file
/// @brief Atomic graph edits, undo/redo, deferred authorization and clipboard preparation.

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

/// Model and presentation serials of the same committed owner pair; either may change independently.
struct Revisions final {
    std::uint64_t model{0};
    std::uint64_t presentation{0};

    bool operator==(const Revisions&) const = default;
};

/// Outcome of a stack operation. Flags describe published changes, not attempted edits. Inspect
/// deferred even on success: a retained change has not advanced either owner or entered history.
struct CommandResult final {
    bool model_changed{false};
    bool presentation_changed{false};
    Revisions revisions;
    std::optional<DeferredOperation> deferred;
};

/// One copied node including ordered pins and its canvas state (default state if not present at capture).
struct GraphFragmentNode final {
    NodeCreation creation;
    NodePresentation presentation;
};

/// Copied internal edge; absence of presentation is preserved.
struct GraphFragmentLink final {
    Link link;
    std::optional<LinkPresentation> presentation;
};

/// Owning clipboard payload in source IDs. Top-level nodes, routes and groups are placed relative to
/// origin; owned child graphs keep their own coordinate systems. Referenced graphs are not deep-copied.
struct GraphFragment final {
    std::vector<GraphFragmentNode> nodes;
    std::vector<GraphFragmentLink> links;
    std::vector<GroupPresentation> groups;
    /// Semantic child graph with its own node, link and group presentation, copied with its owner.
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

/// Source-to-destination IDs assigned during preparation, separated by entity kind.
struct FragmentIdRemap final {
    std::unordered_map<GraphId, GraphId, IdHash> graphs;
    std::unordered_map<NodeId, NodeId, IdHash> nodes;
    std::unordered_map<PinId, PinId, IdHash> pins;
    std::unordered_map<LinkId, LinkId, IdHash> links;
    std::unordered_map<GroupId, GroupId, IdHash> groups;
    std::unordered_map<RoutePointId, RoutePointId, IdHash> route_points;
    std::unordered_map<IntergraphLinkId, IntergraphLinkId, IdHash> intergraph_links;
};

/// Ready-to-stage destination fragment, including fresh IDs, map to them and registered descriptor
/// provenance. Holding this value does not put anything in the target graph.
struct PreparedGraphFragment final {
    GraphId graph;
    GraphFragment fragment;
    FragmentIdRemap remap;
    std::vector<NodeTypeDescriptorPtr> prepared_descriptors;
};

/// Copy selected nodes and selected groups (including members) in selection.graph. Include links
/// whose **both** ends are among those nodes, recursively owned graphs and intergraph edges internal
/// to those graphs. Link/route-only selection is not copyable. Missing selected records or invalid
/// ownership are errors. origin is the component-wise minimum of copied top-level node/group positions.
/// A missing selected node returns NodeNotFound; a selected group whose member does not exist
/// in the selected graph returns InvalidGraph. No destination IDs are reserved.
[[nodiscard]] UNI_GUI_EXPORT Result<GraphFragment> CaptureGraphFragment(const GraphDocument& document,
                                                                        const GraphPresentation& presentation,
                                                                        const GraphSelection& selection);

/// Validate against the destination and registry, remap copied identities, and translate top-level
/// coordinates so origin lands at finite position. Requires an existing writable graph and a nonempty
/// node/group payload. ID reservations affect document/presentation allocation epochs, even if the
/// command is never run. Execute PasteGraphFragmentCommand to publish; preparation is not authorization.
/// Invalid ownership, cyclic internal dependencies, malformed intergraph endpoints and cross-graph
/// group membership return InvalidGraph before remapping. Exhaustion during remapping can consume
/// earlier reservations.
[[nodiscard]] UNI_GUI_EXPORT Result<PreparedGraphFragment>
PrepareGraphFragmentPaste(GraphDocument& document, GraphPresentation& presentation, const RegistryCatalog& registry,
                          const GraphFragment& fragment, GraphId graph, Vec2 position);

/// Private staging area supplied to a command by CommandStack. Successful mutators modify only
/// staged state; final policy/structure checks can still fail. Return errors to the stack rather
/// than continuing with a partially staged command. On normal execute/redo, protection uses both
/// baseline and staged flags. Effective leaves count against the active policy batch budget
/// (SizeLimitExceeded). Snapshot restoration helpers are for undo, not a way to bypass protection.
class UNI_GUI_EXPORT GraphTransaction final {
  public:
    ~GraphTransaction();
    GraphTransaction(GraphTransaction&& other);
    GraphTransaction& operator=(GraphTransaction&& other);
    GraphTransaction(const GraphTransaction&) = delete;
    GraphTransaction& operator=(const GraphTransaction&) = delete;

    /// @name Staged views
    /// Borrow for this command invocation only. Entity pointers obtained from them may be invalidated
    /// by later mutators; copy inverse data before making changes.
    /// @{
    [[nodiscard]] const GraphDocument& Document() const noexcept;
    [[nodiscard]] const GraphPresentation& Presentation() const noexcept;
    /// @}

    /// Set nonzero application version, without running migrations; zero gives InvalidArgument.
    [[nodiscard]] Result<void> SetSchemaVersion(std::uint32_t version);
    /// Choose an existing reusable entry graph. Missing/owned targets are invalid.
    [[nodiscard]] Result<void> SetRootGraph(GraphId graph);
    /// @name Staged ID reservation
    /// Reserve a fresh ID of the specified kind without inserting an entity. Check for zero
    /// (exhaustion); do not allocate on the live document while a command is running.
    /// @{
    [[nodiscard]] GraphId AllocateGraphId() noexcept;
    [[nodiscard]] NodeId AllocateNodeId() noexcept;
    [[nodiscard]] PinId AllocatePinId() noexcept;
    [[nodiscard]] LinkId AllocateLinkId() noexcept;
    [[nodiscard]] IntergraphLinkId AllocateIntergraphLinkId() noexcept;
    /// @}
    /// Stage a graph with a nonzero, currently unused ID. Removing and adding the same
    /// ID in one transaction preserves revisions for an identical final graph. A changed
    /// graph must still satisfy ownership and presentation invariants at commit.
    [[nodiscard]] Result<GraphId> AddGraph(Graph graph);
    /// Remove a non-root graph, returning its semantic record. Clean up presentation, dependent
    /// bindings and intergraph edges as required; use RemoveGraphCommand for owned-closure handling.
    [[nodiscard]] Result<Graph> RemoveGraph(GraphId graph);
    /// Restore a removed semantic graph on undo; CommandFailed if used for ordinary execute/redo.
    [[nodiscard]] Result<void> RestoreGraph(Graph graph);

    /// Insert into an existing graph. Nonzero IDs must be unused and pin keys unique per node.
    /// pins are copied in order; node.pins must either be empty or match exactly. All pins must
    /// name the supplied node; subgraph bindings must match their projected interface.
    [[nodiscard]] Result<void> AddNode(GraphId graph, NodeInstance node, std::span<const PinInstance> pins);
    /// Return semantic node/pins/incident local links. Presentation and intergraph cleanup are separate.
    [[nodiscard]] Result<RemovedNode> RemoveNode(GraphId graph, NodeId node);
    /// Undo only: reinsert removed and its recorded local edges with their original IDs.
    /// Links not incident to a restored pin return InvalidGraph. A failed restore publishes no
    /// entities, link indexes or ID reservations to the staged document.
    [[nodiscard]] Result<void> RestoreNode(GraphId graph, RemovedNode removed);
    /// Insert an instance-owned pin into its owner's full pin order at index (size means append).
    [[nodiscard]] Result<void> AddDynamicPin(GraphId graph, PinInstance pin, std::size_t index);
    /// Remove a dynamic pin and return its metadata, original index and incident local edges.
    [[nodiscard]] Result<RemovedPin> RemoveDynamicPin(GraphId graph, PinId pin);
    /// Undo only: restore the exact pin, position and recorded edges.
    /// All supplied links must touch this dynamic pin; unrelated links return InvalidGraph.
    /// Failure leaves staged entities, link indexes and ID reservations unchanged.
    [[nodiscard]] Result<void> RestoreDynamicPin(GraphId graph, RemovedPin removed);
    /// ID, owner, key and storage cannot change. Disconnect before changing the type, direction,
    /// kind or cardinality of a connected pin; protection changes use SetPinReadOnly().
    [[nodiscard]] Result<void> UpdateDynamicPin(GraphId graph, PinInstance pin);
    /// Require a permutation of every pin; static pins stay at their indices, and interface
    /// projections cannot be manually reordered.
    [[nodiscard]] Result<void> ReorderDynamicPins(GraphId graph, NodeId node, std::vector<PinId> order);
    /// Insert a direct output-to-input edge; a required converter is not inserted implicitly.
    /// For reconnect, replacing must equal the old record and share its ID with link; at least
    /// one endpoint stays fixed. Direction, compatibility, cardinality and protection errors propagate.
    [[nodiscard]] Result<void> AddLink(GraphId graph, Link link, std::optional<Link> replacing = std::nullopt);
    /// Remove and return a local semantic edge; its presentation is handled separately.
    [[nodiscard]] Result<Link> RemoveLink(GraphId graph, LinkId link);
    /// Stage a path edit; the resulting property bag must satisfy default PropertyLimits,
    /// including valid UTF-8 root keys. Schema-dependent edits that require disconnecting
    /// links must arrange their removal explicitly. Propagate errors to discard staging.
    [[nodiscard]] Result<void> SetNodeProperty(GraphId graph, NodeId node, PropertyPath path,
                                               std::optional<PropertyValue> value);
    /// Change instance text only, not its registered type.
    [[nodiscard]] Result<void> SetNodeDisplayName(GraphId graph, NodeId node, std::string name);
    /// Set or clear the local/external binding. Cannot abandon an owned child; the final projection
    /// must be valid. Prefer SetNodeSubgraphCommand to synchronize the pins as part of the edit.
    [[nodiscard]] Result<void> SetNodeSubgraph(GraphId graph, NodeId node, std::optional<SubgraphReference> subgraph);
    /// Add an exact-type proxy edge between different graphs (see IntergraphLink); rejects duplicate
    /// endpoint use or a dependency cycle. No registry conversion is inserted.
    [[nodiscard]] Result<void> AddIntergraphLink(IntergraphLink link);
    /// Disconnect a document-wide proxy edge and return both endpoint records in the removed link.
    [[nodiscard]] Result<IntergraphLink> RemoveIntergraphLink(IntergraphLinkId link);
    /// @name Semantic protection
    /// Explicitly set/clear a persisted flag. The entity must exist; protection on its containing
    /// graph or node can still block an edit. Clearing a bit does not bypass baseline protection
    /// elsewhere in this transaction. These are distinct from presentation locks.
    /// @{
    [[nodiscard]] Result<void> SetGraphReadOnly(GraphId graph, bool read_only);
    [[nodiscard]] Result<void> SetNodeReadOnly(GraphId graph, NodeId node, bool read_only);
    [[nodiscard]] Result<void> SetPinReadOnly(GraphId graph, PinId pin, bool read_only);
    [[nodiscard]] Result<void> SetLinkReadOnly(GraphId graph, LinkId link, bool read_only);
    /// @}

    /// @name Node and link presentation
    /// Set a complete persisted entry, or erase only its presentation with nullopt. A present
    /// value must belong to a semantic entity; node geometry must be finite with nonnegative size.
    /// Lock changes use explicit setters. Style-only link edits retain the route; null color clears the override.
    /// @{
    [[nodiscard]] Result<void> SetNodePresentation(NodeId node, std::optional<NodePresentation> value);
    [[nodiscard]] Result<void> SetLinkPresentation(LinkId link, std::optional<LinkPresentation> value);
    [[nodiscard]] Result<void> SetLinkRouter(LinkId link, TypeId router);
    [[nodiscard]] Result<void> SetLinkColor(LinkId link, std::optional<std::uint32_t> color);
    /// @}
    /// @name Authored route points
    /// SetLinkRoute replaces the sequence, not link style. Points require finite canvas positions
    /// and presentation-wide unique nonzero IDs; index is zero-based and may equal the current size.
    /// Move/remove address existing points of this link by ID. Empty removal is a no-op.
    /// @{
    [[nodiscard]] Result<void> SetLinkRoute(LinkId link, PersistentRoutePointSequence route);
    [[nodiscard]] Result<void> InsertRoutePoint(LinkId link, RoutePoint point, std::size_t index);
    [[nodiscard]] Result<void> MoveRoutePoint(LinkId link, RoutePointId point, Vec2 position);
    [[nodiscard]] Result<void> RemoveRoutePoints(LinkId link, std::span<const RoutePointId> points);
    /// @}
    /// @name Presentation groups
    /// Groups have a nonzero unique ID, an existing graph, a non-null style and finite geometry
    /// with nonnegative size. Removal returns the group; neither removing nor moving it changes
    /// the semantic nodes. SetGroupMembers replaces the set; add/remove apply deltas. Members
    /// must belong to the group's graph. Span inputs are consumed during the call.
    /// @{
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
    /// @}
    /// @name Presentation protection
    /// Change just the persistent lock bit on the indicated target, not its semantic read_only flag.
    /// @{
    [[nodiscard]] Result<void> SetNodeLocked(NodeId node, bool locked);
    [[nodiscard]] Result<void> SetLinkLocked(LinkId link, bool locked);
    [[nodiscard]] Result<void> SetGroupLocked(GroupId group, bool locked);
    /// @}
    /// Borrow the tracked catalog generation for this attempt (also available to Revert).
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
    [[nodiscard]] PropertyImpact ResolvePropertyImpact(const TypeId& type, const PropertyPath& path) const noexcept;
    [[nodiscard]] Result<void> SetNodePropertyWithImpact(GraphId graph, NodeId node, PropertyPath path,
                                                         std::optional<PropertyValue> value, PropertyImpact impact);
    [[nodiscard]] Result<void> SetDescriptorPins(GraphId graph, NodeId node, std::span<const PinInstance> pins);
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

/// Reversible unit of work. Override Apply and Revert even though invocation is private to the
/// stack/compound. Capture inverse data from staged views, mutate only via the transaction, and
/// propagate failures. No external side effects: policy can discard, defer or replace the attempt.
/// Do not retain argument references. Built-in constructors store their inputs; validation and
/// error reporting occur when the stack applies the command, not when constructing it.
class UNI_GUI_EXPORT Command {
  public:
    virtual ~Command();
    /// Return a borrowed display label valid while used by history; never return a view to a temporary.
    [[nodiscard]] virtual std::string_view Name() const noexcept = 0;

  private:
    /// Stage the forward edit (including redo) against this attempt's immutable registry generation.
    [[nodiscard]] virtual Result<void> Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) = 0;
    /// Stage the inverse using data saved by Apply; the stack validates and commits it separately.
    [[nodiscard]] virtual Result<void> Revert(GraphTransaction& transaction) = 0;
    /// Optionally absorb an already applied newer command into this history record; default is false.
    /// On failure or exception the receiver must still undo/redo its original edit. The stack
    /// treats an exception as a declined merge, retaining changed commands separately.
    [[nodiscard]] virtual bool TryMerge(const Command& newer);

    friend class CommandStack;
    friend class CompoundCommand;
};

/// UI-thread mutation boundary. While retaining history, binds to one document, presentation,
/// catalog identity and expected owner revisions. A different owner pair gives CommandFailed,
/// stale revisions give RevisionConflict, and a different catalog gives RegistryMismatch. Reentrant
/// execute/undo/redo returns CommandFailed. While an operation is pending they return OperationPending.
class UNI_GUI_EXPORT CommandStack final {
  public:
    /// Limits are per stack. Zero history disables retention, not execution. The operation budget
    /// bounds effective leaves recorded for an active policy; max_replacements bounds restaging chains.
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

    /// Take a non-null command, stage against both owners, authorize and commit atomically. A
    /// changed commit clears redo; a no-op does not advance revisions or add a history entry.
    /// Policy denial is PolicyRejected, deferral returns a successful uncommitted CommandResult.
    [[nodiscard]] Result<CommandResult> Execute(std::unique_ptr<Command> command, GraphDocument& document,
                                                GraphPresentation& presentation, const RegistryCatalog& registry,
                                                const GraphPolicy& policy = {});
    /// Restore the most recent history entry, bypassing current policy and protection;
    /// NothingToUndo when no entry exists.
    [[nodiscard]] Result<CommandResult> Undo(GraphDocument& document, GraphPresentation& presentation,
                                             const RegistryCatalog& registry);
    /// RespectCurrentPolicy authorizes the inverse batch (may deny or defer, never replace).
    /// RestoreHistory is equivalent to the overload without policy.
    [[nodiscard]] Result<CommandResult> Undo(GraphDocument& document, GraphPresentation& presentation,
                                             const GraphPolicy& policy, UndoPolicyMode mode,
                                             const RegistryCatalog& registry);
    /// Reapply the most recently undone command with current protection, registry and policy.
    /// NothingToRedo if empty; replacement is not allowed for history operations.
    [[nodiscard]] Result<CommandResult> Redo(GraphDocument& document, GraphPresentation& presentation,
                                             const RegistryCatalog& registry, const GraphPolicy& policy = {});
    /// Resolve the matching pending ID. Both modes check owner identity, revisions, allocation
    /// epochs and the registry dependencies actually consulted. A conflict leaves it pending.
    /// Reauthorize may deny (still pending) or replace the request list on a new deferral;
    /// CommitPrepared treats the outstanding requests as approved. Does not rerun the command.
    [[nodiscard]] Result<CommandResult> Resume(DeferredOperationId operation, GraphDocument& document,
                                               GraphPresentation& presentation, ResumeMode mode,
                                               const GraphPolicy& policy = {});
    /// Discard matching staging without changing committed owners or history. Like Resume(), a
    /// nonmatching ID returns DeferredOperationNotFound.
    [[nodiscard]] Result<void> Cancel(DeferredOperationId operation);

    /// Drop undo, redo and owner bindings; no effect while busy or pending.
    void Clear() noexcept;
    /// Bound the combined undo and redo count. On shrink discard oldest applied edits
    /// first, then the most distant redo entries. Zero clears history and owner bindings.
    /// No effect while the stack is busy or an operation is pending.
    void SetHistoryLimit(std::size_t limit);
    /// @name History and authorization status
    /// CanUndo/CanRedo report entry presence, not whether owner checks will pass. HasPending is
    /// independent of IsBusy (currently executing/exclusive). PendingOperation borrows stack storage
    /// or is null; copy before resume/cancel. History labels borrow the corresponding top command,
    /// are empty when absent, and should not be retained across stack mutations.
    /// @{
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool HasPending() const noexcept;
    [[nodiscard]] bool IsBusy() const noexcept;
    [[nodiscard]] const DeferredOperation* PendingOperation() const noexcept;
    [[nodiscard]] std::string_view UndoName() const noexcept;
    [[nodiscard]] std::string_view RedoName() const noexcept;
    /// @}

  private:
    [[nodiscard]] bool BeginExclusiveOperation() noexcept;
    void EndExclusiveOperation() noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_exclusive_operation{false};

    friend class NodeEditorWorkspace;
};

/// Own non-null children and a display name. Execute children in order in one transaction; undo
/// in reverse order. Empty list/null child gives InvalidArgument; failure of any child prevents commit.
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

/// Change the nonzero application data version without changing the JSON envelope or migrating data.
class UNI_GUI_EXPORT SetSchemaVersionCommand final : public Command {
  public:
    explicit SetSchemaVersionCommand(std::uint32_t version);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetSchemaVersionCommand);
};

/// Select an existing reusable graph as root; undo restores the previous entry point.
class UNI_GUI_EXPORT SetRootGraphCommand final : public Command {
  public:
    explicit SetRootGraphCommand(GraphId graph);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetRootGraphCommand);
};

/// Supply a complete graph or just an unused nonzero ID for an empty reusable graph. An owned graph
/// needs its owner in the same committed state (typically add and bind in a compound command).
class UNI_GUI_EXPORT AddGraphCommand final : public Command {
  public:
    explicit AddGraphCommand(Graph graph);
    explicit AddGraphCommand(GraphId graph);
    UNI_GUI_NODE_COMMAND_DECLARATION(AddGraphCommand);
};

/// Remove a non-root graph and its recursive owned descendants, including groups, presentation
/// and incident proxy edges. Surviving callers must not dangle. Undo restores the captured closure.
class UNI_GUI_EXPORT RemoveGraphCommand final : public Command {
  public:
    explicit RemoveGraphCommand(GraphId graph);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveGraphCommand);
};

/// Insert a prepared creation and its presentation into graph. Descriptor-backed preparations
/// are checked against the current registered schema; additional instance-owned pins may remain.
class UNI_GUI_EXPORT AddNodeCommand final : public Command {
  public:
    AddNodeCommand(GraphId graph, NodeCreation creation, NodePresentation presentation = {});
    UNI_GUI_NODE_COMMAND_DECLARATION(AddNodeCommand);
};

/// Delete the named nodes and/or local links. Includes incident connections, group membership,
/// presentation and owned child graphs; referenced targets survive. Undo restores captured IDs.
class UNI_GUI_EXPORT DeleteElementsCommand final : public Command {
  public:
    DeleteElementsCommand(GraphId graph, std::vector<NodeId> nodes, std::vector<LinkId> links = {});
    UNI_GUI_NODE_COMMAND_DECLARATION(DeleteElementsCommand);
};

/// Insert one local edge with a supplied ID and ordered endpoints; both pins must admit a direct wire.
class UNI_GUI_EXPORT ConnectPinsCommand final : public Command {
  public:
    ConnectPinsCommand(GraphId graph, Link link);
    UNI_GUI_NODE_COMMAND_DECLARATION(ConnectPinsCommand);
};

/// Preview request using ValidateConnection() rules; return a direct connect, reconnect or converter
/// insertion command. conversion_position is used only for a converter and must then be finite.
/// Borrowed pending records can supply endpoints for a compound command; arrange their creation
/// before executing the returned command. Preparation consumes any needed IDs, but adds no entities.
[[nodiscard]] UNI_GUI_EXPORT Result<std::unique_ptr<Command>>
PrepareConnectionCommand(GraphDocument& document, const GraphPresentation& presentation,
                         const RegistryCatalog& registry, ConnectionRequest request, Vec2 conversion_position,
                         const GraphPolicy& policy = {}, std::span<const PinInstance> pending_pins = {},
                         std::span<const NodeInstance> pending_nodes = {});

/// Insert the exact prepared converter and two links as one undoable edit. The immutable recipe
/// must still be registered and match conversion. When replacing a link, exactly its fixed-endpoint
/// leg reuses the old ID. Prefer PrepareConnectionCommand() to build a consistent chain.
class UNI_GUI_EXPORT InsertConversionCommand final : public Command {
  public:
    InsertConversionCommand(GraphId graph, ConversionRecipe recipe, NodeCreation conversion,
                            NodePresentation presentation, Link first, Link second, LinkId replacing = {});
    UNI_GUI_NODE_COMMAND_DECLARATION(InsertConversionCommand);
};

/// Rewire an existing local link without changing its ID, retaining at least one endpoint. By
/// default drop its old authored route on endpoint change; preserve_route retains the points.
class UNI_GUI_EXPORT ReconnectLinkCommand final : public Command {
  public:
    ReconnectLinkCommand(GraphId graph, LinkId link, PinId output, PinId input, bool preserve_route = false);
    UNI_GUI_NODE_COMMAND_DECLARATION(ReconnectLinkCommand);
};

/// Apply a PrepareGraphFragmentPaste() result in one transaction; recheck descriptor provenance
/// before insertion. Undo removes the newly remapped entities, not their source copies.
class UNI_GUI_EXPORT PasteGraphFragmentCommand final : public Command {
  public:
    explicit PasteGraphFragmentCommand(PreparedGraphFragment fragment);
    UNI_GUI_NODE_COMMAND_DECLARATION(PasteGraphFragmentCommand);
};

/// Insert a dynamic pin into its owner's full pin order; index may equal size to append.
class UNI_GUI_EXPORT AddDynamicPinCommand final : public Command {
  public:
    AddDynamicPinCommand(GraphId graph, PinInstance pin, std::size_t index);
    UNI_GUI_NODE_COMMAND_DECLARATION(AddDynamicPinCommand);
};

/// Remove an instance-owned pin, preserving insertion index and affected local link state for undo.
class UNI_GUI_EXPORT RemoveDynamicPinCommand final : public Command {
  public:
    RemoveDynamicPinCommand(GraphId graph, PinId pin);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveDynamicPinCommand);
};

/// Update an instance-owned pin without changing ID, node, key or storage. Connected pins cannot
/// change type, direction, kind or cardinality; label-only edits need not disconnect.
class UNI_GUI_EXPORT UpdateDynamicPinCommand final : public Command {
  public:
    UpdateDynamicPinCommand(GraphId graph, PinInstance pin);
    UNI_GUI_NODE_COMMAND_DECLARATION(UpdateDynamicPinCommand);
};

/// Supply every pin ID exactly once, leaving static pins at their original indices. Not applicable
/// to call-site or boundary projections, whose order comes from the interface.
class UNI_GUI_EXPORT ReorderDynamicPinsCommand final : public Command {
  public:
    ReorderDynamicPinsCommand(GraphId graph, NodeId node, std::vector<PinId> order);
    UNI_GUI_NODE_COMMAND_DECLARATION(ReorderDynamicPinsCommand);
};

/// Commit absolute canvas positions for an already presented set of nodes. Both maps must have
/// identical keys and finite positions; each current position must still match before.
class UNI_GUI_EXPORT MoveNodesCommand final : public Command {
  public:
    using Positions = std::unordered_map<NodeId, Vec2, IdHash>;
    MoveNodesCommand(GraphId graph, Positions before, Positions after);
    UNI_GUI_NODE_COMMAND_DECLARATION(MoveNodesCommand);
};

/// When a property-driven pin schema changes: remove now-invalid edges atomically or reject the edit.
enum class InvalidConnectionPolicy {
    Disconnect,
    Reject,
};

/// Set a leaf or erase it with nullopt, preserving siblings and capturing the old value. Path
/// rules follow SetProperty(). Intersecting schema dependencies trigger pin reconciliation by
/// semantic key. By default invalid connections are disconnected and restored on undo; Reject
/// instead fails the edit. The overload without Edit is a standalone history operation.
class UNI_GUI_EXPORT SetNodePropertyCommand final : public Command {
  public:
    /// Gesture coalescing: use a nonzero shared merge_key for consecutive edits on the same node,
    /// path and invalid-connection policy. begin starts a new record, final closes the merge window.
    /// Schema reprojection prevents merging even with a matching key.
    struct Edit final {
        std::uint64_t merge_key{0};
        bool begin{false};
        bool final{true};
    };

    SetNodePropertyCommand(GraphId graph, NodeId node, PropertyPath path, std::optional<PropertyValue> value);
    SetNodePropertyCommand(GraphId graph, NodeId node, PropertyPath path, std::optional<PropertyValue> value,
                           InvalidConnectionPolicy invalid_connections);
    SetNodePropertyCommand(GraphId graph, NodeId node, PropertyPath path, std::optional<PropertyValue> value, Edit edit);
    SetNodePropertyCommand(GraphId graph, NodeId node, PropertyPath path, std::optional<PropertyValue> value, Edit edit,
                           InvalidConnectionPolicy invalid_connections);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodePropertyCommand);
    [[nodiscard]] bool TryMerge(const Command& newer) override;
};

/// Rename the instance, not its type descriptor; undo restores the previous text.
class UNI_GUI_EXPORT SetNodeDisplayNameCommand final : public Command {
  public:
    SetNodeDisplayNameCommand(GraphId graph, NodeId node, std::string name);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeDisplayNameCommand);
};

/// Bind a node to a local graph or external interface snapshot, synchronizing projected pins. nullopt
/// clears a reference; an existing owned binding cannot simply be abandoned or retargeted.
class UNI_GUI_EXPORT SetNodeSubgraphCommand final : public Command {
  public:
    SetNodeSubgraphCommand(GraphId graph, NodeId node, std::optional<SubgraphReference> subgraph);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeSubgraphCommand);
};

/// Update the versioned signature, create missing input/output boundaries and synchronize all local
/// caller and boundary projections by key. Connected structural pins cannot simply be removed or
/// retyped. A failure at any affected site prevents the entire update.
class UNI_GUI_EXPORT SetGraphInterfaceCommand final : public Command {
  public:
    SetGraphInterfaceCommand(GraphId graph, GraphInterface interface);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGraphInterfaceCommand);
};

/// Create one cross-graph proxy edge under the direction, ownership and acyclicity rules of IntergraphLink.
class UNI_GUI_EXPORT ConnectIntergraphCommand final : public Command {
  public:
    explicit ConnectIntergraphCommand(IntergraphLink link);
    UNI_GUI_NODE_COMMAND_DECLARATION(ConnectIntergraphCommand);
};

/// Remove a proxy edge by its document-wide ID; leaves the proxy nodes intact.
class UNI_GUI_EXPORT DisconnectIntergraphCommand final : public Command {
  public:
    explicit DisconnectIntergraphCommand(IntergraphLinkId link);
    UNI_GUI_NODE_COMMAND_DECLARATION(DisconnectIntergraphCommand);
};

/// Set/clear the graph's persisted semantic protection, not its children's individual flags.
class UNI_GUI_EXPORT SetGraphReadOnlyCommand final : public Command {
  public:
    SetGraphReadOnlyCommand(GraphId graph, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGraphReadOnlyCommand);
};

/// Set/clear the node's semantic edit protection within graph. A read-only parent graph still applies.
class UNI_GUI_EXPORT SetNodeReadOnlyCommand final : public Command {
  public:
    SetNodeReadOnlyCommand(GraphId graph, NodeId node, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeReadOnlyCommand);
};

/// Protect or unprotect an endpoint without changing its schema; a protected graph or node can block this.
class UNI_GUI_EXPORT SetPinReadOnlyCommand final : public Command {
  public:
    SetPinReadOnlyCommand(GraphId graph, PinId pin, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetPinReadOnlyCommand);
};

/// Change semantic edge protection; this is separate from the presentation lock on its route.
class UNI_GUI_EXPORT SetLinkReadOnlyCommand final : public Command {
  public:
    SetLinkReadOnlyCommand(GraphId graph, LinkId link, bool read_only);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkReadOnlyCommand);
};

/// Replace the node's persisted canvas state, remembering whether it had an entry for undo. Use
/// SetNodeLockedCommand for explicit lock transitions.
class UNI_GUI_EXPORT SetNodePresentationCommand final : public Command {
  public:
    SetNodePresentationCommand(NodeId node, NodePresentation presentation);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodePresentationCommand);
};

/// Set stacking order for one node or a nonempty ID-to-order map. All nodes need presentation entries.
class UNI_GUI_EXPORT SetNodeZOrderCommand final : public Command {
  public:
    using Orders = std::unordered_map<NodeId, std::uint64_t, IdHash>;
    explicit SetNodeZOrderCommand(Orders orders);
    SetNodeZOrderCommand(NodeId node, std::uint64_t z_order);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeZOrderCommand);
};

/// Set finite, nonnegative canvas size on an existing node presentation, keeping its position.
class UNI_GUI_EXPORT ResizeNodeCommand final : public Command {
  public:
    ResizeNodeCommand(NodeId node, Vec2 size);
    UNI_GUI_NODE_COMMAND_DECLARATION(ResizeNodeCommand);
};

/// Collapse/expand an existing presentation without removing its pins or incident links.
class UNI_GUI_EXPORT SetNodeCollapsedCommand final : public Command {
  public:
    SetNodeCollapsedCommand(NodeId node, bool collapsed);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeCollapsedCommand);
};

/// Set/clear the node presentation lock without changing semantic read_only.
class UNI_GUI_EXPORT SetNodeLockedCommand final : public Command {
  public:
    SetNodeLockedCommand(NodeId node, bool locked);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetNodeLockedCommand);
};

/// Replace the authored link style and route as one change; use SetLinkLockedCommand to change protection.
class UNI_GUI_EXPORT SetLinkPresentationCommand final : public Command {
  public:
    SetLinkPresentationCommand(LinkId link, LinkPresentation presentation);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkPresentationCommand);
};

/// Store a router type ID, not its callback. A style-only edit does not rewrite authored route points.
class UNI_GUI_EXPORT SetLinkRouterCommand final : public Command {
  public:
    SetLinkRouterCommand(LinkId link, TypeId router);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkRouterCommand);
};

/// Set the link's packed color override, or clear it with nullopt without replacing the route.
class UNI_GUI_EXPORT SetLinkColorCommand final : public Command {
  public:
    SetLinkColorCommand(LinkId link, std::optional<std::uint32_t> color);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkColorCommand);
};

/// Replace the complete authored point order, or clear it with an empty sequence. IDs must be
/// nonzero and unique in the presentation; positions must be finite canvas coordinates.
class UNI_GUI_EXPORT SetLinkRoutePointsCommand final : public Command {
  public:
    SetLinkRoutePointsCommand(LinkId link, PersistentRoutePointSequence route_points);
    SetLinkRoutePointsCommand(LinkId link, std::vector<RoutePoint> route_points);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkRoutePointsCommand);
};

/// Insert a fresh point before index (or append at size) in the named edge's route.
class UNI_GUI_EXPORT InsertRoutePointCommand final : public Command {
  public:
    InsertRoutePointCommand(LinkId link, RoutePoint point, std::size_t index);
    UNI_GUI_NODE_COMMAND_DECLARATION(InsertRoutePointCommand);
};

/// Move an existing point of link to an absolute finite canvas position; its ID and order are kept.
class UNI_GUI_EXPORT MoveRoutePointCommand final : public Command {
  public:
    MoveRoutePointCommand(LinkId link, RoutePointId point, Vec2 position);
    UNI_GUI_NODE_COMMAND_DECLARATION(MoveRoutePointCommand);
};

/// Remove points identified by (link,point) pairs across routes, without removing the links. Duplicate
/// pairs are ignored; an empty selection or a nonexistent point gives InvalidArgument.
class UNI_GUI_EXPORT RemoveRoutePointsCommand final : public Command {
  public:
    explicit RemoveRoutePointsCommand(std::vector<RoutePointRef> points);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveRoutePointsCommand);
};

/// Change link presentation protection without altering semantic link protection or route points.
class UNI_GUI_EXPORT SetLinkLockedCommand final : public Command {
  public:
    SetLinkLockedCommand(LinkId link, bool locked);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetLinkLockedCommand);
};

/// Add a presentation group/comment in its graph with unique ID, valid geometry, style and members.
class UNI_GUI_EXPORT AddGroupCommand final : public Command {
  public:
    explicit AddGroupCommand(GroupPresentation group);
    UNI_GUI_NODE_COMMAND_DECLARATION(AddGroupCommand);
};

/// Remove the group presentation, not its member nodes. Undo restores geometry, style and membership.
class UNI_GUI_EXPORT RemoveGroupCommand final : public Command {
  public:
    explicit RemoveGroupCommand(GroupId group);
    UNI_GUI_NODE_COMMAND_DECLARATION(RemoveGroupCommand);
};

/// Replace style independently of geometry/membership. Supply a style value or a non-null immutable handle.
class UNI_GUI_EXPORT SetGroupStyleCommand final : public Command {
  public:
    SetGroupStyleCommand(GroupId group, GroupStyle style);
    SetGroupStyleCommand(GroupId group, GroupStyleHandle style);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupStyleCommand);
};

/// Replace all memberships with members; all IDs must name nodes of the group's graph.
class UNI_GUI_EXPORT SetGroupMembersCommand final : public Command {
  public:
    SetGroupMembersCommand(GroupId group, GroupMemberSet members);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupMembersCommand);
};

/// Apply an ID delta instead of replacing the set. Duplicates and ineffective changes are skipped;
/// addition wins for IDs in both lists. Undo reverses only effective changes.
class UNI_GUI_EXPORT ChangeGroupMembersCommand final : public Command {
  public:
    ChangeGroupMembersCommand(GroupId group, std::vector<NodeId> added, std::vector<NodeId> removed);
    UNI_GUI_NODE_COMMAND_DECLARATION(ChangeGroupMembersCommand);
};

/// Set the group's stacking value independently of its member nodes' z-order.
class UNI_GUI_EXPORT SetGroupZOrderCommand final : public Command {
  public:
    SetGroupZOrderCommand(GroupId group, std::uint64_t z_order);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupZOrderCommand);
};

/// Set the group's absolute canvas position; moving its member nodes requires a separate command.
class UNI_GUI_EXPORT MoveGroupCommand final : public Command {
  public:
    MoveGroupCommand(GroupId group, Vec2 position);
    UNI_GUI_NODE_COMMAND_DECLARATION(MoveGroupCommand);
};

/// Set a finite nonnegative size without changing group membership or resizing member nodes.
class UNI_GUI_EXPORT ResizeGroupCommand final : public Command {
  public:
    ResizeGroupCommand(GroupId group, Vec2 size);
    UNI_GUI_NODE_COMMAND_DECLARATION(ResizeGroupCommand);
};

/// Change only collapsed/expanded presentation state. Members remain in the semantic graph.
class UNI_GUI_EXPORT SetGroupCollapsedCommand final : public Command {
  public:
    SetGroupCollapsedCommand(GroupId group, bool collapsed);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupCollapsedCommand);
};

/// Change group presentation protection without setting member-node locks.
class UNI_GUI_EXPORT SetGroupLockedCommand final : public Command {
  public:
    SetGroupLockedCommand(GroupId group, bool locked);
    UNI_GUI_NODE_COMMAND_DECLARATION(SetGroupLockedCommand);
};

#undef UNI_GUI_NODE_COMMAND_DECLARATION

} // namespace Uni::GUI::Nodes
