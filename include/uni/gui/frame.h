#pragma once

#include <chrono>
#include <cstdint>

namespace Uni::GUI {

/// Host loop scheduling: iterate continuously, target a frequency, or wait for an event.
enum class UiLoopMode {
    Continuous,
    RateLimited,
    WaitForEvent,
};

/// Requested iteration rate, not a guarantee that each iteration renders.
struct UiLoopRate final {
    UiLoopMode mode{UiLoopMode::Continuous};
    double frames_per_second{}; ///< Used only in RateLimited mode; must yield a positive, representable clock period.
};

/// Rates for active, idle and minimized windows, in decreasing order of visibility.
struct UiFramePolicy final {
    UiLoopRate active{UiLoopMode::Continuous, 0.0};
    UiLoopRate idle{UiLoopMode::RateLimited, 10.0};
    UiLoopRate minimized{UiLoopMode::WaitForEvent, 0.0};
    std::chrono::milliseconds idle_after{500};
    bool render_while_minimized{};
};

/// Additional work requested by an element; Continuous takes precedence over OneMoreFrame.
enum class UiFrameDemand {
    None,
    OneMoreFrame,
    Continuous,
};

/// Result of one successful element callback. Returning keep_alive=false schedules removal after iteration.
struct UiElementUpdate final {
    bool keep_alive{true};
    UiFrameDemand frame_demand{UiFrameDemand::None};
};

/// Outcome of UiApp::Tick(), including non-rendering iterations.
struct UiTickResult final {
    bool rendered{};
    bool exit_requested{};
    UiLoopRate next_iteration;
    std::uint64_t frame_index{}; ///< Number of frames successfully rendered since initialization.
};

} // namespace Uni::GUI
