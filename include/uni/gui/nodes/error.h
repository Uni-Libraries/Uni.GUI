#pragma once

#include <expected>
#include <string>

namespace Uni::GUI::Nodes {

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
    RevisionConflict,
    NothingToUndo,
    NothingToRedo,
    InvalidFormat,
    UnsupportedVersion,
    SizeLimitExceeded,
    MigrationMissing,
    MigrationFailed,
    IoRead,
    IoWrite,
    ClipboardUnavailable,
    ReadOnly,
    Locked,
    PolicyRejected,
    AssetNotFound,
    AssetInUse,
    TypeInUse,
    RegistryMismatch,
    GenerationOverflow,
    OperationPending,
    DeferredOperationNotFound,
};

struct Error final {
    ErrorCode code{ErrorCode::InvalidArgument};
    std::string message;
};

template <typename T> using Result = std::expected<T, Error>;

} // namespace Uni::GUI::Nodes
