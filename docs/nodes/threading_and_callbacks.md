# Threading And Callbacks

## Main-Thread Contract

Except for `UiDispatcher` operations, `UiCommandTicket` observation/waiting, and const reads of an already captured `GraphDocumentSnapshot`, UniGUI is main-thread-only. This contract includes all mutable Nodes owners and persistence/editor APIs.

Nodes objects are not internally synchronized and do not expose `WrongThread` in `Nodes::ErrorCode`; the caller must enforce thread ownership. Do not read a `GraphDocument` on one thread while the UI thread can mutate it. Do not move a document, registry, editor context, command stack, asset registry, or subscription to a worker while UI use is possible.

Use `CaptureGraphDocumentSnapshot()` on the UI thread when workers need immutable semantic authoring data. The copyable snapshot owns shallow COW state and exposes only const graph/entity queries, so it remains stable while the live document changes or is destroyed. It does not contain presentation, runtime values, evaluation order, or scheduler state. Return worker results through `UiDispatcher` before mutating live Nodes state.

```cpp
void MeasurementWorker(
    Uni::GUI::UiDispatcher dispatcher,
    std::shared_ptr<NodeWorkspace> workspace,
    Uni::GUI::Nodes::GraphId graph,
    Uni::GUI::Nodes::NodeId node) {
    const double measured = ComputeInBackground();
    auto posted = dispatcher.Post(
        [workspace = std::move(workspace), graph, node, measured]
        (Uni::GUI::UiApp&) -> Uni::GUI::UiResult<void> {
            using namespace Uni::GUI::Nodes;
            auto changed = workspace->Execute(
                std::make_unique<SetNodePropertyCommand>(
                    graph,
                    node,
                    "measurement",
                    PropertyValue{measured}));
            if (!changed) {
                return std::unexpected(Uni::GUI::UiError{
                    Uni::GUI::UiErrorCode::InvalidState,
                    changed.error().message,
                });
            }
            return {};
        });
    if (!posted) Report(posted.error());
}
```

The shared workspace keeps the posted command's target alive, but only the posted UI-thread callback accesses it. The UI loop must continue ticking while the command is pending. Store and join worker threads through application lifetime management; never block the UI thread on a worker that is waiting for a dispatcher ticket.

## Common Callback Rules

All Nodes callbacks execute synchronously on the thread that called the surrounding API. Under the main-thread contract, that is the UI thread. Callback reference arguments and contexts are borrowed for that invocation only.

Callbacks must not:

- Recursively invoke `DrawEditor()` with the same state.
- Execute/undo/redo the same busy `CommandStack`.
- Directly mutate a document or presentation observed by the callback boundary.
- Reserve IDs except through a provided callback context/transaction allocator.
- Register or unregister the registry currently invoking a cached callback.
- Retain context objects, spans, or entity references after return. `RegistryCatalog::Find()` is the exception: it returns an owning immutable descriptor handle.

Queue commands through the supplied context when one exists. Exceptions and illegal mutation are handled as described below, but containment is not a substitute for obeying the contract.

## Registry Generations

Application code captures one immutable combined generation through `RegistryCatalog::Snapshot()`. Public snapshots are passive, copyable read-only owners: they have no dependency recorder and never turn reads into hidden mutable state. Invocation leases and tracked snapshots are private internal objects used around descriptor migrations, validators, conversion checks, command apply/revert/redo, and connection admission. They pin one generation and keep the logical catalog implementation alive for the complete synchronous callback chain.

While such a lease is active, mutation of that same logical registry fails with `ErrorCode::CommandFailed`.
This applies to descriptor and conversion register/replace/unregister from command, policy, replacement-factory,
validation, or connection callbacks. Moving or destroying the public registry owner inside a callback
does not invalidate the pinned snapshot or callback function; the internal lease safely completes. After a move,
the moved-from owner has independent empty state. A public snapshot does not hold a lease and never blocks mutation.

## Node UI Callbacks

`DrawNodeBodyFn` and `DrawNodeInspectorFn` may call ImGui and queue edits through `NodeUiContext`. They must balance ImGui ID, style, font, item-width, group, table, disabled, and popup stacks that they open.

`ResolveNodeHeaderFn` is a pure per-visible-node presentation callback. It returns owning text and item data, may inspect its graph/node/selection context, and must not mutate the document, presentation, editor, or UI registry. `DrawNodeHeaderGlyphFn` may only append primitives to its provided draw list. Header actions are reported after drawing through `EditorResult::header_actions`; process them after `DrawEditor()` returns.

`NodeUiContext::Submit()`, property methods, and dynamic-pin methods are the only supported mutation path during these callbacks. Revision/identity-changing direct changes to the document, presentation, or `NodeUiRegistry` are detected and reported. Direct ID allocation is also unsupported; use the context methods even though their guarded allocation is intentionally allowed. Submitted commands execute after callback rendering; several submissions become one compound history operation.

Callback exceptions are caught and exposed through `NodeUiResult::error` or `EditorContext::LastError()`. Pending callback commands are not executed after a failed callback.

## Layout, Pin Style, And Router Callbacks

`LayoutNodeUiFn`, `PinStyleFn`, and `LinkRouterFn` are pure calculation callbacks. They must not issue ImGui mutation, queue commands, mutate editor state, allocate IDs, or modify their registry.

The editor snapshots document/presentation identity, revision and allocation state plus the relevant registry identity/revision around pure callbacks. Mutation fails closed, invalidates the operation, and records an error. Exceptions are contained. Returned layouts and paths undergo finite-coordinate, ownership, cardinality, continuity, and size-limit validation before use.

These callbacks can be cached:

- Layout runs on geometry rebuild, not every frame.
- Base pin styles can be reused within a frame; hovered state may invoke again.
- Routers run on geometry rebuild and transient rerouting interactions.

If a callback captures external state that changes without a model or registry revision, call `EditorContext::InvalidateGeometry()` for layout/router geometry. Pin style is rendering-only and is evaluated as needed.

## Node Validation Callbacks

`ValidateGraph()` pins an immutable document snapshot and one `RegistrySnapshot` before traversal. Descriptor validators and all link checks therefore observe stable configuration even if captured external code mutates the live document during a callback; diagnostics still describe the captured generations. Validators should remain pure. Any update on the active logical catalog returns `CommandFailed`.

## Context Menu Callback

`DrawEditorContextMenuFn` may draw ImGui menu items and call `EditorMenuContext::Submit()`. Its document, presentation, selection, and target accessors are const views/copies. Direct model mutation or ID allocation is detected; queued commands from a failed/invalid callback are discarded. Exceptions are converted to `LastError()`.

## Policy Callback

`GraphPolicy::evaluate_operation` and `evaluate_batch` run after apply has staged exact transaction mutations but before commit. They receive const before/staged state; leaf callbacks also receive operation index/batch size, and the batch callback receives the complete span. Every leaf callback runs even after a non-allow result. Leaf/batch callback exceptions become denials; the complete result is folded with `Deny > Replace > Defer > Allow`. Replacement is batch-only. Replacement-factory exceptions are reported as `CommandFailed`, while an empty/null factory fails closed. Neither callback nor replacement factory may recursively execute the same stack.

`CommandStack` is deliberately non-copyable and non-movable. Keep it at a stable address for the complete lifetime of synchronous command, policy, replacement-factory, and undo/redo callbacks. This prevents an active callback from transferring or destroying the history implementation guarded by the executing method.

Each `DeferOperation` or `DeferBatch` contributes an owning scoped request to `DeferredOperation::requests`. Complete authorization asynchronously, then post one `Resume(..., ResumeMode::CommitPrepared)`, `Resume(..., ResumeMode::Reauthorize, policy)`, or `Cancel()` for the complete prepared batch to the owning UI thread.

## Migration Callbacks

Document and node migrations run synchronously during deserialize/load. They mutate temporary archive/creation state, not a live editor document. Exceptions are converted to `MigrationFailed`, and no partially loaded document is returned. Migration allocators/remap functions are valid only for the callback invocation. Deserialization pins one immutable `RegistrySnapshot` before document migration begins; descriptor callbacks remain valid if an external catalog owner is moved or destroyed, and mutation of the active logical catalog returns `CommandFailed`. `DocumentMigrationRegistry` applies the same invocation guard. `NodeEditorWorkspace::Load()` also holds an exclusive command-stack guard and rejects catalog mutations for the complete callback interval.

Deferred command staging records only catalog data read by the final command/replacement attempt: positive or negative node `TypeId` lookups, positive or negative `ConversionKey` lookups, the descriptor root for enumeration, the conversion root for generalized compatibility scans, the captured type-compatibility policy, exact descriptor/conversion roots plus revisions for domain revision reads, and the exact published state for `Generation()`. The bounded no-allocation node lookup recorder conservatively promotes to a descriptor-root dependency if its fixed capacity is exhausted. `Resume()` compares only the resulting dependencies. Unrelated updates therefore do not invalidate ordinary prepared execute/undo/redo work, while a changed related/formerly-missing record, observed policy/root/domain revision, or observed combined generation returns `RevisionConflict`.

Loading can be computationally expensive, but the UniGUI contract still keeps the Nodes load API on the main thread. An application that needs background parsing must use its own data format/parser or otherwise marshal only application-owned bytes/data, then call Nodes APIs on the UI thread.

## Graph Asset Notifications

`GraphAssetRegistry` commits before dispatching change callbacks. Dispatch is synchronous and events own immutable before/after record snapshots. Exceptions are swallowed. `Write()` and `Unregister()` on the same logical registry implementation are explicitly rejected while it is dispatching; queue a later UI action instead.

Subscription destruction or `Reset()` unregisters the callback for future batches. Callback lists are snapshotted before a batch, so reset/subscribe during dispatch does not alter that batch. A strong implementation guard keeps active dispatch safe even if a callback moves or destroys the registry owner. Outstanding subscriptions remain safe after registry destruction because they hold weak state references.

## Reentrancy Summary

Safe callback behavior is read, draw/calculate, and submit through the provided sink. Unsafe behavior is immediate mutation of the objects whose iteration/cache/transaction caused the callback. The relevant APIs detect many unsafe mutations through busy flags, identity, revision, allocation epoch, and registry dispatch state, but applications must not depend on every misuse being detectable.

For command atomicity, see [commands and transactions](commands_and_transactions.md). For cache-specific callback behavior, see [routing](routing.md#geometry-cache).
