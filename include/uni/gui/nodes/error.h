#pragma once

/// @file
/// @brief Structured results shared by semantic, persistence and editor operations.

#include <expected>
#include <string>

namespace Uni::GUI::Nodes {

/// Machine-readable failure category. Use the code for control flow, not the diagnostic wording.
enum class ErrorCode {
    InvalidArgument,
    DuplicateId,
    GraphNotFound,
    NodeNotFound,
    PinNotFound,
    LinkNotFound,
    TypeNotFound,
    InvalidDirection,
    IncompatiblePins,
    CardinalityExceeded,
    InvalidGraph,
    CommandFailed,
    RevisionConflict, ///< The state used for preparation or history is no longer current.
    NothingToUndo,
    NothingToRedo,
    InvalidFormat, ///< Malformed serialized content, as distinct from an unsupported version.
    UnsupportedVersion,
    SizeLimitExceeded,
    MigrationMissing,
    MigrationFailed,
    IoRead,
    IoWrite,
    ClipboardUnavailable,
    ReadOnly, ///< Persisted semantic protection prevents the edit.
    Locked, ///< Presentation protection prevents the edit.
    PolicyRejected, ///< Application authorization denied the operation or its replacement.
    AssetNotFound,
    AssetInUse,
    TypeInUse,
    RegistryMismatch, ///< The supplied catalog or registration belongs to a different owner.
    GenerationOverflow,
    OperationPending, ///< Resolve a retained authorization before starting this operation.
    DeferredOperationNotFound, ///< The ID does not identify the currently retained operation.
};

/// Owning diagnostic; the message can be displayed or logged after its producer returns.
struct Error final {
    ErrorCode code{ErrorCode::InvalidArgument};
    std::string message;
};

/// Contains the returned value (or simply success for void), otherwise an Error accessible via error().
template <typename T> using Result = std::expected<T, Error>;

} // namespace Uni::GUI::Nodes
