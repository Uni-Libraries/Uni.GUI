#pragma once

/// @file
/// @brief UI-thread application lifecycle and resource operations.

#include <uni/gui/asset.h>
#include <uni/gui/config.h>
#include <uni/gui/dispatcher.h>
#include <uni/gui/element.h>
#include <uni/gui/error.h>
#include <uni/gui/event.h>
#include <uni/gui/export.h>
#include <uni/gui/frame.h>
#include <uni/gui/layout.h>
#include <uni/gui/texture.h>

#include <memory>
#include <string>
#include <string_view>

namespace Uni::GUI {

/// State of a single UiApp instance. Shutdown returns it to Empty for reuse.
enum class UiLifecycleState {
    Empty,
    Initializing,
    Ready,
    ExitRequested,
    Failed,
    ShuttingDown,
};

/// Owns the main window, backend, ImGui/ImPlot contexts and registered UI objects.
/// @par Threading
/// Initialize, use and destroy the app on its UI thread. Obtain a UiDispatcher
/// there and give copies to workers instead of sharing the app. Destruction of
/// a non-empty app on another thread terminates the process.
class UNI_GUI_EXPORT UiApp final {
public:
    UiApp();
    ~UiApp();
    UiApp(const UiApp&) = delete;
    UiApp& operator=(const UiApp&) = delete;
    UiApp(UiApp&&) = delete;
    UiApp& operator=(UiApp&&) = delete;

    /// Initialize an empty app. After an initialization failure in state Failed, call Shutdown() before retrying.
    [[nodiscard]] UiResult<void> Initialize(UiAppConfig config);
    /// Cancel pending commands and release owned resources. A save error is reported after cleanup.
    [[nodiscard]] UiResult<void> Shutdown();

    /// Drain queued work and render if needed. The result includes the suggested rate for the next iteration.
    [[nodiscard]] UiResult<UiTickResult> Tick();
    /// Deliver an SDL event to the current hooks and backend; internal dispatcher wakes bypass user hooks.
    [[nodiscard]] UiResult<UiEventDispatchResult> DispatchEvent(const SDL_Event& event);
    /// Replace both hooks. Not allowed from inside an event hook.
    [[nodiscard]] UiResult<void> SetEventHooks(UiEventHooks hooks);
    /// Obtain a worker-safe handle to this initialization's queue; it does not keep the app alive.
    [[nodiscard]] UiDispatcher Dispatcher() const noexcept;

    /// Take ownership of a non-null element and request a redraw, including in WaitForEvent mode.
    /// An addition during Update starts on the next iteration.
    [[nodiscard]] UiResult<UiElementId> AddElement(std::unique_ptr<UiElement> element);
    /// Request removal. Returns false for an unknown or already removed/marked ID; zero is InvalidArgument.
    [[nodiscard]] UiResult<bool> RemoveElement(UiElementId id);

    /// Create a managed RGBA32 texture with positive dimensions representable by the backend descriptor.
    [[nodiscard]] UiResult<UiTexture> CreateTexture(int width, int height);
    /// Change the presentation interval; a mode change is unsupported with SDL_GPU platform viewports enabled.
    [[nodiscard]] UiResult<void> SetVsync(UiVsyncMode mode);
    /// Set the SDL main window title.
    [[nodiscard]] UiResult<void> SetWindowTitle(std::string title);
    /// Return a copy of the latest main-window metrics, not a live renderer query.
    [[nodiscard]] UiResult<UiDisplayMetrics> DisplayMetrics() const;
    /// Set the scale multiplier outside the ImGui frame (for example, in a posted command).
    [[nodiscard]] UiResult<void> SetUserScale(float scale);
    /// Copy an unscaled style outside the ImGui frame; the configured base font size remains in effect.
    [[nodiscard]] UiResult<void> SetReferenceStyle(const ImGuiStyle& style);

    /// Add or replace a validated definition without activating it or discarding its stored snapshot.
    [[nodiscard]] UiResult<void> DefineDockLayout(UiDockLayout layout);
    /// Delete the definition and snapshot, cancelling activation of that ID; returns false if not defined.
    [[nodiscard]] UiResult<bool> RemoveDockLayout(std::string_view id);
    /// Queue activation for a rendered frame. A later request replaces the pending request.
    [[nodiscard]] UiResult<void> ActivateDockLayout(
        std::string_view id,
        UiDockApplyMode mode = UiDockApplyMode::RestoreOrBuild);
    /// Return the applied layout ID; a selection loaded from disk is not active until its frame is prepared.
    [[nodiscard]] std::string ActiveDockLayout() const;
    /// Save outside the ImGui frame. Saves before a loaded layout is applied preserve its ID and snapshot.
    /// An explicit save may replace a file whose load failed. See UiPersistenceConfig.
    [[nodiscard]] UiResult<void> SaveSettingsNow();
    /// Read settings outside the ImGui frame. A failed load keeps the last snapshots and suppresses automatic saves.
    [[nodiscard]] UiResult<void> ReloadSettings();

    /// Snapshot asset data outside the ImGui frame. Replacing a key preserves its ID and rebuilds dependent fonts.
    [[nodiscard]] UiResult<UiAssetInfo> UpsertAsset(UiAssetSpec asset);
    /// Look up a registered key; reports AssetNotFound if absent.
    [[nodiscard]] UiResult<UiAssetInfo> FindAsset(std::string_view key) const;
    /// Remove an asset outside the ImGui frame; returns false if absent, InvalidState if a font still uses it.
    [[nodiscard]] UiResult<bool> RemoveAsset(UiAssetId id);
    /// Register a runtime font from a registered asset outside the ImGui frame.
    [[nodiscard]] UiResult<UiFontId> CreateFont(UiFontSpec font);
    /// Select a registered default font outside the ImGui frame.
    [[nodiscard]] UiResult<void> SetDefaultFont(UiFontId id);
    /// Remove a runtime font outside the ImGui frame. Removing the default restores the fallback font.
    [[nodiscard]] UiResult<bool> RemoveFont(UiFontId id);
    /// Borrow an atlas font. Resolve the ID again after replacing its asset; removal or shutdown also invalidates the pointer.
    [[nodiscard]] UiResult<ImFont*> GetFont(UiFontId id) const;

    /// Inspect the lifecycle state on the UI thread.
    [[nodiscard]] UiLifecycleState State() const noexcept;
    /// Whether the calling thread is the recorded UI thread (false before initialization).
    [[nodiscard]] bool IsMainThread() const noexcept;
    /// Borrow the renderer's name while Ready; returns an empty view if unavailable.
    [[nodiscard]] std::string_view RendererName() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Uni::GUI
