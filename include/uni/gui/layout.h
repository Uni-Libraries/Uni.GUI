#pragma once

#include <string>
#include <vector>

namespace Uni::GUI {

enum class UiDockSide {
    Left,
    Right,
    Up,
    Down,
};

struct UiDockSplit final {
    std::string leaf;
    UiDockSide side{UiDockSide::Left};
    float fraction{0.25f};
    std::string side_leaf;
    std::string remainder_leaf;
};

struct UiDockPlacement final {
    std::string window_name;
    std::string leaf;
};

struct UiDockLayout final {
    std::string id;
    std::vector<UiDockSplit> splits;
    std::vector<UiDockPlacement> placements;
};

enum class UiDockApplyMode {
    RestoreOrBuild,
    ResetToDefinition,
};

struct UiDockingConfig final {
    bool enabled{true};
    std::string dockspace_id{"UniGUI.MainDockSpace"};
    std::vector<UiDockLayout> layouts;
    std::string initial_layout;
};

} // namespace Uni::GUI
