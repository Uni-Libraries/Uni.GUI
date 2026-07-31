#pragma once

#include <chrono>
#include <cstdint>

namespace Uni::GUI {

enum class UiLoopMode {
    Continuous,
    RateLimited,
    WaitForEvent,
};

struct UiLoopRate final {
    UiLoopMode mode{UiLoopMode::Continuous};
    double frames_per_second{};
};

struct UiFramePolicy final {
    UiLoopRate active{UiLoopMode::Continuous, 0.0};
    UiLoopRate idle{UiLoopMode::RateLimited, 10.0};
    UiLoopRate minimized{UiLoopMode::WaitForEvent, 0.0};
    std::chrono::milliseconds idle_after{500};
    bool render_while_minimized{};
};

enum class UiFrameDemand {
    None,
    OneMoreFrame,
    Continuous,
};

struct UiElementUpdate final {
    bool keep_alive{true};
    UiFrameDemand frame_demand{UiFrameDemand::None};
};

struct UiTickResult final {
    bool rendered{};
    bool exit_requested{};
    UiLoopRate next_iteration;
    std::uint64_t frame_index{};
};

} // namespace Uni::GUI
