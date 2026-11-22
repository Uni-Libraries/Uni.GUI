#pragma once

#include <expected>
#include <string>

/// Public application, resource and event-loop interfaces.
namespace Uni::GUI {

/// Machine-readable failure category. Use this rather than parsing diagnostic text.
enum class UiErrorCode {
    InvalidState,
    InvalidArgument,
    WrongThread,
    WouldDeadlock,
    Cancelled,
    SdlInitialization,
    WindowCreation,
    RendererUnavailable,
    RendererInitialization,
    BackendInitialization,
    FontInitialization,
    FrameRendering,
    EventHandling,
    TextureCreation,
    QueueClosed,
    QueueFull,
    WakeupFailed,
    CommandFailed,
    LayoutInvalid,
    LayoutNotFound,
    LayoutApplyFailed,
    PersistenceLoad,
    PersistenceSave,
    PersistenceFormat,
    AssetNotFound,
    AssetIo,
    AssetTooLarge,
    FontNotFound,
};

/// Error returned by a checked operation, with an owning diagnostic string.
struct UiError final {
    UiErrorCode code{UiErrorCode::InvalidState};
    std::string message;
};

/// Result containing the requested value (or void on success) or a UiError on failure.
template<typename T>
using UiResult = std::expected<T, UiError>;

} // namespace Uni::GUI
