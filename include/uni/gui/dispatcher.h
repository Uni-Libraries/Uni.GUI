#pragma once

#include <uni/gui/error.h>
#include <uni/gui/export.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace Uni::GUI {

class UiApp;
namespace Detail {
struct UiCommandTicketState;
struct UiDispatcherState;
}

/// Move-only callable for UiDispatcher::Post(). Captured references must outlive execution or cancellation.
class UiCommand final {
public:
    UiCommand() noexcept = default;

    template<typename Callable>
        requires(!std::is_same_v<std::remove_cvref_t<Callable>, UiCommand> &&
                 std::is_constructible_v<std::decay_t<Callable>, Callable> &&
                 std::is_invocable_r_v<UiResult<void>, std::decay_t<Callable>&, UiApp&>)
    UiCommand(Callable&& callable)
        : m_callable(std::make_unique<CallableModel<std::decay_t<Callable>>>(
              std::forward<Callable>(callable))) {}

    UiCommand(const UiCommand&) = delete;
    UiCommand& operator=(const UiCommand&) = delete;
    UiCommand(UiCommand&&) noexcept = default;
    UiCommand& operator=(UiCommand&&) noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(m_callable);
    }

    /// Invoke the stored callable. @pre The command is neither empty nor moved from.
    UiResult<void> operator()(UiApp& app) {
        return m_callable->Invoke(app);
    }

private:
    struct CallableInterface {
        virtual ~CallableInterface() = default;
        virtual UiResult<void> Invoke(UiApp& app) = 0;
    };

    template<typename Callable>
    struct CallableModel final : CallableInterface {
        template<typename Value>
        explicit CallableModel(Value&& callable)
            : m_callable(std::forward<Value>(callable)) {}

        UiResult<void> Invoke(UiApp& app) override {
            return std::invoke(m_callable, app);
        }

        Callable m_callable;
    };

    std::unique_ptr<CallableInterface> m_callable;
};

/// Positive queue limits. The per-tick time budget is checked between commands; it cannot interrupt a command.
struct UiCommandQueueConfig final {
    std::size_t max_pending{4096};
    std::size_t max_commands_per_tick{256};
    std::chrono::nanoseconds execution_budget{std::chrono::milliseconds{2}};
};

/// Copyable shared completion handle that can be observed from worker threads.
class UNI_GUI_EXPORT UiCommandTicket final {
public:
    UiCommandTicket() noexcept;
    ~UiCommandTicket();
    UiCommandTicket(const UiCommandTicket& other);
    UiCommandTicket& operator=(const UiCommandTicket& other);
    UiCommandTicket(UiCommandTicket&& other) noexcept;
    UiCommandTicket& operator=(UiCommandTicket&& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;
    /// True if a result (success, error or cancellation) is available; false for an empty ticket.
    [[nodiscard]] bool Ready() const noexcept;
    /// Return nullopt if pending, or a copy of the result. An empty ticket yields an InvalidState error.
    [[nodiscard]] std::optional<UiResult<void>> TryResult() const;
    /// Wait for completion. Pending waits on the UI thread return WouldDeadlock without blocking.
    [[nodiscard]] UiResult<void> Wait() const;

private:
    friend class UiDispatcher;
    friend class UiApp;

    std::shared_ptr<Detail::UiCommandTicketState> m_state;
};

/// Copyable, thread-safe posting handle. Copies remain closed after their originating app shuts down.
class UNI_GUI_EXPORT UiDispatcher final {
public:
    UiDispatcher() noexcept;
    ~UiDispatcher();
    UiDispatcher(const UiDispatcher& other);
    UiDispatcher& operator=(const UiDispatcher& other);
    UiDispatcher(UiDispatcher&& other) noexcept;
    UiDispatcher& operator=(UiDispatcher&& other) noexcept;

    /// Queue a non-empty command for a UI-thread tick before the ImGui frame, and wake the host loop.
    /// Acceptance is not completion: use the ticket to observe command errors or shutdown cancellation.
    [[nodiscard]] UiResult<UiCommandTicket> Post(UiCommand command) const;
    /// Request a frame and wake the host loop; repeated wakes may coalesce.
    [[nodiscard]] UiResult<void> RequestFrame() const;
    /// Wake the UI loop to process an exit request; this does not synchronously shut down the app.
    [[nodiscard]] UiResult<void> RequestExit() const;
    /// Observe whether the queue is open now. It can close before a subsequent Post().
    [[nodiscard]] bool IsOpen() const noexcept;

private:
    friend class UiApp;

    std::shared_ptr<Detail::UiDispatcherState> m_state;
};

} // namespace Uni::GUI
