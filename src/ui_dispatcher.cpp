#include <uni/gui/dispatcher.h>

#include "ui_dispatcher_internal.h"

#include <uni/gui/app.h>

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

namespace Uni::GUI {

namespace {

void CompleteTicket(const std::shared_ptr<Detail::UiCommandTicketState>& ticket, UiResult<void> result) {
    {
        std::lock_guard lock(ticket->mutex);
        if (ticket->result) {
            return;
        }
        ticket->result.emplace(std::move(result));
    }
    ticket->condition.notify_all();
}

[[nodiscard]] bool ScheduleWake(
    const std::shared_ptr<Detail::UiDispatcherState>& state,
    const std::uint32_t generation) {
    SDL_Event wake_event{};
    wake_event.user.type = state->wake_event_type;
    wake_event.user.code = std::bit_cast<std::int32_t>(generation);
    return SDL_PeepEvents(&wake_event, 1, SDL_ADDEVENT, 0, 0) == 1;
}

[[nodiscard]] std::uint32_t ReserveWakeGeneration(Detail::UiDispatcherState& state) {
    const std::uint32_t generation = state.next_wake_generation++;
    if (state.next_wake_generation == 0) {
        state.next_wake_generation = 1;
    }
    return generation;
}

template<typename Mutator>
[[nodiscard]] UiResult<void> MutateAndArm(
    const std::shared_ptr<Detail::UiDispatcherState>& state,
    Mutator&& mutator) {
    std::uint32_t generation = 0;
    {
        std::unique_lock lock(state->mutex);
        state->wake_condition.wait(lock, [&] { return !state->wake_scheduling || !state->open; });
        if (!state->open) {
            return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher is closed"});
        }
        if (state->armed_wake_generation != 0) {
            return std::forward<Mutator>(mutator)(*state);
        }
        generation = ReserveWakeGeneration(*state);
        state->armed_wake_generation = generation;
        state->wake_scheduling = true;
    }
    bool scheduled = false;
    try {
        scheduled = ScheduleWake(state, generation);
    } catch (...) {
        scheduled = false;
    }

    std::unique_lock lock(state->mutex);
    const auto finish_scheduling = [&] {
        state->wake_scheduling = false;
        lock.unlock();
        state->wake_condition.notify_all();
    };
    if (!state->open) {
        if (state->armed_wake_generation == generation) {
            state->armed_wake_generation = 0;
        }
        finish_scheduling();
        return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher closed while scheduling a wake event"});
    }
    if (!scheduled) {
        if (state->armed_wake_generation == generation) {
            state->armed_wake_generation = 0;
        }
        finish_scheduling();
        return std::unexpected(UiError{UiErrorCode::WakeupFailed, "SDL_PeepEvents failed"});
    }

    UiResult<void> mutated;
    try {
        mutated = std::forward<Mutator>(mutator)(*state);
    } catch (...) {
        if (state->armed_wake_generation == generation) {
            state->armed_wake_generation = 0;
        }
        finish_scheduling();
        throw;
    }
    if (!mutated && state->armed_wake_generation == generation) {
        state->armed_wake_generation = 0;
    }
    finish_scheduling();
    return mutated;
}

[[nodiscard]] UiResult<void> ArmWake(const std::shared_ptr<Detail::UiDispatcherState>& state) {
    return MutateAndArm(state, [](Detail::UiDispatcherState&) -> UiResult<void> { return {}; });
}

} // namespace

UiCommandTicket::UiCommandTicket() noexcept = default;
UiCommandTicket::~UiCommandTicket() = default;
UiCommandTicket::UiCommandTicket(const UiCommandTicket&) = default;
UiCommandTicket& UiCommandTicket::operator=(const UiCommandTicket&) = default;
UiCommandTicket::UiCommandTicket(UiCommandTicket&&) noexcept = default;
UiCommandTicket& UiCommandTicket::operator=(UiCommandTicket&&) noexcept = default;

UiCommandTicket::operator bool() const noexcept {
    return static_cast<bool>(m_state);
}

bool UiCommandTicket::Ready() const noexcept {
    if (!m_state) {
        return false;
    }
    std::lock_guard lock(m_state->mutex);
    return m_state->result.has_value();
}

std::optional<UiResult<void>> UiCommandTicket::TryResult() const {
    if (!m_state) {
        return UiResult<void>{std::unexpected(UiError{UiErrorCode::InvalidState, "Command ticket is empty"})};
    }
    std::lock_guard lock(m_state->mutex);
    return m_state->result;
}

UiResult<void> UiCommandTicket::Wait() const {
    if (!m_state) {
        return std::unexpected(UiError{UiErrorCode::InvalidState, "Command ticket is empty"});
    }
    std::unique_lock lock(m_state->mutex);
    if (!m_state->result && std::this_thread::get_id() == m_state->main_thread) {
        return std::unexpected(UiError{UiErrorCode::WouldDeadlock, "Cannot wait for a pending command on the UI thread"});
    }
    m_state->condition.wait(lock, [this] { return m_state->result.has_value(); });
    return *m_state->result;
}

UiDispatcher::UiDispatcher() noexcept = default;
UiDispatcher::~UiDispatcher() = default;
UiDispatcher::UiDispatcher(const UiDispatcher&) = default;
UiDispatcher& UiDispatcher::operator=(const UiDispatcher&) = default;
UiDispatcher::UiDispatcher(UiDispatcher&&) noexcept = default;
UiDispatcher& UiDispatcher::operator=(UiDispatcher&&) noexcept = default;

UiResult<UiCommandTicket> UiDispatcher::Post(UiCommand command) const {
    if (!m_state) {
        return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher is empty"});
    }
    if (!command) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Command must not be empty"});
    }

    auto ticket = std::make_shared<Detail::UiCommandTicketState>();
    ticket->main_thread = m_state->main_thread;
    if (auto wake = MutateAndArm(m_state, [&](Detail::UiDispatcherState& state) -> UiResult<void> {
            if (state.pending_count >= state.config.max_pending) {
                return std::unexpected(UiError{UiErrorCode::QueueFull, "Dispatcher queue is full"});
            }
            state.commands.push_back(Detail::UiCommandNode{std::move(command), ticket});
            ++state.pending_count;
            state.frame_requested = true;
            return {};
        }); !wake) {
        return std::unexpected(std::move(wake.error()));
    }
    UiCommandTicket result;
    result.m_state = std::move(ticket);
    return result;
}

UiResult<void> UiDispatcher::RequestFrame() const {
    if (!m_state) {
        return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher is empty"});
    }
    return MutateAndArm(m_state, [](Detail::UiDispatcherState& state) -> UiResult<void> {
        state.frame_requested = true;
        return {};
    });
}

UiResult<void> UiDispatcher::RequestExit() const {
    if (!m_state) {
        return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher is empty"});
    }
    return MutateAndArm(m_state, [](Detail::UiDispatcherState& state) -> UiResult<void> {
        state.exit_requested = true;
        state.frame_requested = true;
        return {};
    });
}

bool UiDispatcher::IsOpen() const noexcept {
    if (!m_state) {
        return false;
    }
    std::lock_guard lock(m_state->mutex);
    return m_state->open;
}

namespace Detail {

std::shared_ptr<UiDispatcherState> CreateDispatcherState(
    const UiCommandQueueConfig& config,
    const std::thread::id main_thread,
    const std::uint32_t wake_event_type) {
    auto state = std::make_shared<UiDispatcherState>();
    state->config = config;
    state->main_thread = main_thread;
    state->wake_event_type = wake_event_type;
    return state;
}

UiResult<std::size_t> DrainDispatcher(
    const std::shared_ptr<UiDispatcherState>& state,
    UiApp& app) {
    if (!state) {
        return std::size_t{0};
    }

    std::vector<UiCommandNode> commands;
    const auto started = std::chrono::steady_clock::now();
    {
        std::unique_lock lock(state->mutex);
        state->wake_condition.wait(lock, [&] { return !state->wake_scheduling || !state->open; });
        if (!state->open) {
            return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher is closed"});
        }
        state->armed_wake_generation = 0;
        const std::size_t watermark = std::min(state->commands.size(), state->config.max_commands_per_tick);
        commands.reserve(watermark);
        for (std::size_t index = 0; index < watermark; ++index) {
            commands.push_back(std::move(state->commands.front()));
            state->commands.pop_front();
        }
    }

    std::size_t executed = 0;
    for (auto& node : commands) {
        UiResult<void> result;
        try {
            result = node.command(app);
        } catch (const std::exception& exception) {
            result = std::unexpected(UiError{
                UiErrorCode::CommandFailed,
                std::string{"UI command threw: "}.append(exception.what()),
            });
        } catch (...) {
            result = std::unexpected(UiError{UiErrorCode::CommandFailed, "UI command threw an unknown exception"});
        }
        {
            std::lock_guard lock(state->mutex);
            --state->pending_count;
        }
        CompleteTicket(node.ticket, std::move(result));
        ++executed;

        if (std::chrono::steady_clock::now() - started >= state->config.execution_budget) {
            break;
        }
    }

    if (executed < commands.size()) {
        std::lock_guard lock(state->mutex);
        for (std::size_t index = commands.size(); index-- > executed;) {
            state->commands.push_front(std::move(commands[index]));
        }
    }

    if (HasPendingCommands(state)) {
        if (auto wake = ArmWake(state); !wake) {
            return std::unexpected(std::move(wake.error()));
        }
    }
    return executed;
}

bool ConsumeFrameRequest(const std::shared_ptr<UiDispatcherState>& state) {
    if (!state) {
        return false;
    }
    std::lock_guard lock(state->mutex);
    return std::exchange(state->frame_requested, false);
}

UiResult<void> RequestFrameWithoutWake(const std::shared_ptr<UiDispatcherState>& state) {
    if (!state) {
        return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher is empty"});
    }
    std::lock_guard lock(state->mutex);
    if (!state->open) {
        return std::unexpected(UiError{UiErrorCode::QueueClosed, "Dispatcher is closed"});
    }
    state->frame_requested = true;
    return {};
}

bool ConsumeExitRequest(const std::shared_ptr<UiDispatcherState>& state) {
    if (!state) {
        return false;
    }
    std::lock_guard lock(state->mutex);
    return std::exchange(state->exit_requested, false);
}

bool HasPendingCommands(const std::shared_ptr<UiDispatcherState>& state) {
    if (!state) {
        return false;
    }
    std::lock_guard lock(state->mutex);
    return !state->commands.empty();
}

bool HasPendingDispatcherWork(const std::shared_ptr<UiDispatcherState>& state) {
    if (!state) {
        return false;
    }
    std::lock_guard lock(state->mutex);
    return !state->commands.empty() || state->frame_requested || state->exit_requested;
}

bool ConsumeDispatcherWakeEvent(
    const std::shared_ptr<UiDispatcherState>& state,
    const SDL_Event& event) {
    if (!state || state->wake_event_type == 0 || event.type != state->wake_event_type) {
        return false;
    }
    const std::uint32_t generation = std::bit_cast<std::uint32_t>(event.user.code);
    std::lock_guard lock(state->mutex);
    if (state->armed_wake_generation == generation) {
        state->armed_wake_generation = 0;
    }
    return true;
}

void CloseDispatcher(const std::shared_ptr<UiDispatcherState>& state) noexcept {
    if (!state) {
        return;
    }
    std::deque<UiCommandNode> cancelled;
    {
        std::unique_lock lock(state->mutex);
        state->wake_condition.wait(lock, [&] { return !state->wake_scheduling; });
        if (!state->open) {
            return;
        }
        state->open = false;
        state->armed_wake_generation = 0;
        state->frame_requested = false;
        state->exit_requested = false;
        cancelled.swap(state->commands);
        state->pending_count -= cancelled.size();
    }
    state->wake_condition.notify_all();
    for (auto& node : cancelled) {
        CompleteTicket(node.ticket, std::unexpected(UiError{UiErrorCode::Cancelled, "UI command was cancelled during shutdown"}));
    }
}

} // namespace Detail
} // namespace Uni::GUI
