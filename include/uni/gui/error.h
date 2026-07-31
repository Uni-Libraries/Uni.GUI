#pragma once

#include <expected>
#include <string>

namespace Uni::GUI {

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

struct UiError final {
    UiErrorCode code{UiErrorCode::InvalidState};
    std::string message;
};

template<typename T>
using UiResult = std::expected<T, UiError>;

} // namespace Uni::GUI
