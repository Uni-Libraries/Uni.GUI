#include "nodes/commands/internal.h"

namespace Uni::GUI::Nodes {
namespace {

using CommandDetail::MakeError;

struct BusyGuard final {
    bool& busy;
    ~BusyGuard() {
        busy = false;
    }
};

struct BatchAuthorization final {
    std::optional<DenyOperation> denial;
    std::optional<ReplaceBatch> replacement;
    std::vector<DeferredRequest> deferred;
};

[[nodiscard]] BatchAuthorization AuthorizeBatch(const GraphDocument& before_document,
                                                const GraphPresentation& before_presentation,
                                                const GraphDocument& staged_document,
                                                const GraphPresentation& staged_presentation, const GraphPolicy& policy,
                                                const OperationPhase phase, const PolicyEvaluationPass pass,
                                                const std::vector<OperationIntent>& batch) {
    BatchAuthorization authorization;
    for (std::size_t index = 0; index < batch.size(); ++index) {
        auto decision = policy.EvaluateOperation(
            OperationPolicyContext{
                .before_document = before_document,
                .before_presentation = before_presentation,
                .staged_document = staged_document,
                .staged_presentation = staged_presentation,
                .phase = phase,
                .pass = pass,
                .operation_index = index,
                .batch_size = batch.size(),
            },
            batch[index]);
        if (auto* denied = std::get_if<DenyOperation>(&decision)) {
            if (!authorization.denial) authorization.denial = std::move(*denied);
        } else if (auto* deferred = std::get_if<DeferOperation>(&decision)) {
            authorization.deferred.push_back(DeferredRequest{
                .scope = DeferredRequestScope::Operation,
                .operation_index = index,
                .path = batch[index].path,
                .request = std::move(deferred->request),
            });
        }
    }
    auto batch_decision = policy.EvaluateBatch(
        BatchPolicyContext{
            .before_document = before_document,
            .before_presentation = before_presentation,
            .staged_document = staged_document,
            .staged_presentation = staged_presentation,
            .phase = phase,
            .pass = pass,
            .batch_size = batch.size(),
        },
        batch);
    if (auto* denied = std::get_if<DenyBatch>(&batch_decision)) {
        if (!authorization.denial) authorization.denial = DenyOperation{std::move(denied->reason)};
    } else if (auto* replacement = std::get_if<ReplaceBatch>(&batch_decision)) {
        authorization.replacement = std::move(*replacement);
    } else if (auto* deferred = std::get_if<DeferBatch>(&batch_decision)) {
        authorization.deferred.push_back(DeferredRequest{
            .scope = DeferredRequestScope::Batch,
            .operation_index = std::nullopt,
            .request = std::move(deferred->request),
        });
    }
    return authorization;
}

[[nodiscard]] Error CommandException(const char* action, const std::exception& exception) {
    return MakeError(ErrorCode::CommandFailed, std::string{action} + ": " + exception.what());
}

} // namespace

Command::~Command() = default;

bool Command::TryMerge(const Command&) {
    return false;
}

struct CommandStack::Impl final {
    enum class PendingKind { Execute, Undo, Redo };

    struct Pending final {
        PendingKind kind{PendingKind::Execute};
        DeferredOperation operation;
        GraphTransaction transaction;
        Detail::RegistryInvocationSource registry_source;
        RegistrySnapshot registry_snapshot;
        std::unique_ptr<Command> command;
        std::uint64_t document_identity{0};
        std::uint64_t presentation_identity{0};
        Revisions revisions;
    };

    std::vector<std::unique_ptr<Command>> undo;
    std::vector<std::unique_ptr<Command>> redo;
    std::optional<Pending> pending;
    std::size_t history_limit{256};
    std::size_t max_policy_batch_operations{262'144};
    std::size_t max_replacements{16};
    std::uint64_t next_deferred_id{1};
    std::uint64_t document_identity{0};
    std::uint64_t presentation_identity{0};
    Detail::RegistryInvocationSource registry_source;
    Revisions expected;
    bool busy{false};

    [[nodiscard]] Result<void> ValidateBinding(const std::uint64_t current_document_identity,
                                               const std::uint64_t current_presentation_identity,
                                               const Revisions current_revisions,
                                               const Detail::RegistryInvocationSource& current_registry_source) const {
        if (document_identity == 0) {
            return {};
        }
        if (!Detail::RegistryAccess::SameCatalog(registry_source, current_registry_source)) {
            return std::unexpected(MakeError(ErrorCode::RegistryMismatch,
                                             "Command stack history belongs to a different registry catalog"));
        }
        if (document_identity != current_document_identity || presentation_identity != current_presentation_identity) {
            return std::unexpected(
                MakeError(ErrorCode::CommandFailed, "Command stack is bound to a different document or presentation"));
        }
        if (expected != current_revisions) {
            return std::unexpected(MakeError(ErrorCode::RevisionConflict,
                                             "Document or presentation was changed by another command stack"));
        }
        return {};
    }

    void Bind(const std::uint64_t current_document_identity, const std::uint64_t current_presentation_identity,
              const Revisions revisions, const Detail::RegistryInvocationSource& current_registry_source) noexcept {
        document_identity = current_document_identity;
        presentation_identity = current_presentation_identity;
        registry_source = current_registry_source;
        expected = revisions;
    }

    void ResetBinding() noexcept {
        document_identity = 0;
        presentation_identity = 0;
        registry_source = {};
        expected = {};
    }

    [[nodiscard]] Result<DeferredOperationId> AllocateDeferredId() {
        if (next_deferred_id == 0) {
            return std::unexpected(
                MakeError(ErrorCode::GenerationOverflow, "Deferred operation ID space is exhausted"));
        }
        return DeferredOperationId{next_deferred_id++};
    }
};

CommandStack::CommandStack() : CommandStack(Options{}) {}

CommandStack::CommandStack(const Options options) : m_impl(std::make_unique<Impl>()) {
    m_impl->history_limit = options.history_limit;
    m_impl->max_policy_batch_operations = options.max_policy_batch_operations;
    m_impl->max_replacements = options.max_replacements;
}

CommandStack::~CommandStack() = default;

Result<CommandResult> CommandStack::Execute(std::unique_ptr<Command> command, GraphDocument& document,
                                            GraphPresentation& presentation, const RegistryCatalog& registry,
                                            const GraphPolicy& policy) {
    if (!command) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Command cannot be null"));
    }
    if (m_impl->busy) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command stack is already executing"));
    }
    if (m_impl->pending) {
        return std::unexpected(MakeError(ErrorCode::OperationPending, "A deferred operation is pending"));
    }
    m_impl->busy = true;
    BusyGuard busy_guard{m_impl->busy};
    const auto invocation = Detail::RegistryAccess::Invoke(registry);
    if (auto binding = m_impl->ValidateBinding(document.Identity(), presentation.Identity(),
                                               {document.ModelRevision(), presentation.PresentationRevision()},
                                               invocation.Source());
        !binding) {
        return std::unexpected(std::move(binding.error()));
    }
    if (m_impl->history_limit != 0) {
        m_impl->undo.reserve(m_impl->undo.size() + 1);
    }
    for (std::size_t depth = 0; depth <= m_impl->max_replacements; ++depth) {
        const auto attempt_invocation = Detail::RegistryAccess::Invoke(invocation.Source());
        const RegistrySnapshot& snapshot = attempt_invocation.Snapshot();
        GraphTransaction transaction{document, presentation,    snapshot,
                                     true,     !policy.Empty(), m_impl->max_policy_batch_operations};
        transaction.PushCommandScope(command->Name(), 0);
        Result<void> applied;
        try {
            applied = command->Apply(transaction, snapshot);
        } catch (const std::exception& exception) {
            transaction.PopCommandScope();
            return std::unexpected(CommandException("Command apply failed", exception));
        } catch (...) {
            transaction.PopCommandScope();
            return std::unexpected(
                MakeError(ErrorCode::CommandFailed, "Command apply failed with an unknown exception"));
        }
        Detail::RegistryAccess::SealDependencies(snapshot);
        transaction.PopCommandScope();
        if (!applied) return std::unexpected(std::move(applied.error()));
        if (transaction.OperationLimitExceeded()) {
            return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Graph policy mutation batch exceeds "
                                                                           "the configured operation limit"));
        }

        BatchAuthorization authorization;
        if (!policy.Empty()) {
            authorization =
                AuthorizeBatch(transaction.BaselineDocument(), transaction.BaselinePresentation(),
                               transaction.Document(), transaction.Presentation(), policy, OperationPhase::Execute,
                               PolicyEvaluationPass::Initial, transaction.Operations());
        }
        if (auto stable = transaction.RebindOwners(document, presentation); !stable) {
            return std::unexpected(std::move(stable.error()));
        }
        if (authorization.denial) {
            return std::unexpected(MakeError(ErrorCode::PolicyRejected, authorization.denial->reason));
        }
        if (authorization.replacement) {
            if (depth == m_impl->max_replacements) {
                return std::unexpected(MakeError(ErrorCode::PolicyRejected, "Graph policy replacement limit exceeded"));
            }
            try {
                command = authorization.replacement->make_command();
            } catch (const std::exception& exception) {
                return std::unexpected(CommandException("Graph policy replacement failed", exception));
            } catch (...) {
                return std::unexpected(
                    MakeError(ErrorCode::PolicyRejected, "Graph policy replacement failed with an unknown exception"));
            }
            if (!command) {
                return std::unexpected(
                    MakeError(ErrorCode::PolicyRejected, "Graph policy returned an empty replacement"));
            }
            if (auto stable = transaction.RebindOwners(document, presentation); !stable) {
                return std::unexpected(std::move(stable.error()));
            }
            continue;
        }
        if (!authorization.deferred.empty()) {
            auto id = m_impl->AllocateDeferredId();
            if (!id) return std::unexpected(std::move(id.error()));
            try {
                DeferredOperation operation{
                    .id = *id,
                    .phase = OperationPhase::Execute,
                    .batch = transaction.Operations(),
                    .requests = std::move(authorization.deferred),
                };
                m_impl->pending.emplace(Impl::Pending{
                    .kind = Impl::PendingKind::Execute,
                    .operation = operation,
                    .transaction = std::move(transaction),
                    .registry_source = attempt_invocation.Source(),
                    .registry_snapshot = snapshot,
                    .command = std::move(command),
                    .document_identity = document.Identity(),
                    .presentation_identity = presentation.Identity(),
                    .revisions = {document.ModelRevision(), presentation.PresentationRevision()},
                });
                if (auto stable = m_impl->pending->transaction.RebindOwners(document, presentation); !stable) {
                    m_impl->pending.reset();
                    return std::unexpected(std::move(stable.error()));
                }
                return CommandResult{
                    .revisions = {document.ModelRevision(), presentation.PresentationRevision()},
                    .deferred = std::move(operation),
                };
            } catch (const std::exception& exception) {
                return std::unexpected(CommandException("Deferred operation storage failed", exception));
            } catch (...) {
                return std::unexpected(
                    MakeError(ErrorCode::CommandFailed, "Deferred operation storage failed with an unknown exception"));
            }
        }

        auto committed = transaction.Commit();
        if (!committed) return committed;
        if (!committed->model_changed && !committed->presentation_changed) {
            if (!m_impl->undo.empty()) (void)m_impl->undo.back()->TryMerge(*command);
            return committed;
        }
        m_impl->redo.clear();
        if (m_impl->history_limit == 0) {
            m_impl->ResetBinding();
            return committed;
        }
        m_impl->Bind(document.Identity(), presentation.Identity(), committed->revisions, attempt_invocation.Source());
        if (!m_impl->undo.empty() && m_impl->undo.back()->TryMerge(*command)) return committed;
        m_impl->undo.push_back(std::move(command));
        if (m_impl->undo.size() > m_impl->history_limit) {
            m_impl->undo.erase(m_impl->undo.begin(),
                               m_impl->undo.begin() +
                                   static_cast<std::ptrdiff_t>(m_impl->undo.size() - m_impl->history_limit));
        }
        return committed;
    }
    return std::unexpected(MakeError(ErrorCode::PolicyRejected, "Graph policy replacement limit exceeded"));
}

Result<CommandResult> CommandStack::Undo(GraphDocument& document, GraphPresentation& presentation,
                                         const RegistryCatalog& registry) {
    if (m_impl->busy) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command stack is already executing"));
    }
    if (m_impl->pending) {
        return std::unexpected(MakeError(ErrorCode::OperationPending, "A deferred operation is pending"));
    }
    m_impl->busy = true;
    BusyGuard busy_guard{m_impl->busy};
    if (m_impl->undo.empty()) {
        return std::unexpected(MakeError(ErrorCode::NothingToUndo, "There is no command to undo"));
    }
    const auto invocation = Detail::RegistryAccess::Invoke(registry);
    if (auto binding = m_impl->ValidateBinding(document.Identity(), presentation.Identity(),
                                               {document.ModelRevision(), presentation.PresentationRevision()},
                                               invocation.Source());
        !binding) {
        return std::unexpected(std::move(binding.error()));
    }
    m_impl->redo.reserve(m_impl->redo.size() + 1);
    auto& command = m_impl->undo.back();
    const RegistrySnapshot& snapshot = invocation.Snapshot();
    GraphTransaction transaction{document, presentation, snapshot, false, false, 0};
    transaction.PushCommandScope(command->Name(), 0);
    Result<void> reverted;
    try {
        reverted = command->Revert(transaction);
    } catch (const std::exception& exception) {
        transaction.PopCommandScope();
        return std::unexpected(CommandException("Command revert failed", exception));
    } catch (...) {
        transaction.PopCommandScope();
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command revert failed with an unknown exception"));
    }
    Detail::RegistryAccess::SealDependencies(snapshot);
    transaction.PopCommandScope();
    if (!reverted) return std::unexpected(std::move(reverted.error()));
    auto committed = transaction.Commit();
    if (!committed) {
        return committed;
    }
    m_impl->redo.push_back(std::move(command));
    m_impl->undo.pop_back();
    m_impl->Bind(document.Identity(), presentation.Identity(), committed->revisions, invocation.Source());
    return committed;
}

Result<CommandResult> CommandStack::Undo(GraphDocument& document, GraphPresentation& presentation,
                                         const GraphPolicy& policy, const UndoPolicyMode mode,
                                         const RegistryCatalog& registry) {
    if (mode == UndoPolicyMode::RestoreHistory) return Undo(document, presentation, registry);
    if (m_impl->busy) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command stack is already executing"));
    }
    if (m_impl->pending) {
        return std::unexpected(MakeError(ErrorCode::OperationPending, "A deferred operation is pending"));
    }
    m_impl->busy = true;
    BusyGuard busy_guard{m_impl->busy};
    if (m_impl->undo.empty()) {
        return std::unexpected(MakeError(ErrorCode::NothingToUndo, "There is no command to undo"));
    }
    const auto invocation = Detail::RegistryAccess::Invoke(registry);
    if (auto binding = m_impl->ValidateBinding(document.Identity(), presentation.Identity(),
                                               {document.ModelRevision(), presentation.PresentationRevision()},
                                               invocation.Source());
        !binding) {
        return std::unexpected(std::move(binding.error()));
    }
    m_impl->redo.reserve(m_impl->redo.size() + 1);
    auto& command = m_impl->undo.back();
    const RegistrySnapshot& snapshot = invocation.Snapshot();
    GraphTransaction transaction{document, presentation,    snapshot,
                                 false,    !policy.Empty(), m_impl->max_policy_batch_operations};
    transaction.PushCommandScope(command->Name(), 0);
    Result<void> reverted;
    try {
        reverted = command->Revert(transaction);
    } catch (const std::exception& exception) {
        transaction.PopCommandScope();
        return std::unexpected(CommandException("Command revert failed", exception));
    } catch (...) {
        transaction.PopCommandScope();
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command revert failed with an unknown exception"));
    }
    Detail::RegistryAccess::SealDependencies(snapshot);
    transaction.PopCommandScope();
    if (!reverted) return std::unexpected(std::move(reverted.error()));
    if (transaction.OperationLimitExceeded()) {
        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded,
                                         "Graph policy mutation batch exceeds the configured operation limit"));
    }
    BatchAuthorization authorization;
    if (!policy.Empty()) {
        authorization = AuthorizeBatch(transaction.BaselineDocument(), transaction.BaselinePresentation(),
                                       transaction.Document(), transaction.Presentation(), policy, OperationPhase::Undo,
                                       PolicyEvaluationPass::Initial, transaction.Operations());
    }
    if (auto stable = transaction.RebindOwners(document, presentation); !stable) {
        return std::unexpected(std::move(stable.error()));
    }
    if (authorization.denial) {
        return std::unexpected(MakeError(ErrorCode::PolicyRejected, authorization.denial->reason));
    }
    if (authorization.replacement) {
        return std::unexpected(MakeError(ErrorCode::PolicyRejected, "Undo cannot be replaced"));
    }
    if (!authorization.deferred.empty()) {
        auto id = m_impl->AllocateDeferredId();
        if (!id) return std::unexpected(std::move(id.error()));
        try {
            DeferredOperation operation{
                .id = *id,
                .phase = OperationPhase::Undo,
                .batch = transaction.Operations(),
                .requests = std::move(authorization.deferred),
            };
            m_impl->pending.emplace(Impl::Pending{
                .kind = Impl::PendingKind::Undo,
                .operation = operation,
                .transaction = std::move(transaction),
                .registry_source = invocation.Source(),
                .registry_snapshot = snapshot,
                .document_identity = document.Identity(),
                .presentation_identity = presentation.Identity(),
                .revisions = {document.ModelRevision(), presentation.PresentationRevision()},
            });
            if (auto stable = m_impl->pending->transaction.RebindOwners(document, presentation); !stable) {
                m_impl->pending.reset();
                return std::unexpected(std::move(stable.error()));
            }
            return CommandResult{
                .revisions = {document.ModelRevision(), presentation.PresentationRevision()},
                .deferred = std::move(operation),
            };
        } catch (const std::exception& exception) {
            return std::unexpected(CommandException("Deferred undo storage failed", exception));
        } catch (...) {
            return std::unexpected(
                MakeError(ErrorCode::CommandFailed, "Deferred undo storage failed with an unknown exception"));
        }
    }
    auto committed = transaction.Commit();
    if (!committed) return committed;
    m_impl->redo.push_back(std::move(command));
    m_impl->undo.pop_back();
    m_impl->Bind(document.Identity(), presentation.Identity(), committed->revisions, invocation.Source());
    return committed;
}

Result<CommandResult> CommandStack::Redo(GraphDocument& document, GraphPresentation& presentation,
                                         const RegistryCatalog& registry, const GraphPolicy& policy) {
    if (m_impl->busy) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command stack is already executing"));
    }
    if (m_impl->pending) {
        return std::unexpected(MakeError(ErrorCode::OperationPending, "A deferred operation is pending"));
    }
    m_impl->busy = true;
    BusyGuard busy_guard{m_impl->busy};
    if (m_impl->redo.empty()) {
        return std::unexpected(MakeError(ErrorCode::NothingToRedo, "There is no command to redo"));
    }
    const auto invocation = Detail::RegistryAccess::Invoke(registry);
    if (auto binding = m_impl->ValidateBinding(document.Identity(), presentation.Identity(),
                                               {document.ModelRevision(), presentation.PresentationRevision()},
                                               invocation.Source());
        !binding) {
        return std::unexpected(std::move(binding.error()));
    }
    m_impl->undo.reserve(m_impl->undo.size() + 1);
    auto& command = m_impl->redo.back();
    const RegistrySnapshot& snapshot = invocation.Snapshot();
    GraphTransaction transaction{document, presentation,    snapshot,
                                 true,     !policy.Empty(), m_impl->max_policy_batch_operations};
    transaction.PushCommandScope(command->Name(), 0);
    Result<void> applied;
    try {
        applied = command->Apply(transaction, snapshot);
    } catch (const std::exception& exception) {
        transaction.PopCommandScope();
        return std::unexpected(CommandException("Command apply failed", exception));
    } catch (...) {
        transaction.PopCommandScope();
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command apply failed with an unknown exception"));
    }
    Detail::RegistryAccess::SealDependencies(snapshot);
    transaction.PopCommandScope();
    if (!applied) return std::unexpected(std::move(applied.error()));
    if (transaction.OperationLimitExceeded()) {
        return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded,
                                         "Graph policy mutation batch exceeds the configured operation limit"));
    }
    BatchAuthorization authorization;
    if (!policy.Empty()) {
        authorization = AuthorizeBatch(transaction.BaselineDocument(), transaction.BaselinePresentation(),
                                       transaction.Document(), transaction.Presentation(), policy, OperationPhase::Redo,
                                       PolicyEvaluationPass::Initial, transaction.Operations());
    }
    if (auto stable = transaction.RebindOwners(document, presentation); !stable) {
        return std::unexpected(std::move(stable.error()));
    }
    if (authorization.denial) {
        return std::unexpected(MakeError(ErrorCode::PolicyRejected, authorization.denial->reason));
    }
    if (authorization.replacement) {
        return std::unexpected(MakeError(ErrorCode::PolicyRejected, "Redo cannot be replaced"));
    }
    if (!authorization.deferred.empty()) {
        auto id = m_impl->AllocateDeferredId();
        if (!id) return std::unexpected(std::move(id.error()));
        try {
            DeferredOperation operation{
                .id = *id,
                .phase = OperationPhase::Redo,
                .batch = transaction.Operations(),
                .requests = std::move(authorization.deferred),
            };
            m_impl->pending.emplace(Impl::Pending{
                .kind = Impl::PendingKind::Redo,
                .operation = operation,
                .transaction = std::move(transaction),
                .registry_source = invocation.Source(),
                .registry_snapshot = snapshot,
                .document_identity = document.Identity(),
                .presentation_identity = presentation.Identity(),
                .revisions = {document.ModelRevision(), presentation.PresentationRevision()},
            });
            if (auto stable = m_impl->pending->transaction.RebindOwners(document, presentation); !stable) {
                m_impl->pending.reset();
                return std::unexpected(std::move(stable.error()));
            }
            return CommandResult{
                .revisions = {document.ModelRevision(), presentation.PresentationRevision()},
                .deferred = std::move(operation),
            };
        } catch (const std::exception& exception) {
            return std::unexpected(CommandException("Deferred redo storage failed", exception));
        } catch (...) {
            return std::unexpected(
                MakeError(ErrorCode::CommandFailed, "Deferred redo storage failed with an unknown exception"));
        }
    }
    auto committed = transaction.Commit();
    if (!committed) {
        return committed;
    }
    m_impl->undo.push_back(std::move(command));
    m_impl->redo.pop_back();
    m_impl->Bind(document.Identity(), presentation.Identity(), committed->revisions, invocation.Source());
    return committed;
}

Result<CommandResult> CommandStack::Resume(const DeferredOperationId operation, GraphDocument& document,
                                           GraphPresentation& presentation, const ResumeMode mode,
                                           const GraphPolicy& policy) {
    if (mode != ResumeMode::CommitPrepared && mode != ResumeMode::Reauthorize) {
        return std::unexpected(MakeError(ErrorCode::InvalidArgument, "Resume mode is invalid"));
    }
    if (m_impl->busy) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command stack is already executing"));
    }
    if (!m_impl->pending || m_impl->pending->operation.id != operation) {
        return std::unexpected(
            MakeError(ErrorCode::DeferredOperationNotFound, "Deferred operation is not pending on this command stack"));
    }
    m_impl->busy = true;
    BusyGuard busy_guard{m_impl->busy};
    auto& pending = *m_impl->pending;
    if (pending.document_identity != document.Identity() || pending.presentation_identity != presentation.Identity()) {
        return std::unexpected(
            MakeError(ErrorCode::CommandFailed, "Deferred operation belongs to a different document or presentation"));
    }
    if (pending.revisions != Revisions{document.ModelRevision(), presentation.PresentationRevision()}) {
        return std::unexpected(MakeError(ErrorCode::RevisionConflict,
                                         "Document or presentation changed while the operation was deferred"));
    }
    if (!Detail::RegistryAccess::DependenciesCurrent(pending.registry_source, pending.registry_snapshot)) {
        return std::unexpected(
            MakeError(ErrorCode::RevisionConflict, "Registry generation changed while the operation was deferred"));
    }
    const auto invocation = Detail::RegistryAccess::Invoke(pending.registry_source);
    if (auto rebound = pending.transaction.RebindOwners(document, presentation); !rebound) {
        return std::unexpected(std::move(rebound.error()));
    }
    if (mode == ResumeMode::Reauthorize && !policy.Empty()) {
        if (pending.operation.batch.size() > m_impl->max_policy_batch_operations) {
            return std::unexpected(MakeError(ErrorCode::SizeLimitExceeded, "Deferred policy mutation batch exceeds "
                                                                           "the configured operation limit"));
        }
        BatchAuthorization authorization;
        authorization =
            AuthorizeBatch(pending.transaction.BaselineDocument(), pending.transaction.BaselinePresentation(),
                           pending.transaction.Document(), pending.transaction.Presentation(), policy,
                           pending.operation.phase, PolicyEvaluationPass::Resume, pending.operation.batch);
        if (auto stable = pending.transaction.RebindOwners(document, presentation); !stable) {
            return std::unexpected(std::move(stable.error()));
        }
        if (authorization.denial) {
            return std::unexpected(MakeError(ErrorCode::PolicyRejected, authorization.denial->reason));
        }
        if (authorization.replacement) {
            return std::unexpected(MakeError(ErrorCode::PolicyRejected, "A prepared deferred operation cannot "
                                                                        "be replaced during reauthorization"));
        }
        if (!authorization.deferred.empty()) {
            try {
                DeferredOperation updated = pending.operation;
                updated.requests = std::move(authorization.deferred);
                DeferredOperation deferred = updated;
                if (auto stable = pending.transaction.RebindOwners(document, presentation); !stable) {
                    return std::unexpected(std::move(stable.error()));
                }
                pending.operation = std::move(updated);
                return CommandResult{
                    .revisions = {document.ModelRevision(), presentation.PresentationRevision()},
                    .deferred = std::move(deferred),
                };
            } catch (const std::exception& exception) {
                return std::unexpected(CommandException("Deferred reauthorization storage failed", exception));
            } catch (...) {
                return std::unexpected(MakeError(ErrorCode::CommandFailed, "Deferred reauthorization storage "
                                                                           "failed with an unknown exception"));
            }
        }
    }
    if (pending.kind == Impl::PendingKind::Execute && m_impl->history_limit != 0) {
        m_impl->undo.reserve(m_impl->undo.size() + 1);
    } else if (pending.kind == Impl::PendingKind::Undo) {
        m_impl->redo.reserve(m_impl->redo.size() + 1);
    } else if (pending.kind == Impl::PendingKind::Redo) {
        m_impl->undo.reserve(m_impl->undo.size() + 1);
    }
    auto committed = pending.transaction.Commit();
    if (!committed) return committed;

    const auto kind = pending.kind;
    const auto registry_source = pending.registry_source;
    auto command = std::move(pending.command);
    m_impl->pending.reset();
    if (kind == Impl::PendingKind::Execute) {
        if (!committed->model_changed && !committed->presentation_changed) {
            if (!m_impl->undo.empty()) (void)m_impl->undo.back()->TryMerge(*command);
            return committed;
        }
        m_impl->redo.clear();
        if (m_impl->history_limit == 0) {
            m_impl->ResetBinding();
            return committed;
        }
        m_impl->Bind(document.Identity(), presentation.Identity(), committed->revisions, registry_source);
        if (!m_impl->undo.empty() && m_impl->undo.back()->TryMerge(*command)) return committed;
        m_impl->undo.push_back(std::move(command));
        if (m_impl->undo.size() > m_impl->history_limit) {
            m_impl->undo.erase(m_impl->undo.begin(),
                               m_impl->undo.begin() +
                                   static_cast<std::ptrdiff_t>(m_impl->undo.size() - m_impl->history_limit));
        }
        return committed;
    }
    if (kind == Impl::PendingKind::Undo) {
        m_impl->redo.push_back(std::move(m_impl->undo.back()));
        m_impl->undo.pop_back();
    } else {
        m_impl->undo.push_back(std::move(m_impl->redo.back()));
        m_impl->redo.pop_back();
    }
    m_impl->Bind(document.Identity(), presentation.Identity(), committed->revisions, registry_source);
    return committed;
}

Result<void> CommandStack::Cancel(const DeferredOperationId operation) {
    if (m_impl->busy) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Command stack is already executing"));
    }
    if (!m_impl->pending || m_impl->pending->operation.id != operation) {
        return std::unexpected(
            MakeError(ErrorCode::DeferredOperationNotFound, "Deferred operation is not pending on this command stack"));
    }
    m_impl->pending.reset();
    return {};
}

void CommandStack::Clear() noexcept {
    if (m_impl->busy || m_impl->pending) return;
    m_impl->undo.clear();
    m_impl->redo.clear();
    m_impl->ResetBinding();
}

void CommandStack::SetHistoryLimit(const std::size_t limit) {
    if (m_impl->busy || m_impl->pending) return;
    m_impl->history_limit = limit;
    if (m_impl->undo.size() > limit) {
        m_impl->undo.erase(m_impl->undo.begin(),
                           m_impl->undo.begin() + static_cast<std::ptrdiff_t>(m_impl->undo.size() - limit));
    }
    if (m_impl->redo.size() > limit) {
        m_impl->redo.erase(m_impl->redo.begin(),
                           m_impl->redo.begin() + static_cast<std::ptrdiff_t>(m_impl->redo.size() - limit));
    }
    if (limit == 0) {
        Clear();
    } else if (m_impl->undo.empty() && m_impl->redo.empty()) {
        m_impl->ResetBinding();
    }
}

bool CommandStack::CanUndo() const noexcept {
    return !m_impl->undo.empty();
}
bool CommandStack::CanRedo() const noexcept {
    return !m_impl->redo.empty();
}
bool CommandStack::HasPending() const noexcept {
    return m_impl->pending.has_value();
}
bool CommandStack::IsBusy() const noexcept {
    return m_impl->busy;
}

bool CommandStack::BeginExclusiveOperation() noexcept {
    if (m_exclusive_operation || m_impl->busy || m_impl->pending) return false;
    m_exclusive_operation = true;
    m_impl->busy = true;
    return true;
}

void CommandStack::EndExclusiveOperation() noexcept {
    if (!m_exclusive_operation) return;
    m_impl->busy = false;
    m_exclusive_operation = false;
}

const DeferredOperation* CommandStack::PendingOperation() const noexcept {
    return m_impl->pending ? &m_impl->pending->operation : nullptr;
}

std::string_view CommandStack::UndoName() const noexcept {
    return CanUndo() ? m_impl->undo.back()->Name() : std::string_view{};
}

std::string_view CommandStack::RedoName() const noexcept {
    return CanRedo() ? m_impl->redo.back()->Name() : std::string_view{};
}

struct CompoundCommand::Impl final {
    std::string name;
    std::vector<std::unique_ptr<Command>> commands;
};

CompoundCommand::CompoundCommand(std::string name, std::vector<std::unique_ptr<Command>> commands)
    : m_impl(std::make_unique<Impl>(Impl{std::move(name), std::move(commands)})) {}

CompoundCommand::~CompoundCommand() = default;

std::string_view CompoundCommand::Name() const noexcept {
    return m_impl->name;
}

Result<void> CompoundCommand::Apply(GraphTransaction& transaction, const RegistrySnapshot& registry) {
    if (m_impl->commands.empty() ||
        std::ranges::any_of(m_impl->commands, [](const auto& command) { return !command; })) {
        return std::unexpected(
            MakeError(ErrorCode::InvalidArgument, "Compound command cannot be empty or contain null commands"));
    }
    for (std::size_t index = 0; index < m_impl->commands.size(); ++index) {
        auto& command = m_impl->commands[index];
        transaction.PushCommandScope(command->Name(), index);
        auto result = command->Apply(transaction, registry);
        transaction.PopCommandScope();
        if (!result) {
            return result;
        }
    }
    return {};
}

Result<void> CompoundCommand::Revert(GraphTransaction& transaction) {
    for (std::size_t index = m_impl->commands.size(); index > 0; --index) {
        auto& command = m_impl->commands[index - 1];
        transaction.PushCommandScope(command->Name(), index - 1);
        auto result = command->Revert(transaction);
        transaction.PopCommandScope();
        if (!result) {
            return result;
        }
    }
    return {};
}

} // namespace Uni::GUI::Nodes
