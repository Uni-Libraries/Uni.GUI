# Policies

`GraphPolicy` authorizes the exact semantic and presentation mutations staged by a command. It is independent of persisted read-only and lock flags, which are always enforced by normal execute and redo.

## Evaluation

Set a leaf callback, a batch callback, or both:

```cpp
using namespace Uni::GUI::Nodes;

GraphPolicy policy;
policy.evaluate_operation = [](const OperationPolicyContext& context,
                               const OperationIntent& intent)
    -> OperationPolicyDecision {
    const auto* property = intent.Get<PropertyOperation>();
    if (context.phase == OperationPhase::Execute &&
        intent.kind == OperationKind::SetNodeProperty &&
        property && property->change->key == "runtime.gain" &&
        ApplicationIsRunning()) {
        return DenyOperation{"Properties are frozen while the runtime is running"};
    }
    return AllowOperation{};
};

policy.evaluate_batch = [](const BatchPolicyContext& context,
                           std::span<const OperationIntent> batch)
    -> BatchPolicyDecision {
    const auto* staged = context.staged_document.FindGraph(context.staged_document.RootGraph());
    if (staged != nullptr && staged->nodes.size() > 100) {
        return DenyBatch{"The document is limited to 100 root nodes"};
    }
    return AllowBatch{};
};
```

`OperationPolicyContext` exposes before/staged document and presentation views, phase, initial/resume pass, exact `operation_index`, and `batch_size`. `BatchPolicyContext` exposes the same state views and receives the complete immutable intent span once. Policy callbacks run synchronously on the main thread. Exceptions are contained and converted to denial messages.

With no callback, authorization recording is disabled: transactions do not allocate intent payloads or command paths and skip policy traversal entirely. `CommandStack::Options::max_policy_batch_operations` is also the active transaction mutation budget. Each effective mutator reserves its exact leaf count before staging; exhaustion returns `SizeLimitExceeded` immediately and skips policy dispatch.

`CommandStack` first applies a command to private COW state. `GraphTransaction` records every effective leaf mutation with its operation kind, set/erase action, target IDs, payload, previous property value where relevant, and compound command path. The stack evaluates every record before choosing a batch outcome; it does not stop after an early defer, replacement, or denial. The command name is never used to infer authorization data, and custom commands cannot suppress records because only transaction mutation methods create them.

`OperationIntent::payload` is a compact tagged `OperationPayload` variant. Use `intent.Get<T>()` to inspect the payload matching `intent.kind`. Common alternatives include:

- `NodeOperation` and `PinOperation`, whose `value` fields are owning immutable handles to the staged COW entities;
- `LinkOperation`, containing exact link identity and endpoints inline;
- `PropertyOperation`, whose `change` contains the key plus current and previous optional values, while `intent.action` distinguishes set from erase;
- `ProtectionOperation`, `NodeSubgraphOperation`, `GraphInterfaceOperation`, and presentation operations;
- `LinkPresentationOperation`, whose impact flags distinguish immutable style, route, geometry, protection, and lifecycle changes; style-only payloads do not copy the persistent route;
- `GroupLifecycleOperation`, fixed-size `GroupGeometryOperation`, `GroupStyleOperation`, and delta-only `GroupMembershipOperation` without copying complete membership state.

`CommandPath` is an immutable shared facade. It provides `empty()`, `size()`, `operator[]`, and const iteration. All leaves emitted in one command scope share the same path storage instead of copying path strings per mutation.

Compound and nested compound commands do not receive an aggregate grant. Every child mutation is checked, including converter nodes/links, fragment contents, graph replacement diffs, and cascading link/pin/node removals. Denial of one leaf discards the complete staged transaction.

## Decisions

`OperationPolicyDecision` has three alternatives:

- `AllowOperation`: authorize this mutation and continue through the batch.
- `DenyOperation`: return `ErrorCode::PolicyRejected`; an empty reason is normalized.
- `DeferOperation`: add an owning application request for this leaf to a retained `DeferredOperation`.

`BatchPolicyDecision` has `AllowBatch`, `DenyBatch`, `ReplaceBatch`, and `DeferBatch`. Replacement exists only at batch level, so overlapping aggregate/leaf intents or multiple matching properties cannot create ambiguous replacement requests. `DeferBatch` creates one batch-scoped request without a leaf index/path.

After all leaves and the batch callback have run, outcomes use deterministic priority `Deny > Replace > Defer > Allow`. A denial suppresses replacement factory execution. Replacement is accepted only for initial execute; undo, redo, and resume reauthorization preserve recorded history/prepared state and reject replacement.

Replacement is useful for application-specific normalization:

```cpp
policy.evaluate_batch = [graph, node](const BatchPolicyContext&,
                                      std::span<const OperationIntent> batch)
    -> BatchPolicyDecision {
    const bool replace = std::ranges::any_of(batch, [](const OperationIntent& intent) {
        return intent.kind == OperationKind::SetNodeProperty;
    });
    if (!replace) return AllowBatch{};
    return ReplaceBatch{
        [graph, node] {
            return std::make_unique<SetNodeDisplayNameCommand>(
                graph, node, "Property edit requested");
        },
    };
};
```

The replacement factory must not execute the same command stack. It returns a root command for normal restaging and complete leaf/batch authorization. Chaining is bounded by `CommandStack::Options::max_replacements`. An empty factory is normalized to a batch denial, a null returned command fails closed with `PolicyRejected`, and a factory exception is contained as `CommandFailed`.

## Preview

`CheckCreateNode()`, `CheckDeleteNode()`, `CheckConnection()`, and `CheckGroupLifecycle()` are convenience adapters that evaluate a corresponding `Preview` intent without changing state. Group updates use their granular typed intents directly; the lifecycle adapter accepts only add or remove previews.

```cpp
const OperationPolicyDecision preview = policy.CheckCreateNode(
    document,
    presentation,
    CreateNodePolicyRequest{document.RootGraph(), TypeId{"audio.gain"}});

const bool available = std::holds_alternative<AllowOperation>(preview);
```

Preview is advisory and evaluates only `evaluate_operation` against a synthetic singleton intent; batch invariants require real staged state and run only during command authorization. State and policy may change before execution, so the command stack always evaluates the actual staged mutations again. `ValidateConnection()` reports structural, cardinality, protection, type/conversion compatibility, and a supplied leaf-policy denial.

## Undo And Redo

Default undo restores history without consulting current policy:

```cpp
auto result = commands.Undo(document, presentation, registry);
```

Use current policy when the application can enter a frozen state after an edit:

```cpp
auto result = commands.Undo(
    document,
    presentation,
    policy,
    UndoPolicyMode::RespectCurrentPolicy,
    registry);
```

Respectful undo first stages the actual inverse mutations, then authorizes each one with phase `Undo`. Default `RestoreHistory` undo intentionally bypasses current application policy and protection so recorded state can be restored.

Redo stages and authorizes the actual forward mutations with phase `Redo`, then revalidates types, affected structural records/index closures, and current protection. Undo and redo may be deferred, but replacement is rejected because history must retain its recorded command.

## Deferred Authorization

Each `DeferOperation::request` is an owning `std::any` application request. The command stack keeps the already staged transaction, command ownership, exact mutation batch, document/presentation/catalog identities, revisions, allocation epochs, the tracked immutable catalog snapshot used by the final command attempt, and every deferred leaf request:

```cpp
struct ApprovalRequest { std::uint64_t ticket; };

policy.evaluate_operation = [](const OperationPolicyContext&,
                               const OperationIntent& operation)
    -> OperationPolicyDecision {
    if (operation.kind == OperationKind::SetNodeProperty) {
        return DeferOperation{ApprovalRequest{73}};
    }
    return AllowOperation{};
};

auto submitted = commands.Execute(
    std::move(command), document, presentation, registry, policy);
if (submitted && submitted->deferred) {
    const DeferredOperation pending = *submitted->deferred;
    for (const DeferredRequest& deferred : pending.requests) {
        const auto& request = std::any_cast<const ApprovalRequest&>(deferred.request);
        QueueApproval(request.ticket, pending.id, deferred.operation_index, deferred.path);
    }
}
```

`DeferredRequest::scope` distinguishes operation and batch requests. Operation requests have an `operation_index` and command `path`; batch requests have no index/path. The exact owning mutation batch remains available in `DeferredOperation::batch`.

After application approval, choose resume semantics explicitly:

```cpp
commands.Resume(id, document, presentation, ResumeMode::CommitPrepared);
commands.Resume(id, document, presentation, ResumeMode::Reauthorize, current_policy);
```

`CommitPrepared` treats resume as approval of the retained request list. `Reauthorize` evaluates the retained exact batch against current application policy with `PolicyEvaluationPass::Resume`; deny keeps the operation pending, defer replaces the request list, and allow commits. Both modes require matching document/presentation identity, revision, and allocation state. Registry checks are dependency-selective: exact positive/negative descriptor and conversion lookups, whole-domain enumeration/revision reads, and combined-generation reads are compared with their current roots/values. Unrelated updates can therefore resume, while a changed recorded dependency returns `RevisionConflict`. Retained snapshots are plain immutable owners, not invocation leases, so live catalog updates remain possible while approval is pending. Use `Cancel(id)` to discard stale or denied work.

## Reentrancy And Side Effects

The policy sees const before/staged model references and should be quick and deterministic. It may read application state, but it must not mutate the document/presentation, reserve IDs, modify registries used by the operation, or recursively call the same `CommandStack`. Recursive stack use returns `CommandFailed`; the outer operation can still continue if the policy returns an allowed decision.

Command `Apply()`/`Revert()` must only mutate through `GraphTransaction`; external side effects cannot be rolled back when staging is denied or replaced. Do not retain references from policy contexts. An `OperationIntent` copy and deferred batch are owning: entity handles, paths, and large payloads remain valid until those copies are released, but all exposed entities are immutable.

See [threading and callbacks](threading_and_callbacks.md) for the common callback rules and [commands and transactions](commands_and_transactions.md) for atomicity and protection.
