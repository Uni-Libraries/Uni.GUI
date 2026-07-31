#include "nodes/commands/transaction_internal.h"

namespace Uni::GUI::Nodes {

using CommandDetail::MakeError;

GraphTransaction::GraphTransaction(GraphDocument& document, GraphPresentation& presentation, RegistrySnapshot registry,
                                   const bool enforce_protection,
                                   const bool record_operations, const std::size_t max_operations)
    : m_impl(std::make_unique<Impl>(Impl{
          .document = &document,
          .presentation = &presentation,
          .baseline_document = document.SnapshotForTransaction(),
          .baseline_presentation = presentation.SnapshotForTransaction(),
          .staged_document = document.SnapshotForTransaction(),
          .staged_presentation = presentation.SnapshotForTransaction(),
          .expected = {document.ModelRevision(), presentation.PresentationRevision()},
          .document_identity = document.Identity(),
          .presentation_identity = presentation.Identity(),
          .document_allocation_epoch = document.AllocationEpoch(),
          .presentation_allocation_epoch = presentation.AllocationEpoch(),
          .registry = std::move(registry),
          .enforce_protection = enforce_protection,
          .record_operations = record_operations,
          .max_operations = max_operations,
      })) {}

GraphTransaction::~GraphTransaction() = default;

GraphTransaction::GraphTransaction(GraphTransaction&& other) : m_impl(std::move(other.m_impl)) {}

GraphTransaction& GraphTransaction::operator=(GraphTransaction&& other) {
    if (this != &other) {
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

const GraphDocument& GraphTransaction::Document() const noexcept { return m_impl->staged_document; }

const GraphPresentation& GraphTransaction::Presentation() const noexcept { return m_impl->staged_presentation; }

const std::vector<OperationIntent>& GraphTransaction::Operations() const noexcept { return m_impl->operations; }

const GraphDocument& GraphTransaction::BaselineDocument() const noexcept { return m_impl->baseline_document; }

const GraphPresentation& GraphTransaction::BaselinePresentation() const noexcept {
    return m_impl->baseline_presentation;
}

const RegistrySnapshot& GraphTransaction::Registry() const noexcept { return m_impl->registry; }

bool GraphTransaction::OperationLimitExceeded() const noexcept { return m_impl->operation_limit_exceeded; }

GraphId GraphTransaction::AllocateGraphId() noexcept { return m_impl->staged_document.AllocateGraphId(); }

NodeId GraphTransaction::AllocateNodeId() noexcept { return m_impl->staged_document.AllocateNodeId(); }

PinId GraphTransaction::AllocatePinId() noexcept { return m_impl->staged_document.AllocatePinId(); }

LinkId GraphTransaction::AllocateLinkId() noexcept { return m_impl->staged_document.AllocateLinkId(); }

IntergraphLinkId GraphTransaction::AllocateIntergraphLinkId() noexcept {
    return m_impl->staged_document.AllocateIntergraphLinkId();
}

Result<void> GraphTransaction::RebindOwners(GraphDocument& document, GraphPresentation& presentation) {
    if (document.Identity() != m_impl->document_identity || presentation.Identity() != m_impl->presentation_identity) {
        return std::unexpected(MakeError(ErrorCode::CommandFailed, "Transaction owners have different identities"));
    }
    if (document.ModelRevision() != m_impl->expected.model ||
        presentation.PresentationRevision() != m_impl->expected.presentation ||
        document.AllocationEpoch() != m_impl->document_allocation_epoch ||
        presentation.AllocationEpoch() != m_impl->presentation_allocation_epoch) {
        return std::unexpected(MakeError(ErrorCode::RevisionConflict, "Transaction owners changed before rebinding"));
    }
    m_impl->document = &document;
    m_impl->presentation = &presentation;
    return {};
}

} // namespace Uni::GUI::Nodes
