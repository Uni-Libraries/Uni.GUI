#include <uni/gui/app.h>

#include <imgui.h>

#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool SDLCALL CountAndRejectEvent(void* userdata, SDL_Event*) {
    ++*static_cast<std::atomic<int>*>(userdata);
    return false;
}

class ChildElement final : public Uni::GUI::UiElement {
public:
    explicit ChildElement(int& updates) : m_updates(updates) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        ++m_updates;
        ImGui::Begin("Runtime child");
        ImGui::TextUnformatted("Child element");
        ImGui::End();
        return Uni::GUI::UiElementUpdate{};
    }

private:
    int& m_updates;
};

class RootElement final : public Uni::GUI::UiElement {
public:
    explicit RootElement(int& child_updates) : m_child_updates(child_updates) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState& state) override {
        if (!m_initialized) {
            m_initialized = true;
            auto texture = state.app.CreateTexture(32, 32);
            if (!texture) {
                throw std::runtime_error(texture.error().message);
            }
            m_texture = std::move(*texture);
            if (!m_texture.Clear(IM_COL32(40, 100, 180, 255))) {
                throw std::runtime_error("Failed to clear runtime texture");
            }

            auto child = state.app.AddElement(std::make_unique<ChildElement>(m_child_updates));
            if (!child) {
                throw std::runtime_error(child.error().message);
            }
            auto removed = state.app.RemoveElement(state.element_id);
            if (!removed || !*removed) {
                throw std::runtime_error("Failed to request self-removal");
            }
        }

        ImGui::Begin("Runtime root");
        ImGui::Image(m_texture.GetRef(), ImVec2(32.0f, 32.0f));
        ImGui::End();
        return Uni::GUI::UiElementUpdate{};
    }

private:
    int& m_child_updates;
    Uni::GUI::UiTexture m_texture;
    bool m_initialized{};
};

class ThrowingWindow final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        ImGui::Begin("Throwing runtime window");
        throw std::runtime_error("expected runtime exception");
    }
};

class RecursiveFrameElement final : public Uni::GUI::UiElement {
public:
    explicit RecursiveFrameElement(bool& rejected) : m_rejected(rejected) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState& state) override {
        auto nested = state.app.Tick();
        m_rejected = !nested && nested.error().code == Uni::GUI::UiErrorCode::InvalidState;
        return Uni::GUI::UiElementUpdate{false, Uni::GUI::UiFrameDemand::None};
    }

private:
    bool& m_rejected;
};

class ShutdownAttemptElement final : public Uni::GUI::UiElement {
public:
    explicit ShutdownAttemptElement(bool& rejected) : m_rejected(rejected) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState& state) override {
        auto shutdown = state.app.Shutdown();
        m_rejected = !shutdown && shutdown.error().code == Uni::GUI::UiErrorCode::InvalidState;
        return Uni::GUI::UiElementUpdate{false, Uni::GUI::UiFrameDemand::None};
    }

private:
    bool& m_rejected;
};

class OneMoreFrameElement final : public Uni::GUI::UiElement {
public:
    explicit OneMoreFrameElement(int& updates) : m_updates(updates) {}

    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        ++m_updates;
        return Uni::GUI::UiElementUpdate{
            m_updates < 2,
            m_updates == 1 ? Uni::GUI::UiFrameDemand::OneMoreFrame : Uni::GUI::UiFrameDemand::None,
        };
    }

private:
    int& m_updates;
};

class ExitRequestElement final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState& state) override {
        auto exit = state.app.Dispatcher().RequestExit();
        if (!exit) {
            return std::unexpected(std::move(exit.error()));
        }
        ImGui::Begin("Exit request window");
        ImGui::TextUnformatted("Exit is requested through the dispatcher");
        ImGui::End();
        return Uni::GUI::UiElementUpdate{};
    }
};

Uni::GUI::UiAppConfig MakeConfig() {
    Uni::GUI::UiAppConfig config;
    config.title = "UniGUI runtime test";
    config.initial_width = 320;
    config.initial_height = 240;
    config.renderer = Uni::GUI::UiRendererPreference::SdlRenderer;
    config.viewports = Uni::GUI::UiFeaturePolicy::Disabled;
    config.vsync = Uni::GUI::UiVsyncMode::Disabled;
    config.persistence.enabled = false;
    config.docking.enabled = false;
    return config;
}

Uni::GUI::UiDockLayout MakeDockLayout(const std::string& id, const bool split) {
    Uni::GUI::UiDockLayout layout;
    layout.id = id;
    if (split) {
        layout.splits.push_back({"root", Uni::GUI::UiDockSide::Left, 0.35f, "left", "right"});
        layout.placements.push_back({"Runtime child", "left"});
        layout.placements.push_back({"Runtime root", "right"});
    } else {
        layout.placements.push_back({"Runtime child", "root"});
        layout.placements.push_back({"Runtime root", "root"});
    }
    return layout;
}

} // namespace

int main() {
    using namespace Uni::GUI;

    UiApp app;
    auto initialized = app.Initialize(MakeConfig());
    Expect(initialized.has_value(), initialized ? "" : initialized.error().message.c_str());
    Expect(app.State() == UiLifecycleState::Ready, "Initialized application must be ready");
    Expect(!app.RendererName().empty(), "Initialized application must expose renderer name");

    int child_updates = 0;
    auto root = app.AddElement(std::make_unique<RootElement>(child_updates));
    Expect(root.has_value(), root ? "" : root.error().message.c_str());

    Expect(app.Tick().has_value(), "First runtime frame must render");
    Expect(child_updates == 0, "Deferred child must not update in its creation frame");
    Expect(app.Tick().has_value(), "Second runtime frame must render");
    Expect(child_updates == 1, "Deferred child must update on the next frame");

    auto missing = app.RemoveElement(*root);
    Expect(missing.has_value() && !*missing, "Removed element ID must no longer resolve");

    SDL_Event user_event{};
    user_event.type = SDL_EVENT_USER;
    Expect(app.DispatchEvent(user_event).has_value(), "Synthetic user event must dispatch");

    auto invalid_vsync = app.SetVsync(static_cast<UiVsyncMode>(999));
    Expect(!invalid_vsync && invalid_vsync.error().code == UiErrorCode::InvalidArgument,
           "Invalid runtime VSync enum must return InvalidArgument");

    Expect(app.Shutdown().has_value(), "First runtime Shutdown must succeed");
    Expect(app.Shutdown().has_value(), "Second runtime Shutdown must succeed");
    Expect(app.State() == UiLifecycleState::Empty, "Runtime shutdown must be idempotent");

    UiApp required_viewports;
    auto unsupported_config = MakeConfig();
    unsupported_config.viewports = UiFeaturePolicy::Required;
    auto unsupported = required_viewports.Initialize(std::move(unsupported_config));
    Expect(!unsupported && unsupported.error().code == UiErrorCode::BackendInitialization,
           "Required unsupported viewports must return BackendInitialization");
    Expect(required_viewports.State() == UiLifecycleState::Failed,
           "Failed backend initialization must enter Failed state");
    Expect(required_viewports.Shutdown().has_value(), "Failed app Shutdown must succeed");
    Expect(required_viewports.State() == UiLifecycleState::Empty,
           "Failed initialization resources must support explicit cleanup");

    UiApp throwing_app;
    Expect(throwing_app.Initialize(MakeConfig()).has_value(), "Throwing test application must initialize");
    Expect(throwing_app.AddElement(std::make_unique<ThrowingWindow>()).has_value(),
           "Throwing element must be registered");
    auto failed_frame = throwing_app.Tick();
    Expect(!failed_frame && failed_frame.error().code == UiErrorCode::FrameRendering,
           "Element exceptions must propagate through RenderFrame");
    Expect(throwing_app.Shutdown().has_value(), "Throwing app Shutdown must succeed");
    Expect(throwing_app.State() == UiLifecycleState::Empty,
           "Application must shut down cleanly after an element exception");

    UiApp reentrant_app;
    Expect(reentrant_app.Initialize(MakeConfig()).has_value(), "Reentrancy test application must initialize");
    bool recursive_frame_rejected = false;
    bool in_frame_shutdown_rejected = false;
    Expect(reentrant_app.AddElement(std::make_unique<RecursiveFrameElement>(recursive_frame_rejected)).has_value(),
            "Recursive frame element must be registered");
    Expect(reentrant_app.AddElement(std::make_unique<ShutdownAttemptElement>(in_frame_shutdown_rejected)).has_value(),
            "In-frame shutdown element must be registered");
    Expect(reentrant_app.AddElement(std::make_unique<ExitRequestElement>()).has_value(),
           "Exit request element must be registered");
    Expect(reentrant_app.Tick().has_value(), "Frame containing an exit request must complete");
    auto exit_tick = reentrant_app.Tick();
    Expect(exit_tick && exit_tick->exit_requested, "Dispatcher exit request must terminate the next tick");
    Expect(recursive_frame_rejected, "Recursive Tick must return InvalidState");
    Expect(in_frame_shutdown_rejected, "Shutdown during Tick must return InvalidState");
    Expect(reentrant_app.State() == UiLifecycleState::ExitRequested,
           "Exit request must enter ExitRequested state");
    Expect(reentrant_app.Shutdown().has_value(), "ExitRequested application must shut down");

    UiApp one_more_app;
    auto one_more_config = MakeConfig();
    one_more_config.frame_policy.active = {UiLoopMode::WaitForEvent, 0.0};
    one_more_config.frame_policy.idle = {UiLoopMode::WaitForEvent, 0.0};
    Expect(one_more_app.Initialize(std::move(one_more_config)).has_value(),
           "One-more-frame application must initialize");
    int one_more_updates = 0;
    Expect(one_more_app.AddElement(std::make_unique<OneMoreFrameElement>(one_more_updates)).has_value(),
           "One-more-frame element must register");
    SDL_FlushEvents(SDL_EVENT_USER, SDL_EVENT_LAST);
    const auto one_more_first = one_more_app.Tick();
    Expect(one_more_first && one_more_first->rendered &&
               one_more_first->next_iteration.mode == UiLoopMode::Continuous &&
               !SDL_HasEvents(SDL_EVENT_USER, SDL_EVENT_LAST),
           "OneMoreFrame demand must force the next iteration without an SDL self-wake");
    const auto one_more_second = one_more_app.Tick();
    Expect(one_more_second && one_more_second->rendered && one_more_updates == 2,
           "OneMoreFrame demand must render exactly one additional frame");
    Expect(one_more_app.Shutdown().has_value(), "One-more-frame application must shut down");

    UiApp dispatcher_app;
    auto dispatcher_config = MakeConfig();
    dispatcher_config.frame_policy.active = {UiLoopMode::WaitForEvent, 0.0};
    dispatcher_config.frame_policy.idle = {UiLoopMode::WaitForEvent, 0.0};
    Expect(dispatcher_app.Initialize(std::move(dispatcher_config)).has_value(),
           "Dispatcher test application must initialize");
    auto first_idle_tick = dispatcher_app.Tick();
    Expect(first_idle_tick && first_idle_tick->rendered, "First wait-for-event tick must render");
    auto skipped_tick = dispatcher_app.Tick();
    Expect(skipped_tick && !skipped_tick->rendered && skipped_tick->next_iteration.mode == UiLoopMode::WaitForEvent,
           "Idle wait-for-event tick must skip rendering");

    UiDispatcher dispatcher = dispatcher_app.Dispatcher();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    std::optional<UiResult<void>> worker_frame_request;
    std::thread wake_request_thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
        worker_frame_request.emplace(dispatcher.RequestFrame());
    });
    bool wake_consumed = false;
    Uint32 dispatcher_event_type = 0;
    for (int attempt = 0; attempt < 10 && !wake_consumed; ++attempt) {
        SDL_Event wake_event{};
        if (!SDL_WaitEventTimeout(&wake_event, 100)) {
            continue;
        }
        const auto wake_dispatch = dispatcher_app.DispatchEvent(wake_event);
        Expect(wake_dispatch.has_value(), "Events preceding the dispatcher wake must remain dispatchable");
        wake_consumed = wake_dispatch->consumed;
        if (wake_consumed) {
            dispatcher_event_type = wake_event.type;
        }
    }
    wake_request_thread.join();
    Expect(worker_frame_request && worker_frame_request->has_value(),
           "Worker frame request must enqueue the dispatcher event");
    Expect(wake_consumed,
           "Internal dispatcher wake event must be consumed before user event hooks");

    SDL_FlushEvent(dispatcher_event_type);
    SDL_Event filtered_user_event{};
    filtered_user_event.type = SDL_EVENT_USER;
    const auto filtered_dispatch = dispatcher_app.DispatchEvent(filtered_user_event);
    Expect(filtered_dispatch.has_value() && !SDL_HasEvent(dispatcher_event_type),
           "DispatchEvent must request its frame without pushing a filterable self-event");

    std::atomic<int> command_count{0};
    std::optional<UiCommandTicket> worker_ticket;
    std::thread producer([&] {
        auto posted = dispatcher.Post([&](UiApp& command_app) -> UiResult<void> {
            if (!command_app.IsMainThread()) {
                return std::unexpected(UiError{UiErrorCode::WrongThread, "Command did not run on UI thread"});
            }
            ++command_count;
            return {};
        });
        if (posted) {
            worker_ticket.emplace(std::move(*posted));
        }
    });
    producer.join();
    Expect(worker_ticket.has_value(), "Worker must enqueue a UI command");
    auto would_deadlock = worker_ticket->Wait();
    Expect(!would_deadlock && would_deadlock.error().code == UiErrorCode::WouldDeadlock,
           "UI-thread wait on a pending command must be rejected");
    auto command_tick = dispatcher_app.Tick();
    Expect(command_tick && command_tick->rendered && command_count == 1,
           "Worker command must execute exactly once and request a frame");
    Expect(worker_ticket->Wait().has_value(), "Completed command ticket must return success");

    std::optional<UiCommandTicket> nested_ticket;
    auto outer = dispatcher.Post([&](UiApp& command_app) -> UiResult<void> {
        auto nested = command_app.Dispatcher().Post([&](UiApp&) -> UiResult<void> {
            ++command_count;
            return {};
        });
        if (!nested) {
            return std::unexpected(std::move(nested.error()));
        }
        nested_ticket.emplace(std::move(*nested));
        return {};
    });
    Expect(outer.has_value(), "Outer command must enqueue");
    Expect(dispatcher_app.Tick().has_value() && nested_ticket.has_value() && !nested_ticket->Ready(),
           "Command posted by a command must be deferred to the next tick");
    Expect(dispatcher_app.Tick().has_value() && nested_ticket->Ready() && command_count == 2,
           "Nested command must execute on the following tick");

    std::vector<int> event_order;
    UiEventHooks hooks;
    hooks.before_imgui = [&](UiEventContext& context) -> UiResult<UiEventAction> {
        event_order.push_back(1);
        return context.event.type == SDL_EVENT_QUIT ? UiEventAction::Consume : UiEventAction::Pass;
    };
    hooks.after_imgui = [&](UiEventContext& context) -> UiResult<UiEventAction> {
        event_order.push_back(context.delivered_to_imgui ? 2 : 3);
        return UiEventAction::Pass;
    };
    Expect(dispatcher_app.SetEventHooks(std::move(hooks)).has_value(), "Event hooks must be installed");
    SDL_Event close_event{};
    close_event.type = SDL_EVENT_QUIT;
    auto consumed_close = dispatcher_app.DispatchEvent(close_event);
    Expect(consumed_close && consumed_close->consumed && !consumed_close->exit_requested,
           "Pre-hook must be able to consume a close event");
    Expect(event_order == std::vector<int>({1, 3}), "Consumed event hook ordering must be deterministic");

    bool before_shutdown_rejected = false;
    bool after_shutdown_rejected = false;
    UiEventHooks shutdown_hooks;
    shutdown_hooks.before_imgui = [&](UiEventContext& context) -> UiResult<UiEventAction> {
        const auto shutdown = context.app.Shutdown();
        before_shutdown_rejected = !shutdown && shutdown.error().code == UiErrorCode::InvalidState;
        return UiEventAction::Pass;
    };
    shutdown_hooks.after_imgui = [&](UiEventContext& context) -> UiResult<UiEventAction> {
        const auto shutdown = context.app.Shutdown();
        after_shutdown_rejected = !shutdown && shutdown.error().code == UiErrorCode::InvalidState;
        return UiEventAction::Pass;
    };
    Expect(dispatcher_app.SetEventHooks(std::move(shutdown_hooks)).has_value(),
           "Shutdown-rejection event hooks must install");
    SDL_Event shutdown_hook_event{};
    shutdown_hook_event.type = SDL_EVENT_USER;
    Expect(dispatcher_app.DispatchEvent(shutdown_hook_event).has_value(),
           "Event dispatch must survive shutdown attempts from both hooks");
    Expect(before_shutdown_rejected && after_shutdown_rejected && dispatcher_app.State() == UiLifecycleState::Ready,
           "Shutdown during event dispatch must return InvalidState without destroying application state");

    UiEventHooks throwing_hooks;
    throwing_hooks.before_imgui = [](UiEventContext&) -> UiResult<UiEventAction> {
        throw std::runtime_error("expected event hook failure");
    };
    Expect(dispatcher_app.SetEventHooks(std::move(throwing_hooks)).has_value(),
           "Throwing event hook must install");
    const auto failed_event_hook = dispatcher_app.DispatchEvent(shutdown_hook_event);
    Expect(!failed_event_hook && failed_event_hook.error().code == UiErrorCode::EventHandling &&
               failed_event_hook.error().message.find("expected event hook failure") != std::string::npos,
           "Event hook exceptions must include SDL 3.4 event diagnostics");

    std::optional<UiErrorCode> wrong_thread_error;
    SDL_Event threaded_event{};
    threaded_event.type = SDL_EVENT_USER;
    std::thread event_thread([&] {
        auto dispatched = dispatcher_app.DispatchEvent(threaded_event);
        if (!dispatched) {
            wrong_thread_error = dispatched.error().code;
        }
    });
    event_thread.join();
    Expect(wrong_thread_error == UiErrorCode::WrongThread,
           "Event dispatch from a worker thread must return WrongThread");
    Expect(dispatcher_app.SetEventHooks({}).has_value(), "Event hooks must reset before wake ordering tests");

    UiAssetSpec bytes_asset;
    bytes_asset.key = "runtime-bytes";
    auto initial_bytes = std::make_shared<const std::vector<std::byte>>(
        std::vector<std::byte>{std::byte{1}, std::byte{2}});
    bytes_asset.source = initial_bytes;
    auto asset = dispatcher_app.UpsertAsset(std::move(bytes_asset));
    Expect(asset && asset->generation == 1 && asset->byte_size == 2,
           "Byte asset must register with generation one");
    UiAssetSpec updated_asset;
    updated_asset.key = "runtime-bytes";
    updated_asset.source = std::make_shared<const std::vector<std::byte>>(
        std::vector<std::byte>{std::byte{3}, std::byte{4}, std::byte{5}});
    auto updated_asset_info = dispatcher_app.UpsertAsset(std::move(updated_asset));
    Expect(updated_asset_info && updated_asset_info->id == asset->id && updated_asset_info->generation == 2,
           "Asset replacement must preserve ID and increment generation");
    Expect(dispatcher_app.RemoveAsset(asset->id).value_or(false), "Unused asset must be removable");

    const std::vector<std::filesystem::path> font_candidates{
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/Roboto-Medium.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    const auto font_path = std::find_if(font_candidates.begin(), font_candidates.end(), [](const auto& path) {
        return std::filesystem::exists(path);
    });
    Expect(font_path != font_candidates.end(), "Runtime test requires a platform font file");
    const auto font_size = std::filesystem::file_size(*font_path);
    auto mutable_font_bytes = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(font_size));
    {
        std::ifstream font_stream(*font_path, std::ios::binary);
        Expect(font_stream.read(
                   reinterpret_cast<char*>(mutable_font_bytes->data()),
                   static_cast<std::streamsize>(mutable_font_bytes->size())).good(),
               "Platform font fixture must be readable");
    }
    UiAssetSpec font_asset_spec;
    font_asset_spec.key = "runtime-font";
    font_asset_spec.source = std::static_pointer_cast<const std::vector<std::byte>>(mutable_font_bytes);
    auto font_asset = dispatcher_app.UpsertAsset(std::move(font_asset_spec));
    Expect(font_asset.has_value(), "Font file must register as an asset");
    auto runtime_font = dispatcher_app.CreateFont(UiFontSpec{font_asset->id, 15.0f, UiGlyphRange::Cyrillic, true});
    ImFont* original_runtime_font = runtime_font
        ? dispatcher_app.GetFont(*runtime_font).value_or(nullptr)
        : nullptr;
    Expect(runtime_font.has_value() && original_runtime_font != nullptr,
            "Runtime font must be created and selected");
    mutable_font_bytes->clear();
    mutable_font_bytes->shrink_to_fit();
    Expect(dispatcher_app.Tick().has_value(),
           "Runtime font must remain valid after the caller mutates its original byte buffer");
    UiAssetSpec reloaded_font_asset;
    reloaded_font_asset.key = "runtime-font";
    reloaded_font_asset.source = font_path->string();
    auto reloaded_font = dispatcher_app.UpsertAsset(std::move(reloaded_font_asset));
    Expect(reloaded_font && reloaded_font->generation == 2,
            "Reloading a font asset must preserve the font handle and increment generation");
    Expect(dispatcher_app.GetFont(*runtime_font).value_or(nullptr) != original_runtime_font,
           "Reloading a font source must replace the resolved ImFont pointer");
    auto in_use_asset = dispatcher_app.RemoveAsset(font_asset->id);
    Expect(!in_use_asset && in_use_asset.error().code == UiErrorCode::InvalidState,
           "Asset used by a runtime font must not be removable");
    Expect(dispatcher_app.RemoveFont(*runtime_font).value_or(false), "Runtime font must be removable");
    Expect(dispatcher_app.RemoveAsset(font_asset->id).value_or(false), "Font asset must be removable after its font");

    Expect(dispatcher_app.Tick().has_value(), "Pending UI-thread frame requests must drain before wake ordering testing");
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

    std::atomic<int> wake_filter_calls{0};
    SDL_SetEventFilter(CountAndRejectEvent, &wake_filter_calls);
    const int wake_filter_calls_before = wake_filter_calls.load();
    std::optional<UiResult<void>> first_generation_request;
    std::thread first_generation_thread([&] {
        first_generation_request.emplace(dispatcher.RequestFrame());
    });
    first_generation_thread.join();
    SDL_SetEventFilter(nullptr, nullptr);
    Expect(first_generation_request && first_generation_request->has_value() &&
               wake_filter_calls == wake_filter_calls_before,
           "Dispatcher wake events must bypass synchronous user event filters");

    SDL_Event stale_wake_event{};
    bool stale_wake_captured = false;
    for (int attempt = 0; attempt < 20 && !stale_wake_captured; ++attempt) {
        SDL_Event event{};
        if (!SDL_PollEvent(&event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
            continue;
        }
        if (event.type >= SDL_EVENT_USER) {
            stale_wake_event = event;
            stale_wake_captured = true;
        } else {
            Expect(dispatcher_app.DispatchEvent(event).has_value(),
                   "Events preceding the first wake generation must remain dispatchable");
        }
    }
    Expect(stale_wake_captured, "The first wake generation must be captured before manual Tick");

    Expect(dispatcher_app.Tick().has_value(),
           "Manual Tick must invalidate the captured wake generation");

    std::optional<UiResult<void>> second_generation_request;
    std::thread second_generation_thread([&] {
        second_generation_request.emplace(dispatcher.RequestFrame());
    });
    second_generation_thread.join();
    Expect(second_generation_request && second_generation_request->has_value(),
           "Second wake generation must enqueue after manual Tick invalidates the first");

    SDL_Event newer_wake_event{};
    bool newer_wake_captured = false;
    for (int attempt = 0; attempt < 20 && !newer_wake_captured; ++attempt) {
        SDL_Event event{};
        if (!SDL_PollEvent(&event)) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
            continue;
        }
        if (event.type == stale_wake_event.type) {
            newer_wake_event = event;
            newer_wake_captured = true;
        } else {
            Expect(dispatcher_app.DispatchEvent(event).has_value(),
                   "Events preceding the newer wake generation must remain dispatchable");
        }
    }
    Expect(newer_wake_captured, "The newer wake generation must be captured for ordering coverage");

    const Uint32 dispatcher_wake_event_type = stale_wake_event.type;
    const auto stale_dispatch = dispatcher_app.DispatchEvent(stale_wake_event);
    Expect(stale_dispatch && stale_dispatch->consumed,
           "The captured stale generation must be consumed without disarming the newer wake");

    wake_filter_calls = 0;
    SDL_SetEventFilter(CountAndRejectEvent, &wake_filter_calls);
    const int coalesced_filter_calls_before = wake_filter_calls.load();
    std::optional<UiResult<void>> coalesced_exit_request;
    std::thread coalesced_exit_thread([&] {
        coalesced_exit_request.emplace(dispatcher.RequestExit());
    });
    coalesced_exit_thread.join();
    SDL_SetEventFilter(nullptr, nullptr);
    Expect(coalesced_exit_request && coalesced_exit_request->has_value() &&
               wake_filter_calls == coalesced_filter_calls_before,
           "A stale event must not disarm the newer generation or trigger a redundant wake");

    int redundant_internal_wakes = 0;
    SDL_Event queued_event{};
    while (SDL_PollEvent(&queued_event)) {
        const auto dispatched = dispatcher_app.DispatchEvent(queued_event);
        Expect(dispatched.has_value(), "Queued events must remain dispatchable after stale wake handling");
        redundant_internal_wakes += queued_event.type == dispatcher_wake_event_type ? 1 : 0;
    }
    Expect(redundant_internal_wakes == 0,
           "A request coalesced onto the captured newer generation must not enqueue a third wake");

    const auto newer_dispatch = dispatcher_app.DispatchEvent(newer_wake_event);
    Expect(newer_dispatch && newer_dispatch->consumed,
           "The newer wake generation must remain valid after stale event consumption");

    const auto generation_exit_tick = dispatcher_app.Tick();
    Expect(generation_exit_tick && generation_exit_tick->exit_requested,
           "Exit intent coalesced onto the newer wake generation must survive");
    Expect(dispatcher_app.Shutdown().has_value(), "Dispatcher test application must shut down");
    Expect(!dispatcher.IsOpen(), "Dispatcher copy must close when application shuts down");

    UiApp queue_app;
    auto queue_config = MakeConfig();
    queue_config.command_queue.max_pending = 1;
    Expect(queue_app.Initialize(std::move(queue_config)).has_value(), "Queue-capacity test app must initialize");
    UiDispatcher limited_dispatcher = queue_app.Dispatcher();
    auto pending = limited_dispatcher.Post([](UiApp&) -> UiResult<void> { return {}; });
    Expect(pending.has_value(), "First bounded-queue command must enqueue");
    auto full = limited_dispatcher.Post([](UiApp&) -> UiResult<void> { return {}; });
    Expect(!full && full.error().code == UiErrorCode::QueueFull, "Second bounded-queue command must return QueueFull");
    Expect(queue_app.Shutdown().has_value(), "Queue-capacity app must shut down");
    auto cancelled = pending->Wait();
    Expect(!cancelled && cancelled.error().code == UiErrorCode::Cancelled,
           "Pending command must be cancelled during shutdown");

    UiApp draining_capacity_app;
    auto draining_capacity_config = MakeConfig();
    draining_capacity_config.command_queue.max_pending = 2;
    draining_capacity_config.command_queue.max_commands_per_tick = 2;
    Expect(draining_capacity_app.Initialize(std::move(draining_capacity_config)).has_value(),
           "Drain-capacity test app must initialize");
    UiDispatcher draining_dispatcher = draining_capacity_app.Dispatcher();
    std::optional<UiErrorCode> nested_capacity_error;
    auto capacity_first = draining_dispatcher.Post([&](UiApp& command_app) -> UiResult<void> {
        auto nested = command_app.Dispatcher().Post([](UiApp&) -> UiResult<void> { return {}; });
        if (!nested) {
            nested_capacity_error = nested.error().code;
        }
        return {};
    });
    auto capacity_second = draining_dispatcher.Post([](UiApp&) -> UiResult<void> { return {}; });
    Expect(capacity_first && capacity_second, "Drain-capacity queue must accept commands up to its limit");
    Expect(draining_capacity_app.Tick().has_value(), "Drain-capacity commands must execute");
    Expect(nested_capacity_error == UiErrorCode::QueueFull,
           "Commands being drained must continue to count against queue capacity");
    Expect(draining_capacity_app.Shutdown().has_value(), "Drain-capacity app must shut down");

    const auto settings_path = std::filesystem::temp_directory_path() /
        ("unigui-layout-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".settings");
    UiAppConfig layout_config = MakeConfig();
    layout_config.docking.enabled = true;
    layout_config.docking.layouts = {MakeDockLayout("split", true), MakeDockLayout("single", false)};
    layout_config.docking.initial_layout = "split";
    layout_config.persistence.enabled = true;
    layout_config.persistence.path = settings_path.string();
    layout_config.persistence.save_debounce = std::chrono::milliseconds{0};

    UiApp layout_app;
    Expect(layout_app.Initialize(layout_config).has_value(), "Layout application must initialize");
    Expect(layout_app.Tick().has_value() && layout_app.ActiveDockLayout() == "split",
           "Initial declarative layout must apply");
    Expect(layout_app.ActivateDockLayout("single", UiDockApplyMode::ResetToDefinition).has_value(),
           "Dynamic layout activation must succeed");
    Expect(layout_app.Tick().has_value() && layout_app.ActiveDockLayout() == "single",
           "Activated layout must become current");
    Expect(layout_app.SaveSettingsNow().has_value() && std::filesystem::exists(settings_path),
           "Named layouts must persist atomically");
    Expect(layout_app.Shutdown().has_value(), "Layout application must shut down");

    UiApp restored_layout_app;
    Expect(restored_layout_app.Initialize(layout_config).has_value(), "Persisted layout application must initialize");
    Expect(restored_layout_app.Tick().has_value() && restored_layout_app.ActiveDockLayout() == "single",
           "Persisted active layout must restore");
    Expect(restored_layout_app.Shutdown().has_value(), "Restored layout application must shut down");
    std::error_code remove_error;
    std::filesystem::remove(settings_path, remove_error);

    {
        std::ofstream corrupt(settings_path, std::ios::binary | std::ios::trunc);
        corrupt << "corrupt";
    }
    auto tolerant_config = MakeConfig();
    tolerant_config.persistence.enabled = true;
    tolerant_config.persistence.path = settings_path.string();
    tolerant_config.persistence.fail_on_load_error = false;
    UiApp tolerant_app;
    Expect(tolerant_app.Initialize(tolerant_config).has_value(),
           "Non-strict persistence load must tolerate malformed settings");
    Expect(tolerant_app.Shutdown().has_value(), "Tolerant persistence app must shut down");
    {
        std::ifstream preserved(settings_path, std::ios::binary);
        const std::string contents{
            std::istreambuf_iterator<char>{preserved},
            std::istreambuf_iterator<char>{}};
        Expect(contents == "corrupt", "Tolerant shutdown must not overwrite malformed settings");
    }

    {
        std::ofstream corrupt(settings_path, std::ios::binary | std::ios::trunc);
        corrupt << "corrupt";
    }
    tolerant_config.persistence.fail_on_load_error = true;
    UiApp strict_app;
    const auto strict_initialization = strict_app.Initialize(std::move(tolerant_config));
    Expect(!strict_initialization && strict_initialization.error().code == UiErrorCode::PersistenceFormat,
           "Strict persistence load must fail on malformed settings");
    Expect(strict_app.State() == UiLifecycleState::Failed,
           "Strict persistence load failure must enter Failed state");
    Expect(strict_app.Shutdown().has_value(), "Strict persistence failure must support cleanup");
    std::filesystem::remove(settings_path, remove_error);

    return EXIT_SUCCESS;
}
