# Commands And Transactions

`CommandStack` is the public mutation boundary for `GraphDocument` and `GraphPresentation`. Commands stage changes in a private `GraphTransaction`; callers cannot directly construct a transaction or invoke its commit.

The stack is non-copyable and non-movable so its history implementation cannot be transferred while a synchronous callback is executing. Store it in a stable owner; move document and presentation owners independently when needed.

## Execute

```cpp
using namespace Uni::GUI::Nodes;

auto changed = commands.Execute(
    std::make_unique<SetNodePropertyCommand>(
        graph, node, "enabled", PropertyValue{true}),
    document,
    presentation,
    registry,
    policy);

if (!changed) {
    Report(changed.error().code, changed.error().message);
}
```

`CommandResult` reports whether semantic and presentation state changed, resulting revisions, and an optional retained `DeferredOperation`. A successful no-op does not create a history entry or advance revisions.

Every execute, undo, and redo receives one `RegistryCatalog`; split node/conversion catalogs cannot be represented. Workspace users normally call `NodeEditorWorkspace::Execute()` so its single catalog is supplied through the facade.

At the start of execute, undo, or redo, the stack opens one catalog invocation lease. Each policy replacement attempt receives a fresh tracked `RegistrySnapshot`; discarded attempts discard their dependency recorders, and only the final deferred command's dependencies are retained. The current attempt's snapshot is stored in `GraphTransaction` and passed to every nested `Command::Apply()`. The outer invocation lease prevents descriptor or conversion mutation from command, policy, replacement-factory, validation, and connection-admission callbacks until staging completes.

## Atomicity

Execution has five phases:

1. Check command-stack binding and create private COW snapshots.
2. Apply the command and, only when policy is active, record exact leaf mutations.
3. Evaluate leaf policy and one batch callback against before/staged state, then fold `Deny > Replace > Defer > Allow`.
4. Validate journaled semantic/presentation records and update retained ownership, incidence, dependency, route-point, and group indexes.
5. Compare identity, revisions, and allocation epochs, then commit both owners together.

Any failed apply, validation error, exception converted by a callback boundary, or `RevisionConflict` discards staged state. With policy enabled, every effective transaction mutator consumes the configured operation budget before staging. Exhaustion is sticky and returns `SizeLimitExceeded` immediately, so a custom command that propagates errors cannot continue allocating a huge staged graph after the intent limit is reached. Revisions advance once per changed owner even when a compound command touches many entities.

Transactions maintain an entity journal. Finalization compares only journaled semantic and presentation entities to determine real changes, revision domains, and the incremental validation set. Normal commits never call the full document audit. `ValidateStructure()` remains the explicit import/diagnostic oracle and also verifies derived semantic indexes. The COW and journal counters are described in [performance](performance.md).

## Built-In Commands

The public command set covers:

- Schema/root graph changes and graph add/remove.
- Node add/delete, display name, schema-aware properties, subgraph binding, and graph interfaces.
- Dynamic pin add/remove/update/reorder.
- Link connect/reconnect/delete and automatic converter insertion.
- Intergraph connect/disconnect.
- Semantic read-only and presentation lock state.
- Node position, size, z-order, collapsed state, and complete presentation.
- Link router/color style, protection, persistent route replacement/point deltas, and complete presentation.
- Groups/comments with separate style, geometry, persistent membership-delta, collapse, z-order, and lock commands.
- Fragment paste, alignment, and layout through prepared commands.

Use the specialized command rather than reconstructing a graph. Specialized commands retain the exact data required for undo, preserve route state when requested, and assign the correct semantic revision domains.

`SetNodePropertyCommand` resolves a configurable pin schema only when the edited key is an explicit schema dependency. Retained semantic keys retain IDs, valid links remain connected, and `InvalidConnectionPolicy::Disconnect` removes invalid links and presentation as part of the same undo record. Use `InvalidConnectionPolicy::Reject` for workflows where topology must be repaired explicitly before configuration changes.

## Compound Commands

`CompoundCommand` applies children in order in one transaction and reverts them in reverse order:

```cpp
std::vector<std::unique_ptr<Command>> edits;
edits.push_back(std::make_unique<SetNodeDisplayNameCommand>(graph, node, "Output"));
edits.push_back(std::make_unique<SetNodePropertyCommand>(
    graph, node, "enabled", PropertyValue{true}));

auto result = commands.Execute(
    std::make_unique<CompoundCommand>("Configure output", std::move(edits)),
    document,
    presentation,
    registry);
```

If any child fails or any child mutation is denied by policy, none of the children commit. Nested command paths are retained in policy records. One undo reverts the whole compound operation.

## Custom Commands

Derive from `Command` and implement `Name()`, `Apply()`, and `Revert()`. `Apply()` and `Revert()` are private virtuals by design; overriding a private virtual is valid C++ and keeps invocation under `CommandStack` control.

```cpp
class RenameNodeCommand final : public Uni::GUI::Nodes::Command {
public:
    RenameNodeCommand(
        Uni::GUI::Nodes::GraphId graph,
        Uni::GUI::Nodes::NodeId node,
        std::string name)
        : m_graph(graph), m_node(node), m_after(std::move(name)) {}

    std::string_view Name() const noexcept override { return "Rename application node"; }

private:
    Uni::GUI::Nodes::Result<void> Apply(
        Uni::GUI::Nodes::GraphTransaction& transaction,
        const Uni::GUI::Nodes::RegistrySnapshot&) override {
        const auto* node = transaction.Document().FindNode(m_graph, m_node);
        if (node == nullptr) {
            return std::unexpected(Uni::GUI::Nodes::Error{
                Uni::GUI::Nodes::ErrorCode::NodeNotFound, "Node does not exist"});
        }
        if (!m_before) m_before = node->display_name;
        return transaction.SetNodeDisplayName(m_graph, m_node, m_after);
    }

    Uni::GUI::Nodes::Result<void> Revert(
        Uni::GUI::Nodes::GraphTransaction& transaction) override {
        if (!m_before) {
            return std::unexpected(Uni::GUI::Nodes::Error{
                Uni::GUI::Nodes::ErrorCode::CommandFailed, "Command was not applied"});
        }
        return transaction.SetNodeDisplayName(m_graph, m_node, *m_before);
    }

    Uni::GUI::Nodes::GraphId m_graph;
    Uni::GUI::Nodes::NodeId m_node;
    std::string m_after;
    std::optional<std::string> m_before;
};
```

Capture undo data from `transaction.Document()` or `transaction.Presentation()` on the first apply, not from an external live object. Do not mutate the original document or reserve IDs through it while a transaction is active; allocation-epoch checks will reject the commit. Use transaction allocation methods. `RegistrySnapshot` is copyable when a command needs to own the immutable generation; `transaction.Registry()` exposes that same recorder-backed snapshot to `Revert()`. Retaining a reference after the callback is invalid.

Custom commands do not declare authorization intents. Every successful transaction mutator emits its own structured policy record, so command names and custom metadata cannot hide a mutation. `GraphTransaction::SetNodeProperty()` resolves declared schema dependencies itself and rejects a transition that would require implicit link removal; a custom command that wants disconnection must capture and remove those links explicitly for undo. `Apply()` and `Revert()` must be free of external side effects because they run against staged state before policy authorization.

`TryMerge()` defaults to `false`. `SetNodePropertyCommand::Edit` and `NodeUiContext::EditProperty()` use it to coalesce an active ImGui gesture.

## Undo And Redo

The default history limit is 256 commands. Construction options also bound policy batches and replacement depth:

```cpp
commands.SetHistoryLimit(100);
CommandStack bounded{CommandStack::Options{
    .history_limit = 100,
    .max_policy_batch_operations = 64'000,
    .max_replacements = 8,
}};
if (commands.CanUndo()) ShowUndoLabel(commands.UndoName());

auto undone = commands.Undo(document, presentation, registry);
auto redone = commands.Redo(document, presentation, registry, policy);
```

Executing a changed command clears redo. Lowering the limit drops oldest entries. A limit of zero clears and disables history while still allowing commands to execute. While a deferred operation is pending, execute/undo/redo and history-limit changes are blocked; use `Resume()` or `Cancel()` first.

If policy defers an operation, the retained transaction stores a plain immutable catalog snapshot plus only the dependencies read by the final command attempt, not the internal invocation lease. Catalog mutation is therefore unblocked as soon as the original execute, undo, or redo returns. Resume compares positive/negative descriptor and conversion lookups, descriptor/conversion domain roots and revisions, or the exact combined generation only when those values were read. Unrelated updates remain valid; a changed recorded dependency returns `RevisionConflict` and leaves the operation pending for explicit cancellation. A prepared node/fragment creation also records its descriptor lookup and validates the current version/static schema during `Apply()`; additional `PinStorage::Dynamic` pins remain valid. A conversion command prepared but not yet staged fails closed if its exact recipe is replaced before `Execute()`.

Undo without an explicit policy uses `UndoPolicyMode::RestoreHistory`: it restores the recorded state even if current semantic read-only or presentation lock flags would block a new edit. To authorize undo against current application policy, use the overload with `UndoPolicyMode::RespectCurrentPolicy`. Redo always consults the supplied policy and enforces current protection. See [policies](policies.md#undo-and-redo).

The built-in node-editor Ctrl+Z shortcut uses `RespectCurrentPolicy` with the `GraphPolicy` passed to `DrawEditor()`. Application code that invokes `CommandStack::Undo()` directly chooses the mode explicitly.

## Binding And Conflicts

Once it owns history, a command stack is bound to one document identity, one presentation identity, one logical `RegistryCatalog` identity, and the expected document/presentation revisions. It rejects:

- Reuse with a different document or presentation.
- Execute, undo, or redo with a different catalog, even when numeric generations are equal (`RegistryMismatch`).
- Undo/redo after another stack changed either owner.
- A commit when callbacks or external code changed revisions or allocated IDs while the transaction was active.
- Recursive `Execute()`, `Undo()`, or `Redo()` on the same stack.

Call `Clear()` after intentionally discarding history or before binding the stack to different owners. Setting the history limit to zero also clears history and all three identity bindings. A conflict never silently rebases history.

## Workspace Save And Load

A deferred transaction is staged in `CommandStack` and is not published into the live owners. Consequently,
`NodeEditorWorkspace::Save()` remains available while an operation is pending and always serializes committed-only
`document` and `presentation` state. It does not serialize the staged transaction, deferred requests, command
history, type conversions, or registry snapshots.

`NodeEditorWorkspace::Load()` is different because it replaces those owners: it returns `OperationPending` while
a deferred operation exists and `CommandFailed` while the stack is busy. Resume or cancel before loading.

## Protection

Semantic `read_only` exists on graphs, nodes, pins, links, and intergraph links. Presentation `locked` exists on nodes, links, and groups. Normal execute and redo enforce both the baseline and staged protection state, so a custom command cannot unlock, edit, and relock an object in one transaction to bypass protection.

Protection is separate from `GraphPolicy`: read-only/lock state is persisted with the graph, while policy is an application callback evaluated at operation time.
