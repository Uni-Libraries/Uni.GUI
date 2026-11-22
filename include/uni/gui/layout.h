#pragma once

/// @file
/// @brief Declarative docking layouts and activation policy.

#include <string>
#include <vector>

namespace Uni::GUI {

/// Side of the parent leaf occupied by the first child of a split.
enum class UiDockSide {
    Left,
    Right,
    Up,
    Down,
};

/// Replace one current leaf with two uniquely named children.
struct UiDockSplit final {
    std::string leaf; ///< Source leaf; before the first split the only leaf is "root".
    UiDockSide side{UiDockSide::Left};
    float fraction{0.25f}; ///< Fraction occupied by side_leaf, finite and in [0.05, 0.95].
    std::string side_leaf;
    std::string remainder_leaf;
};

/// Place an ImGui window in a final (unsplit) leaf.
struct UiDockPlacement final {
    std::string window_name; ///< Non-empty, NUL-free window name. Each ImGui ID may occur only once, including names sharing a ### suffix.
    std::string leaf;
};

/// Ordered splits followed by window placements. Invalid definitions report UiErrorCode::LayoutInvalid.
struct UiDockLayout final {
    std::string id; ///< Non-empty key used for registration, activation and persistence.
    std::vector<UiDockSplit> splits;
    std::vector<UiDockPlacement> placements;
};

/// How to activate a registered layout.
enum class UiDockApplyMode {
    RestoreOrBuild,    ///< Prefer the stored per-layout ImGui snapshot; otherwise use the definition.
    ResetToDefinition, ///< Ignore the snapshot and rebuild from the registered definition.
};

/// Initial main dockspace configuration. Disabled docking requires empty layouts and initial_layout.
struct UiDockingConfig final {
    bool enabled{true};
    std::string dockspace_id{"UniGUI.MainDockSpace"}; ///< When enabled: non-empty, NUL-free and hashing to a nonzero ImGui ID.
    std::vector<UiDockLayout> layouts;
    std::string initial_layout; ///< Optional registered ID; a known selection in the settings file takes precedence.
};

} // namespace Uni::GUI
