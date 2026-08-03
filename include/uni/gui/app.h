#pragma once

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

enum class UiLifecycleState {
    Empty,
    Initializing,
    Ready,
    ExitRequested,
    Failed,
    ShuttingDown,
};

class UNI_GUI_EXPORT UiApp final {
public:
    UiApp();
    ~UiApp();
    UiApp(const UiApp&) = delete;
    UiApp& operator=(const UiApp&) = delete;
    UiApp(UiApp&&) = delete;
    UiApp& operator=(UiApp&&) = delete;

    [[nodiscard]] UiResult<void> Initialize(UiAppConfig config);
    [[nodiscard]] UiResult<void> Shutdown();

    [[nodiscard]] UiResult<UiTickResult> Tick();
    [[nodiscard]] UiResult<UiEventDispatchResult> DispatchEvent(const SDL_Event& event);
    [[nodiscard]] UiResult<void> SetEventHooks(UiEventHooks hooks);
    [[nodiscard]] UiDispatcher Dispatcher() const noexcept;

    [[nodiscard]] UiResult<UiElementId> AddElement(std::unique_ptr<UiElement> element);
    [[nodiscard]] UiResult<bool> RemoveElement(UiElementId id);

    [[nodiscard]] UiResult<UiTexture> CreateTexture(int width, int height);
    [[nodiscard]] UiResult<void> SetVsync(UiVsyncMode mode);
    [[nodiscard]] UiResult<void> SetWindowTitle(std::string title);
    [[nodiscard]] UiResult<UiDisplayMetrics> DisplayMetrics() const;
    [[nodiscard]] UiResult<void> SetUserScale(float scale);
    [[nodiscard]] UiResult<void> SetReferenceStyle(const ImGuiStyle& style);

    [[nodiscard]] UiResult<void> DefineDockLayout(UiDockLayout layout);
    [[nodiscard]] UiResult<bool> RemoveDockLayout(std::string_view id);
    [[nodiscard]] UiResult<void> ActivateDockLayout(
        std::string_view id,
        UiDockApplyMode mode = UiDockApplyMode::RestoreOrBuild);
    [[nodiscard]] std::string ActiveDockLayout() const;
    [[nodiscard]] UiResult<void> SaveSettingsNow();
    [[nodiscard]] UiResult<void> ReloadSettings();

    [[nodiscard]] UiResult<UiAssetInfo> UpsertAsset(UiAssetSpec asset);
    [[nodiscard]] UiResult<UiAssetInfo> FindAsset(std::string_view key) const;
    [[nodiscard]] UiResult<bool> RemoveAsset(UiAssetId id);
    [[nodiscard]] UiResult<UiFontId> CreateFont(UiFontSpec font);
    [[nodiscard]] UiResult<void> SetDefaultFont(UiFontId id);
    [[nodiscard]] UiResult<bool> RemoveFont(UiFontId id);
    // Resolve the ID again after replacing its source asset; replacement invalidates the previous pointer.
    [[nodiscard]] UiResult<ImFont*> GetFont(UiFontId id) const;

    [[nodiscard]] UiLifecycleState State() const noexcept;
    [[nodiscard]] bool IsMainThread() const noexcept;
    [[nodiscard]] std::string_view RendererName() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Uni::GUI
