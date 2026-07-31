#pragma once

#include <uni/gui/config.h>
#include <uni/gui/error.h>
#include <uni/gui/layout.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Uni::GUI::Detail {

class UiDockLayoutManager final {
public:
    [[nodiscard]] UiResult<void> Configure(
        UiDockingConfig docking,
        UiPersistenceConfig persistence);

    [[nodiscard]] UiResult<void> Define(UiDockLayout layout);
    [[nodiscard]] UiResult<bool> Remove(std::string_view id);
    [[nodiscard]] UiResult<void> Activate(std::string_view id, UiDockApplyMode mode);
    [[nodiscard]] std::string Active() const;

    [[nodiscard]] UiResult<void> Load();
    [[nodiscard]] UiResult<void> SaveNow(bool explicit_overwrite = false);
    [[nodiscard]] UiResult<void> Reload();
    [[nodiscard]] UiResult<void> PrepareBeforeFrame();
    [[nodiscard]] UiResult<void> SubmitDockspace();
    [[nodiscard]] UiResult<void> MaintainAfterFrame(std::chrono::steady_clock::time_point now);

    [[nodiscard]] bool HasPendingWork() const noexcept;
    void Reset() noexcept;

private:
    struct PendingActivation final {
        std::string id;
        UiDockApplyMode mode{UiDockApplyMode::RestoreOrBuild};
    };

    [[nodiscard]] static UiResult<void> Validate(const UiDockLayout& layout);
    [[nodiscard]] UiResult<void> ApplyDefinition(const UiDockLayout& layout);
    void CaptureActive();

    UiDockingConfig m_docking;
    UiPersistenceConfig m_persistence;
    std::unordered_map<std::string, UiDockLayout> m_layouts;
    std::unordered_map<std::string, std::string> m_saved_ini;
    std::string m_active;
    std::optional<PendingActivation> m_pending_activation;
    bool m_build_pending{};
    bool m_dirty{};
    bool m_auto_save_suppressed{};
    std::chrono::steady_clock::time_point m_dirty_since{};
};

} // namespace Uni::GUI::Detail
