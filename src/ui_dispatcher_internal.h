#pragma once

#include <uni/gui/dispatcher.h>

#include <SDL3/SDL_events.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace Uni::GUI::Detail {

struct UiCommandTicketState final {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::optional<UiResult<void>> result;
    std::thread::id main_thread;
};

struct UiCommandNode final {
    UiCommand command;
    std::shared_ptr<UiCommandTicketState> ticket;
};

struct UiDispatcherState final {
    mutable std::mutex mutex;
    std::condition_variable wake_condition;
    std::deque<UiCommandNode> commands;
    std::size_t pending_count{};
    UiCommandQueueConfig config;
    std::thread::id main_thread;
    std::uint32_t wake_event_type{};
    std::uint32_t next_wake_generation{1};
    std::uint32_t armed_wake_generation{};
    bool open{true};
    bool wake_scheduling{};
    bool frame_requested{true};
    bool exit_requested{};
};

[[nodiscard]] std::shared_ptr<UiDispatcherState> CreateDispatcherState(
    const UiCommandQueueConfig& config,
    std::thread::id main_thread,
    std::uint32_t wake_event_type);

[[nodiscard]] UiResult<std::size_t> DrainDispatcher(
    const std::shared_ptr<UiDispatcherState>& state,
    UiApp& app);

[[nodiscard]] bool ConsumeFrameRequest(const std::shared_ptr<UiDispatcherState>& state);
[[nodiscard]] UiResult<void> RequestFrameWithoutWake(const std::shared_ptr<UiDispatcherState>& state);
[[nodiscard]] bool ConsumeExitRequest(const std::shared_ptr<UiDispatcherState>& state);
[[nodiscard]] bool HasPendingCommands(const std::shared_ptr<UiDispatcherState>& state);
[[nodiscard]] bool HasPendingDispatcherWork(const std::shared_ptr<UiDispatcherState>& state);
[[nodiscard]] bool ConsumeDispatcherWakeEvent(
    const std::shared_ptr<UiDispatcherState>& state,
    const SDL_Event& event);
void CloseDispatcher(const std::shared_ptr<UiDispatcherState>& state) noexcept;

} // namespace Uni::GUI::Detail
