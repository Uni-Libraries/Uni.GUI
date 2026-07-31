#include "ui_dock_layout.h"

#include <imgui.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Uni::GUI::UiDockApplyMode;
using Uni::GUI::UiDockLayout;
using Uni::GUI::UiDockSide;
using Uni::GUI::UiDockingConfig;
using Uni::GUI::UiErrorCode;
using Uni::GUI::UiPersistenceConfig;
using Uni::GUI::Detail::UiDockLayoutManager;

constexpr std::array<char, 8> SettingsMagic{'U', 'N', 'I', 'G', 'U', 'I', '2', '\0'};

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void AppendU32(std::vector<char>& output, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        output.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

void AppendString(std::vector<char>& output, const std::string_view value) {
    AppendU32(output, static_cast<std::uint32_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

std::vector<char> MakeSettings(
    const std::string_view active,
    const std::vector<std::pair<std::string_view, std::string_view>>& records,
    const std::uint32_t version = 1) {
    std::vector<char> output(SettingsMagic.begin(), SettingsMagic.end());
    AppendU32(output, version);
    AppendString(output, active);
    AppendU32(output, static_cast<std::uint32_t>(records.size()));
    for (const auto& [id, ini] : records) {
        AppendString(output, id);
        AppendString(output, ini);
    }
    return output;
}

void WriteBytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Expect(output.good(), "Failed to create persistence test fixture");
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    Expect(output.good(), "Failed to write persistence test fixture");
}

std::vector<char> ReadBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    Expect(input.good(), "Failed to open persistence test output");
    const auto size = input.tellg();
    Expect(size >= 0, "Failed to determine persistence test output size");
    std::vector<char> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!bytes.empty()) {
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    Expect(input.good(), "Failed to read persistence test output");
    return bytes;
}

UiDockLayout ValidLayout(std::string id = "valid") {
    UiDockLayout layout;
    layout.id = std::move(id);
    layout.splits.push_back({"root", UiDockSide::Left, 0.25f, "left", "right"});
    layout.splits.push_back({"right", UiDockSide::Down, 0.5f, "bottom", "top"});
    layout.placements.push_back({"Inspector", "left"});
    layout.placements.push_back({"Timeline", "bottom"});
    layout.placements.push_back({"Viewport", "top"});
    return layout;
}

UiDockLayoutManager MakeManager(const std::filesystem::path& path) {
    UiDockLayoutManager manager;
    UiDockingConfig docking;
    docking.enabled = false;
    UiPersistenceConfig persistence;
    persistence.enabled = true;
    persistence.path = path.string();
    Expect(manager.Configure(std::move(docking), std::move(persistence)).has_value(),
           "Persistence test manager must configure");
    return manager;
}

void ExpectFormatError(const std::filesystem::path& path, std::vector<char> bytes, const char* message) {
    WriteBytes(path, bytes);
    auto manager = MakeManager(path);
    const auto loaded = manager.Load();
    Expect(!loaded && loaded.error().code == UiErrorCode::PersistenceFormat, message);
}

void TestValidation() {
    UiDockLayoutManager manager;
    UiDockingConfig docking;
    docking.enabled = true;
    UiPersistenceConfig persistence;
    persistence.enabled = false;
    Expect(manager.Configure(docking, persistence).has_value(), "Validation manager must configure");
    Expect(manager.Define(ValidLayout()).has_value(), "Valid nested split graph must be accepted");

    UiDockLayout empty_id;
    Expect(!manager.Define(std::move(empty_id)), "Empty layout ID must be rejected");

    auto missing_leaf = ValidLayout("missing-leaf");
    missing_leaf.splits.front().leaf = "missing";
    Expect(!manager.Define(std::move(missing_leaf)), "Split source must reference a current leaf");

    auto duplicate_leaf = ValidLayout("duplicate-leaf");
    duplicate_leaf.splits.back().side_leaf = "left";
    Expect(!manager.Define(std::move(duplicate_leaf)), "Split leaf names must be unique");

    auto invalid_side = ValidLayout("invalid-side");
    invalid_side.splits.front().side = static_cast<UiDockSide>(999);
    Expect(!manager.Define(std::move(invalid_side)), "Invalid dock side must be rejected");

    auto nan_fraction = ValidLayout("nan-fraction");
    nan_fraction.splits.front().fraction = std::numeric_limits<float>::quiet_NaN();
    Expect(!manager.Define(std::move(nan_fraction)), "NaN split fraction must be rejected");

    auto infinite_fraction = ValidLayout("infinite-fraction");
    infinite_fraction.splits.front().fraction = std::numeric_limits<float>::infinity();
    Expect(!manager.Define(std::move(infinite_fraction)), "Infinite split fraction must be rejected");

    auto low_fraction = ValidLayout("low-fraction");
    low_fraction.splits.front().fraction = 0.049f;
    Expect(!manager.Define(std::move(low_fraction)), "Split fraction below the lower bound must be rejected");

    auto high_fraction = ValidLayout("high-fraction");
    high_fraction.splits.front().fraction = 0.951f;
    Expect(!manager.Define(std::move(high_fraction)), "Split fraction above the upper bound must be rejected");

    auto boundary_fractions = ValidLayout("boundary-fractions");
    boundary_fractions.splits.front().fraction = 0.05f;
    boundary_fractions.splits.back().fraction = 0.95f;
    Expect(manager.Define(std::move(boundary_fractions)).has_value(), "Split fraction boundaries must be accepted");

    auto invalid_placement = ValidLayout("invalid-placement");
    invalid_placement.placements.front().leaf = "right";
    Expect(!manager.Define(std::move(invalid_placement)), "Placement must reference a final leaf");

    auto duplicate_window = ValidLayout("duplicate-window");
    duplicate_window.placements.back().window_name = duplicate_window.placements.front().window_name;
    Expect(!manager.Define(std::move(duplicate_window)), "A window may only be placed once");

    Expect(!manager.Activate("valid", static_cast<UiDockApplyMode>(999)),
           "Invalid layout apply mode must be rejected");
    Expect(!manager.Activate("missing", UiDockApplyMode::RestoreOrBuild),
           "Unknown layout activation must be rejected");

    Expect(manager.Activate("valid", UiDockApplyMode::RestoreOrBuild).has_value(),
           "Defined layout must support pending activation");
    Expect(manager.Remove("valid").value_or(false), "Defined layout must be removable");
    Expect(!manager.HasPendingWork(), "Removing a pending layout must cancel its activation");

    Expect(manager.Define(ValidLayout("active")).has_value(), "Active layout fixture must be accepted");
    Expect(manager.Define(ValidLayout("pending")).has_value(), "Pending layout fixture must be accepted");
    Expect(manager.Activate("active", UiDockApplyMode::ResetToDefinition).has_value() &&
               manager.PrepareBeforeFrame().has_value() && manager.Active() == "active",
           "Active layout fixture must become active");
    Expect(manager.Activate("pending", UiDockApplyMode::ResetToDefinition).has_value(),
           "Unrelated pending layout must be accepted");
    Expect(manager.Remove("active").value_or(false), "Active layout fixture must be removable");
    Expect(manager.PrepareBeforeFrame().has_value() && manager.Active() == "pending",
           "Removing an active layout must preserve unrelated pending activation");

    UiDockLayoutManager invalid_config;
    UiDockingConfig empty_dockspace;
    empty_dockspace.dockspace_id.clear();
    Expect(!invalid_config.Configure(empty_dockspace, persistence), "Enabled docking requires a dockspace ID");

    UiDockLayoutManager disabled_docking_manager;
    UiDockingConfig disabled_docking;
    disabled_docking.enabled = false;
    disabled_docking.layouts.push_back(ValidLayout("disabled"));
    disabled_docking.initial_layout = "disabled";
    Expect(!disabled_docking_manager.Configure(disabled_docking, persistence),
           "Disabled docking must reject declarative layout work");
    disabled_docking.layouts.clear();
    disabled_docking.initial_layout.clear();
    Expect(disabled_docking_manager.Configure(disabled_docking, persistence).has_value(),
           "Disabled docking without layout work must configure");
    Expect(!disabled_docking_manager.Define(ValidLayout("disabled-runtime")) &&
               !disabled_docking_manager.Activate("disabled-runtime", UiDockApplyMode::RestoreOrBuild) &&
               !disabled_docking_manager.HasPendingWork(),
           "Disabled docking must reject runtime layout work without creating pending work");

    UiPersistenceConfig empty_path;
    empty_path.enabled = true;
    empty_path.path.clear();
    docking.enabled = false;
    Expect(!invalid_config.Configure(docking, empty_path), "Enabled persistence requires a path");

    UiPersistenceConfig negative_debounce;
    negative_debounce.enabled = true;
    negative_debounce.path = "unused";
    negative_debounce.save_debounce = std::chrono::milliseconds{-1};
    Expect(!invalid_config.Configure(docking, negative_debounce), "Negative save debounce must be rejected");
}

void TestPersistence(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    auto missing = MakeManager(path);
    Expect(missing.Load().has_value(), "Missing settings file must be accepted");

    ExpectFormatError(path, {}, "Empty settings file must be rejected");

    auto wrong_magic = MakeSettings("", {});
    wrong_magic.front() = 'X';
    ExpectFormatError(path, std::move(wrong_magic), "Wrong settings magic must be rejected");
    ExpectFormatError(path, MakeSettings("", {}, 2), "Unsupported settings version must be rejected");

    auto truncated = MakeSettings("", {});
    truncated.pop_back();
    ExpectFormatError(path, std::move(truncated), "Truncated settings header must be rejected");

    auto trailing = MakeSettings("", {});
    trailing.push_back('x');
    ExpectFormatError(path, std::move(trailing), "Trailing settings data must be rejected");

    ExpectFormatError(
        path,
        MakeSettings("", {{"same", "one"}, {"same", "two"}}),
        "Duplicate layout records must be rejected");

    auto excessive_count = MakeSettings("", {});
    excessive_count.resize(SettingsMagic.size() + 4 + 4);
    AppendU32(excessive_count, 1025);
    ExpectFormatError(path, std::move(excessive_count), "Excessive settings record count must be rejected");

    const auto unsorted = MakeSettings("", {{"z-layout", "z-data"}, {"a-layout", "a-data"}});
    WriteBytes(path, unsorted);
    auto manager = MakeManager(path);
    Expect(manager.Load().has_value(), "Valid multi-layout settings must load");

    WriteBytes(path, std::vector<char>{'b', 'a', 'd'});
    const auto failed_reload = manager.Reload();
    Expect(!failed_reload && failed_reload.error().code == UiErrorCode::PersistenceFormat,
           "Malformed reload must report a format error");
    Expect(manager.SaveNow().has_value(), "Automatic save must become a no-op after a failed reload");
    Expect(ReadBytes(path) == std::vector<char>({'b', 'a', 'd'}),
           "Automatic save must preserve malformed settings for diagnosis or migration");

    Expect(manager.SaveNow(true).has_value(), "Last-known-good settings must remain saveable after failed reload");
    const auto sorted = MakeSettings("", {{"a-layout", "a-data"}, {"z-layout", "z-data"}});
    Expect(ReadBytes(path) == sorted, "Settings records must survive failed reload and serialize deterministically");
    Expect(!std::filesystem::exists(path.string() + ".tmp"), "Atomic save must not leave a temporary file");

    std::vector<char> oversized(16U * 1024U * 1024U + 1U, '\0');
    WriteBytes(path, oversized);
    auto oversized_manager = MakeManager(path);
    const auto oversized_result = oversized_manager.Load();
    Expect(!oversized_result && oversized_result.error().code == UiErrorCode::PersistenceLoad,
           "Oversized settings file must be rejected before parsing");
}

} // namespace

int main() {
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    TestValidation();
    const auto path = std::filesystem::temp_directory_path() /
        ("unigui-layout-unit-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".settings");
    TestPersistence(path);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + ".tmp", ignored);
    ImGui::DestroyContext();
    return EXIT_SUCCESS;
}
