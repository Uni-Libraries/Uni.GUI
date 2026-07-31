#include "ui_dock_layout.h"

#include <SDL3/SDL_filesystem.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Uni::GUI::Detail {

namespace {

constexpr std::array<char, 8> SettingsMagic{'U', 'N', 'I', 'G', 'U', 'I', '2', '\0'};
constexpr std::uint32_t SettingsVersion = 1;
constexpr std::size_t MaxSettingsBytes = 16U * 1024U * 1024U;

void AppendU32(std::vector<char>& output, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

void AppendString(std::vector<char>& output, const std::string_view value) {
    AppendU32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

[[nodiscard]] bool ReadU32(const std::span<const char> input, std::size_t& offset, std::uint32_t& value) {
    if (offset > input.size() || input.size() - offset < 4) {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(input[offset++])) << shift;
    }
    return true;
}

[[nodiscard]] bool ReadString(const std::span<const char> input, std::size_t& offset, std::string& value) {
    std::uint32_t size = 0;
    if (!ReadU32(input, offset, size) || offset > input.size() || size > input.size() - offset) {
        return false;
    }
    value.assign(input.data() + offset, size);
    offset += size;
    return true;
}

[[nodiscard]] ImGuiDir ToImGuiDirection(const UiDockSide side) {
    switch (side) {
    case UiDockSide::Left: return ImGuiDir_Left;
    case UiDockSide::Right: return ImGuiDir_Right;
    case UiDockSide::Up: return ImGuiDir_Up;
    case UiDockSide::Down: return ImGuiDir_Down;
    }
    return ImGuiDir_None;
}

} // namespace

UiResult<void> UiDockLayoutManager::Configure(
    UiDockingConfig docking,
    UiPersistenceConfig persistence) {
    Reset();
    if (docking.enabled && docking.dockspace_id.empty()) {
        return std::unexpected(UiError{UiErrorCode::LayoutInvalid, "Dockspace ID must not be empty"});
    }
    if (persistence.enabled && persistence.path.empty()) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Persistence path must not be empty"});
    }
    if (persistence.save_debounce.count() < 0 ||
        std::chrono::duration<long double>{persistence.save_debounce} >=
            std::chrono::duration<long double>{std::chrono::steady_clock::duration::max()}) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Persistence debounce must not be negative"});
    }
    if (!docking.enabled && (!docking.layouts.empty() || !docking.initial_layout.empty())) {
        return std::unexpected(UiError{
            UiErrorCode::InvalidArgument,
            "Docking layouts and initial layout require docking to be enabled",
        });
    }

    m_docking = std::move(docking);
    m_persistence = std::move(persistence);
    for (auto& layout : m_docking.layouts) {
        if (auto defined = Define(std::move(layout)); !defined) {
            return defined;
        }
    }
    m_docking.layouts.clear();

    if (!m_docking.initial_layout.empty()) {
        if (!m_layouts.contains(m_docking.initial_layout)) {
            return std::unexpected(UiError{UiErrorCode::LayoutNotFound, "Initial docking layout is not defined"});
        }
        m_pending_activation = PendingActivation{m_docking.initial_layout, UiDockApplyMode::RestoreOrBuild};
    }
    return {};
}

UiResult<void> UiDockLayoutManager::Define(UiDockLayout layout) {
    if (!m_docking.enabled) {
        return std::unexpected(UiError{UiErrorCode::InvalidState, "Docking is disabled"});
    }
    if (auto validated = Validate(layout); !validated) {
        return validated;
    }
    m_layouts.insert_or_assign(layout.id, std::move(layout));
    return {};
}

UiResult<bool> UiDockLayoutManager::Remove(const std::string_view id) {
    const auto iterator = m_layouts.find(std::string{id});
    if (iterator == m_layouts.end()) {
        return false;
    }
    m_layouts.erase(iterator);
    m_saved_ini.erase(std::string{id});
    if (m_active == id) {
        m_active.clear();
        m_build_pending = false;
    }
    if (m_pending_activation && m_pending_activation->id == id) {
        m_pending_activation.reset();
    }
    m_dirty = true;
    m_dirty_since = std::chrono::steady_clock::now();
    return true;
}

UiResult<void> UiDockLayoutManager::Activate(const std::string_view id, const UiDockApplyMode mode) {
    if (!m_docking.enabled) {
        return std::unexpected(UiError{UiErrorCode::InvalidState, "Docking is disabled"});
    }
    if (!m_layouts.contains(std::string{id})) {
        return std::unexpected(UiError{UiErrorCode::LayoutNotFound, "Docking layout is not defined"});
    }
    if (mode != UiDockApplyMode::RestoreOrBuild && mode != UiDockApplyMode::ResetToDefinition) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Docking layout apply mode is invalid"});
    }
    m_pending_activation = PendingActivation{std::string{id}, mode};
    return {};
}

std::string UiDockLayoutManager::Active() const {
    return m_active;
}

UiResult<void> UiDockLayoutManager::Load() {
    if (!m_persistence.enabled) {
        return {};
    }
    m_auto_save_suppressed = true;

    std::error_code error;
    if (!std::filesystem::exists(m_persistence.path, error)) {
        if (error) {
            return std::unexpected(UiError{UiErrorCode::PersistenceLoad, "Failed to inspect settings file"});
        }
        m_auto_save_suppressed = false;
        return {};
    }
    const auto file_size = std::filesystem::file_size(m_persistence.path, error);
    if (error || file_size > MaxSettingsBytes) {
        return std::unexpected(UiError{UiErrorCode::PersistenceLoad, "Settings file is inaccessible or too large"});
    }

    std::ifstream input(m_persistence.path, std::ios::binary);
    if (!input) {
        return std::unexpected(UiError{UiErrorCode::PersistenceLoad, "Failed to open settings file"});
    }
    std::vector<char> bytes(static_cast<std::size_t>(file_size));
    if (!bytes.empty() && !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        return std::unexpected(UiError{UiErrorCode::PersistenceLoad, "Failed to read settings file"});
    }

    const std::span<const char> data(bytes);
    if (data.size() < SettingsMagic.size() || !std::equal(SettingsMagic.begin(), SettingsMagic.end(), data.begin())) {
        return std::unexpected(UiError{UiErrorCode::PersistenceFormat, "Settings file magic is invalid"});
    }
    std::size_t offset = SettingsMagic.size();
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    std::string active;
    if (!ReadU32(data, offset, version) ||
        version != SettingsVersion ||
        !ReadString(data, offset, active) ||
        !ReadU32(data, offset, count) ||
        count > 1024) {
        return std::unexpected(UiError{UiErrorCode::PersistenceFormat, "Settings header is invalid"});
    }

    std::unordered_map<std::string, std::string> loaded;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::string id;
        std::string ini;
        if (!ReadString(data, offset, id) || !ReadString(data, offset, ini) || id.empty()) {
            return std::unexpected(UiError{UiErrorCode::PersistenceFormat, "Settings layout record is invalid"});
        }
        if (!loaded.emplace(std::move(id), std::move(ini)).second) {
            return std::unexpected(UiError{UiErrorCode::PersistenceFormat, "Settings file contains a duplicate layout record"});
        }
    }
    if (offset != data.size()) {
        return std::unexpected(UiError{UiErrorCode::PersistenceFormat, "Settings file contains trailing data"});
    }

    m_saved_ini = std::move(loaded);
    if (!active.empty() && m_layouts.contains(active)) {
        m_active.clear();
        m_pending_activation = PendingActivation{active, UiDockApplyMode::RestoreOrBuild};
    }
    m_auto_save_suppressed = false;
    return {};
}

UiResult<void> UiDockLayoutManager::SaveNow(const bool explicit_overwrite) {
    if (!m_persistence.enabled) {
        return {};
    }
    if (m_auto_save_suppressed && !explicit_overwrite) {
        return {};
    }
    CaptureActive();

    if (m_saved_ini.size() > 1024 ||
        m_active.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(UiError{UiErrorCode::PersistenceSave, "Settings metadata exceeds the format limit"});
    }
    std::vector<std::pair<std::string_view, std::string_view>> records;
    records.reserve(m_saved_ini.size());
    for (const auto& [id, ini] : m_saved_ini) {
        if (id.size() > std::numeric_limits<std::uint32_t>::max() ||
            ini.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(UiError{UiErrorCode::PersistenceSave, "Settings record exceeds the format limit"});
        }
        records.emplace_back(id, ini);
    }
    std::ranges::sort(records, {}, &std::pair<std::string_view, std::string_view>::first);

    std::vector<char> output;
    output.reserve(1024);
    output.insert(output.end(), SettingsMagic.begin(), SettingsMagic.end());
    AppendU32(output, SettingsVersion);
    AppendString(output, m_active);
    AppendU32(output, static_cast<std::uint32_t>(records.size()));
    for (const auto& [id, ini] : records) {
        AppendString(output, id);
        AppendString(output, ini);
    }
    if (output.size() > MaxSettingsBytes) {
        return std::unexpected(UiError{UiErrorCode::PersistenceSave, "Serialized settings exceed the size limit"});
    }

    const std::string temporary = m_persistence.path + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream || !stream.write(output.data(), static_cast<std::streamsize>(output.size()))) {
            return std::unexpected(UiError{UiErrorCode::PersistenceSave, "Failed to write temporary settings file"});
        }
        stream.flush();
        if (!stream) {
            return std::unexpected(UiError{UiErrorCode::PersistenceSave, "Failed to flush temporary settings file"});
        }
    }
    if (!SDL_RenamePath(temporary.c_str(), m_persistence.path.c_str())) {
        std::error_code remove_error;
        std::filesystem::remove(temporary, remove_error);
        return std::unexpected(UiError{UiErrorCode::PersistenceSave, "Failed to atomically replace settings file"});
    }

    m_dirty = false;
    m_auto_save_suppressed = false;
    ImGui::GetIO().WantSaveIniSettings = false;
    return {};
}

UiResult<void> UiDockLayoutManager::Reload() {
    return Load();
}

UiResult<void> UiDockLayoutManager::PrepareBeforeFrame() {
    if (!m_pending_activation) {
        return {};
    }

    CaptureActive();
    const PendingActivation activation = std::move(*m_pending_activation);
    m_pending_activation.reset();
    ImGui::ClearIniSettings();

    const auto saved = m_saved_ini.find(activation.id);
    if (activation.mode == UiDockApplyMode::RestoreOrBuild && saved != m_saved_ini.end()) {
        ImGui::LoadIniSettingsFromMemory(saved->second.data(), saved->second.size());
        m_build_pending = false;
    } else {
        m_build_pending = true;
    }
    m_active = activation.id;
    m_dirty = true;
    m_dirty_since = std::chrono::steady_clock::now();
    return {};
}

UiResult<void> UiDockLayoutManager::SubmitDockspace() {
    if (!m_docking.enabled) {
        return {};
    }
    const ImGuiID dockspace_id = ImHashStr(m_docking.dockspace_id.c_str());
    if (m_build_pending && !m_active.empty()) {
        const auto iterator = m_layouts.find(m_active);
        if (iterator == m_layouts.end()) {
            return std::unexpected(UiError{UiErrorCode::LayoutNotFound, "Active docking layout is not defined"});
        }
        if (auto applied = ApplyDefinition(iterator->second); !applied) {
            return applied;
        }
        m_build_pending = false;
    }
    ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport());
    return {};
}

UiResult<void> UiDockLayoutManager::MaintainAfterFrame(const std::chrono::steady_clock::time_point now) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantSaveIniSettings && !m_dirty) {
        m_dirty = true;
        m_dirty_since = now;
    }
    if (m_dirty && m_persistence.enabled && !m_auto_save_suppressed &&
        now - m_dirty_since >= m_persistence.save_debounce) {
        return SaveNow();
    }
    return {};
}

bool UiDockLayoutManager::HasPendingWork() const noexcept {
    return m_pending_activation.has_value() ||
           m_build_pending ||
           (m_persistence.enabled && m_dirty && !m_auto_save_suppressed);
}

void UiDockLayoutManager::Reset() noexcept {
    m_docking = {};
    m_persistence = {};
    m_layouts.clear();
    m_saved_ini.clear();
    m_active.clear();
    m_pending_activation.reset();
    m_build_pending = false;
    m_dirty = false;
    m_auto_save_suppressed = false;
    m_dirty_since = {};
}

UiResult<void> UiDockLayoutManager::Validate(const UiDockLayout& layout) {
    if (layout.id.empty()) {
        return std::unexpected(UiError{UiErrorCode::LayoutInvalid, "Docking layout ID must not be empty"});
    }
    std::unordered_set<std::string> leaves{"root"};
    std::unordered_set<std::string> all_names{"root"};
    for (const auto& split : layout.splits) {
        if (!leaves.contains(split.leaf) ||
            split.side_leaf.empty() ||
            split.remainder_leaf.empty() ||
            split.side_leaf == split.remainder_leaf ||
            all_names.contains(split.side_leaf) ||
            all_names.contains(split.remainder_leaf) ||
            !std::isfinite(split.fraction) ||
            split.fraction < 0.05f ||
            split.fraction > 0.95f ||
            ToImGuiDirection(split.side) == ImGuiDir_None) {
            return std::unexpected(UiError{UiErrorCode::LayoutInvalid, "Docking split graph is invalid"});
        }
        leaves.erase(split.leaf);
        leaves.insert(split.side_leaf);
        leaves.insert(split.remainder_leaf);
        all_names.insert(split.side_leaf);
        all_names.insert(split.remainder_leaf);
    }
    std::unordered_set<std::string> windows;
    for (const auto& placement : layout.placements) {
        if (placement.window_name.empty() ||
            !leaves.contains(placement.leaf) ||
            !windows.insert(placement.window_name).second) {
            return std::unexpected(UiError{UiErrorCode::LayoutInvalid, "Docking placement references an invalid leaf"});
        }
    }
    return {};
}

UiResult<void> UiDockLayoutManager::ApplyDefinition(const UiDockLayout& layout) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return std::unexpected(UiError{UiErrorCode::LayoutApplyFailed, "Main viewport is unavailable"});
    }

    const ImGuiID root_id = ImHashStr(m_docking.dockspace_id.c_str());
    ImGui::DockBuilderRemoveNode(root_id);
    ImGui::DockBuilderAddNode(root_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(root_id, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(root_id, viewport->WorkSize);

    std::unordered_map<std::string, ImGuiID> leaves{{"root", root_id}};
    for (const auto& split : layout.splits) {
        const auto iterator = leaves.find(split.leaf);
        if (iterator == leaves.end()) {
            return std::unexpected(UiError{UiErrorCode::LayoutApplyFailed, "Docking split leaf disappeared during application"});
        }
        ImGuiID side_id = 0;
        ImGuiID remainder_id = 0;
        ImGui::DockBuilderSplitNode(
            iterator->second,
            ToImGuiDirection(split.side),
            split.fraction,
            &side_id,
            &remainder_id);
        leaves.erase(iterator);
        leaves.emplace(split.side_leaf, side_id);
        leaves.emplace(split.remainder_leaf, remainder_id);
    }
    for (const auto& placement : layout.placements) {
        ImGui::DockBuilderDockWindow(placement.window_name.c_str(), leaves.at(placement.leaf));
    }
    ImGui::DockBuilderFinish(root_id);
    return {};
}

void UiDockLayoutManager::CaptureActive() {
    if (m_active.empty() || ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    std::size_t size = 0;
    const char* data = ImGui::SaveIniSettingsToMemory(&size);
    m_saved_ini.insert_or_assign(m_active, std::string{data, size});
}

} // namespace Uni::GUI::Detail
