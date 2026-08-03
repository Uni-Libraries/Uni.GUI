#include <uni/gui/app.h>

#include "ui_font.h"
#include "ui_asset_manager.h"
#include "ui_dispatcher_internal.h"
#include "ui_dock_layout.h"
#include "ui_element_registry.h"
#include "ui_renderer.h"
#include "ui_renderer_sdl.h"
#include "ui_renderer_sdlgpu.h"
#include "ui_scale.h"
#include "ui_texture_internal.h"
#include "ui_winsys.h"
#include "ui_winsys_sdl.h"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#if defined(UNIGUI_TEST_ENGINE_TEST_BUILD)
#include "ui_test_engine_bridge.h"
#endif

namespace Uni::GUI {

namespace {

[[nodiscard]] UiError MakeError(const UiErrorCode code, std::string message, const bool append_sdl_error = false) {
    if (append_sdl_error) {
        const char* const sdl_error = SDL_GetError();
        if (sdl_error && *sdl_error != '\0') {
            message.append(": ").append(sdl_error);
        }
    }
    return UiError{code, std::move(message)};
}

[[nodiscard]] std::string DescribeEvent(const SDL_Event& event) {
    std::array<char, 256> description{};
    if (SDL_GetEventDescription(&event, description.data(), static_cast<int>(description.size())) > 0) {
        return description.data();
    }
    return std::string{"SDL event type "}.append(std::to_string(event.type));
}

class ScopedUiContext final {
public:
    ScopedUiContext(ImGuiContext* imgui_context, ImPlotContext* implot_context)
        : m_previous_imgui(ImGui::GetCurrentContext()),
          m_previous_implot(ImPlot::GetCurrentContext()) {
        ImGui::SetCurrentContext(imgui_context);
        ImPlot::SetCurrentContext(implot_context);
    }

    ~ScopedUiContext() {
        ImPlot::SetCurrentContext(m_previous_implot);
        ImGui::SetCurrentContext(m_previous_imgui);
    }

private:
    ImGuiContext* m_previous_imgui{};
    ImPlotContext* m_previous_implot{};
};

class RestoreExternalUiContext final {
public:
    RestoreExternalUiContext()
        : m_previous_imgui(ImGui::GetCurrentContext()),
          m_previous_implot(ImPlot::GetCurrentContext()) {}

    ~RestoreExternalUiContext() {
        ImPlot::SetCurrentContext(m_previous_implot);
        ImGui::SetCurrentContext(m_previous_imgui);
    }

private:
    ImGuiContext* m_previous_imgui{};
    ImPlotContext* m_previous_implot{};
};

class FrameActivityGuard final {
public:
    explicit FrameActivityGuard(bool& active) : m_active(active) {
        m_active = true;
    }

    ~FrameActivityGuard() {
        m_active = false;
    }

    void Release() noexcept {
        m_active = false;
    }

private:
    bool& m_active;
};

} // namespace

struct UiApp::Impl final {
    struct InitializationRollback final {
        Impl& owner;
        bool committed{};

        ~InitializationRollback() {
            if (!committed) {
                (void)owner.Cleanup(UiLifecycleState::Failed);
            }
        }

        void Commit() noexcept {
            committed = true;
        }
    };

    UiLifecycleState lifecycle{UiLifecycleState::Empty};
    UiAppConfig config;
    std::thread::id main_thread;
    std::unique_ptr<UiWinsys> winsys;
    std::unique_ptr<UiRenderer> renderer;
    Detail::UiElementRegistry element_registry;
    Detail::UiDockLayoutManager dock_layouts;
    Detail::UiAssetManager assets;
    std::shared_ptr<Detail::UiDispatcherState> dispatcher;
    bool frame_active{};
    bool imgui_frame_active{};
    bool event_dispatch_active{};
    ImGuiContext* imgui_context{};
    ImPlotContext* implot_context{};
    std::shared_ptr<Detail::UiTextureStore> texture_store;
    std::uint64_t frame_index{};
    UiFrameDemand previous_frame_demand{UiFrameDemand::None};
    std::chrono::steady_clock::time_point initialized_at{};
    std::chrono::steady_clock::time_point last_activity{};
    std::chrono::steady_clock::time_point last_render{};
    ImGuiStyle reference_style{};
    UiDisplayMetrics display_metrics{};
    bool has_display_metrics{false};

    void ApplyDisplayScale() {
        ImGuiStyle scaled_style = reference_style;
        scaled_style.ScaleAllSizes(display_metrics.applied_ui_scale);
        scaled_style.FontScaleDpi = display_metrics.applied_ui_scale;
        if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
            scaled_style.WindowRounding = 0.0f;
            scaled_style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
        ImGui::GetStyle() = scaled_style;
    }

    [[nodiscard]] UiResult<bool> RefreshDisplayMetrics(const bool required) {
        const auto window_metrics = winsys ? winsys->QueryDisplayMetrics() : std::nullopt;
        if (!window_metrics) {
            if (required) {
                return std::unexpected(MakeError(
                    UiErrorCode::WindowCreation,
                    "Failed to query main-window display metrics",
                    true));
            }
            return false;
        }
        const std::uint64_t revision = has_display_metrics ? display_metrics.revision + 1 : 1;
        if (revision == 0) {
            return std::unexpected(MakeError(UiErrorCode::InvalidState, "Display metrics revision is exhausted"));
        }
        auto resolved = Detail::ResolveDisplayMetrics(*window_metrics, config.scaling, revision);
        if (!resolved) {
            if (required) {
                return std::unexpected(MakeError(
                    UiErrorCode::InvalidArgument,
                    "Display metrics or scaling configuration is invalid"));
            }
            return false;
        }
        if (has_display_metrics && Detail::SameDisplayMetrics(display_metrics, *resolved)) {
            return false;
        }
        display_metrics = *resolved;
        has_display_metrics = true;
        if (imgui_context) {
            ApplyDisplayScale();
        }
        return true;
    }

    [[nodiscard]] std::expected<void, UiError> Initialize(UiAppConfig new_config) {
        if (lifecycle != UiLifecycleState::Empty) {
            return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not empty"));
        }
        if (new_config.initial_width <= 0 || new_config.initial_height <= 0) {
            return std::unexpected(MakeError(
                UiErrorCode::InvalidArgument,
                "Initial window dimensions must be positive"));
        }
        if (!std::isfinite(new_config.font.size) || new_config.font.size <= 0.0f) {
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Font size must be positive and finite"));
        }
        if (!Detail::ValidScalingConfig(new_config.scaling)) {
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Scaling configuration is invalid"));
        }
        switch (new_config.renderer) {
        case UiRendererPreference::Automatic:
        case UiRendererPreference::SdlGpu:
        case UiRendererPreference::SdlRenderer:
            break;
        default:
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Renderer preference is invalid"));
        }
        switch (new_config.viewports) {
        case UiFeaturePolicy::Disabled:
        case UiFeaturePolicy::IfSupported:
        case UiFeaturePolicy::Required:
            break;
        default:
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Viewport policy is invalid"));
        }
        switch (new_config.vsync) {
        case UiVsyncMode::Disabled:
        case UiVsyncMode::Enabled:
            break;
        default:
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "VSync mode is invalid"));
        }
        switch (new_config.font.glyph_range) {
        case UiGlyphRange::Default:
        case UiGlyphRange::Cyrillic:
            break;
        default:
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Font glyph range is invalid"));
        }
        const auto valid_loop_rate = [](const UiLoopRate& rate) {
            switch (rate.mode) {
            case UiLoopMode::Continuous:
            case UiLoopMode::WaitForEvent:
                return true;
            case UiLoopMode::RateLimited: {
                if (!std::isfinite(rate.frames_per_second) || rate.frames_per_second <= 0.0) {
                    return false;
                }
                const std::chrono::duration<long double> period{
                    1.0L / static_cast<long double>(rate.frames_per_second)};
                return period >= std::chrono::duration<long double>{std::chrono::steady_clock::duration{1}} &&
                       period < std::chrono::duration<long double>{std::chrono::steady_clock::duration::max()};
            }
            }
            return false;
        };
        const auto valid_clock_delay = [](const std::chrono::milliseconds delay) {
            return delay.count() >= 0 &&
                   std::chrono::duration<long double>{delay} <
                       std::chrono::duration<long double>{std::chrono::steady_clock::duration::max()};
        };
        if (!valid_loop_rate(new_config.frame_policy.active) ||
            !valid_loop_rate(new_config.frame_policy.idle) ||
            !valid_loop_rate(new_config.frame_policy.minimized) ||
            !valid_clock_delay(new_config.frame_policy.idle_after)) {
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Frame policy is invalid"));
        }
        if (new_config.command_queue.max_pending == 0 ||
            new_config.command_queue.max_commands_per_tick == 0 ||
            new_config.command_queue.execution_budget.count() <= 0) {
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Command queue limits must be positive"));
        }
        if (new_config.asset_limits.max_asset_bytes == 0 ||
            new_config.asset_limits.max_assets == 0 ||
            new_config.asset_limits.max_fonts == 0) {
            return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Asset limits must be positive"));
        }

        lifecycle = UiLifecycleState::Initializing;
        InitializationRollback rollback{*this};
        main_thread = std::this_thread::get_id();
        config = std::move(new_config);

        const auto fail = [](UiError error) -> std::expected<void, UiError> {
            return std::unexpected(std::move(error));
        };

        if (auto configured = dock_layouts.Configure(config.docking, config.persistence); !configured) {
            return fail(std::move(configured.error()));
        }

        winsys = std::make_unique<UiWinsysSdl>();
        const UiWinsysInitResult winsys_result = winsys->Init(config);
        if (winsys_result != UiWinsysInitResult::Success) {
            const bool window_failed = winsys_result == UiWinsysInitResult::WindowCreationFailed;
            return fail(MakeError(
                window_failed ? UiErrorCode::WindowCreation : UiErrorCode::SdlInitialization,
                window_failed ? "Failed to create SDL window" : "Failed to initialize SDL windowing",
                true));
        }

        const auto initialize_sdl_renderer = [this]() -> bool {
            auto candidate = std::make_unique<UiRendererSdl>();
            if (!candidate->Init(winsys->GetHandle())) {
                return false;
            }
            renderer = std::move(candidate);
            return true;
        };
        const auto initialize_gpu_renderer = [this]() -> bool {
            auto candidate = std::make_unique<UiRendererSdlGpu>();
            if (!candidate->Init(winsys->GetHandle())) {
                return false;
            }
            renderer = std::move(candidate);
            return true;
        };

        switch (config.renderer) {
        case UiRendererPreference::Automatic:
            if (!initialize_gpu_renderer() && !initialize_sdl_renderer()) {
                return fail(MakeError(UiErrorCode::RendererUnavailable, "No SDL renderer is available", true));
            }
            break;
        case UiRendererPreference::SdlGpu:
            if (!initialize_gpu_renderer()) {
                return fail(MakeError(UiErrorCode::RendererUnavailable, "SDL_GPU renderer is unavailable", true));
            }
            break;
        case UiRendererPreference::SdlRenderer:
            if (!initialize_sdl_renderer()) {
                return fail(MakeError(UiErrorCode::RendererUnavailable, "SDL_Renderer is unavailable", true));
            }
            break;
        }

        RestoreExternalUiContext restore_external_context;
        IMGUI_CHECKVERSION();
        imgui_context = ImGui::CreateContext();
        if (!imgui_context) {
            return fail(MakeError(UiErrorCode::BackendInitialization, "Failed to create Dear ImGui context"));
        }
        ImGui::SetCurrentContext(imgui_context);

        implot_context = ImPlot::CreateContext();
        if (!implot_context) {
            return fail(MakeError(UiErrorCode::BackendInitialization, "Failed to create ImPlot context"));
        }
        ImPlot::SetCurrentContext(implot_context);

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr;

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        reference_style = style;
        reference_style.FontSizeBase = config.font.size;

        const ImWchar* glyph_ranges = config.font.glyph_range == UiGlyphRange::Cyrillic
            ? io.Fonts->GetGlyphRangesCyrillic()
            : nullptr;
        ImFont* font = nullptr;
        if (config.font.path.empty()) {
            font = io.Fonts->AddFontFromMemoryCompressedBase85TTF(
                Font::GetRobotoMedium(),
                config.font.size,
                nullptr,
                glyph_ranges);
        } else {
            font = io.Fonts->AddFontFromFileTTF(
                config.font.path.c_str(),
                config.font.size,
                nullptr,
                glyph_ranges);
        }
        if (!font) {
            return fail(MakeError(UiErrorCode::FontInitialization, "Failed to load the configured font"));
        }

        const int vsync_interval = config.vsync == UiVsyncMode::Enabled ? 1 : 0;
        const auto can_fallback_to_sdl = [this]() {
            return config.renderer == UiRendererPreference::Automatic &&
                   dynamic_cast<UiRendererSdlGpu*>(renderer.get()) != nullptr;
        };
        const auto fallback_to_sdl = [this, &initialize_sdl_renderer, vsync_interval]() {
            renderer.reset();
            return initialize_sdl_renderer() && renderer->SetVsync(vsync_interval);
        };

        if (!renderer->SetVsync(vsync_interval)) {
            if (!can_fallback_to_sdl() || !fallback_to_sdl()) {
                return fail(MakeError(UiErrorCode::RendererInitialization, "Failed to configure VSync", true));
            }
        }

        if (!renderer->InitImgui()) {
            if (!can_fallback_to_sdl()) {
                return fail(MakeError(UiErrorCode::BackendInitialization, "Failed to initialize Dear ImGui renderer backend", true));
            }

            if (!fallback_to_sdl() || !renderer->InitImgui()) {
                return fail(MakeError(UiErrorCode::BackendInitialization, "Failed to initialize SDL_Renderer fallback", true));
            }
        }

        const bool viewports_supported =
            (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0 &&
            (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports) != 0;
        if (config.viewports == UiFeaturePolicy::Required && !viewports_supported) {
            return fail(MakeError(UiErrorCode::BackendInitialization, "Multi-viewport support is required but unavailable"));
        }
        if (config.viewports != UiFeaturePolicy::Disabled && viewports_supported) {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigDpiScaleFonts = false;
            io.ConfigDpiScaleViewports = config.scaling.mode == UiScaleMode::Automatic;
        }
        auto display_ready = RefreshDisplayMetrics(true);
        if (!display_ready) {
            return fail(std::move(display_ready.error()));
        }

        assets.Configure(config.asset_limits, font);
        if (auto loaded = dock_layouts.Load(); !loaded) {
            if (config.persistence.fail_on_load_error) {
                return fail(std::move(loaded.error()));
            }
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", loaded.error().message.c_str());
        }

        texture_store = std::make_shared<Detail::UiTextureStore>();
        texture_store->context = imgui_context;
        texture_store->active = true;
        const std::uint32_t wake_event_type = SDL_RegisterEvents(1);
        if (wake_event_type == 0) {
            return fail(MakeError(UiErrorCode::WakeupFailed, "Failed to reserve the dispatcher wake event", true));
        }
        dispatcher = Detail::CreateDispatcherState(config.command_queue, main_thread, wake_event_type);

        initialized_at = std::chrono::steady_clock::now();
        last_activity = initialized_at;
        last_render = {};
        frame_index = 0;
        previous_frame_demand = UiFrameDemand::None;

#if defined(UNIGUI_TEST_ENGINE_TEST_BUILD)
        Detail::StartUiTestEngine(imgui_context);
#endif

        winsys->Show();
        lifecycle = UiLifecycleState::Ready;
        rollback.Commit();
        return {};
    }

    UiResult<void> Cleanup(const UiLifecycleState final_state) {
        if (lifecycle == UiLifecycleState::ShuttingDown) {
            return {};
        }
        if (lifecycle == UiLifecycleState::Empty && final_state == UiLifecycleState::Empty) {
            return {};
        }
        const bool save_settings = lifecycle == UiLifecycleState::Ready || lifecycle == UiLifecycleState::ExitRequested;
        lifecycle = UiLifecycleState::ShuttingDown;
        Detail::CloseDispatcher(dispatcher);

        ImGuiContext* const owned_imgui = imgui_context;
        ImPlotContext* const owned_implot = implot_context;
        ImGuiContext* const previous_imgui = ImGui::GetCurrentContext();
        ImPlotContext* const previous_implot = ImPlot::GetCurrentContext();

        if (owned_imgui) {
            ImGui::SetCurrentContext(owned_imgui);
        }
        if (owned_implot) {
            ImPlot::SetCurrentContext(owned_implot);
        }

        std::optional<UiError> shutdown_error;
        if (save_settings && owned_imgui) {
            if (auto saved = dock_layouts.SaveNow(); !saved) {
                shutdown_error = std::move(saved.error());
            }
        }

        element_registry.Clear();
        if (owned_imgui && ImGui::GetIO().Fonts) {
            ImGui::GetIO().Fonts->ClearFonts();
        }
        assets.Reset();
        dock_layouts.Reset();

        if (texture_store) {
            texture_store->active = false;
            for (auto& entry : texture_store->textures) {
                entry.data->WantDestroyNextFrame = true;
            }
        }

        if (renderer) {
            renderer->ShutdownImgui();
        }

        if (texture_store && owned_imgui) {
            ImGui::GetPlatformIO().Textures.resize(0);
            for (auto& entry : texture_store->textures) {
                if (entry.registered) {
                    ImGui::UnregisterUserTexture(entry.data.get());
                }
            }
            texture_store->textures.clear();
            texture_store->context = nullptr;
        }
        texture_store.reset();

        if (owned_implot) {
            ImPlot::DestroyContext(owned_implot);
            implot_context = nullptr;
        }
        if (owned_imgui) {
            ImGui::DestroyContext(owned_imgui);
            imgui_context = nullptr;
        }

        renderer.reset();
        winsys.reset();
        dispatcher.reset();
        config = {};
        reference_style = {};
        display_metrics = {};
        has_display_metrics = false;
        frame_active = false;
        imgui_frame_active = false;
        event_dispatch_active = false;
        frame_index = 0;
        previous_frame_demand = UiFrameDemand::None;
        initialized_at = {};
        last_activity = {};
        last_render = {};
        if (final_state == UiLifecycleState::Empty) {
            main_thread = {};
        }

        ImPlot::SetCurrentContext(previous_implot == owned_implot ? nullptr : previous_implot);
        ImGui::SetCurrentContext(previous_imgui == owned_imgui ? nullptr : previous_imgui);
        lifecycle = final_state;
        if (shutdown_error) {
            return std::unexpected(std::move(*shutdown_error));
        }
        return {};
    }

    void CollectDestroyedTexturesAfterNewFrame() {
        if (!texture_store) {
            return;
        }
        for (auto& entry : texture_store->textures) {
            ImTextureData* const data = entry.data.get();
            if (entry.registered &&
                data->WantDestroyNextFrame &&
                data->Status == ImTextureStatus_Destroyed &&
                data->TexID == ImTextureID_Invalid &&
                data->BackendUserData == nullptr &&
                data->QueueUserData == nullptr) {
                ImGui::UnregisterUserTexture(data);
                entry.registered = false;
            }
        }
    }

    void ReleaseDestroyedTexturesAfterRender() {
        if (texture_store) {
            std::erase_if(texture_store->textures, [](const Detail::UiTextureEntry& entry) {
                return !entry.registered;
            });
        }
    }

};

UiApp::UiApp() : m_impl(std::make_unique<Impl>()) {}

UiApp::~UiApp() {
    if (m_impl->lifecycle != UiLifecycleState::Empty && !IsMainThread()) {
        Detail::CloseDispatcher(m_impl->dispatcher);
        std::terminate();
    }
    try {
        (void)Shutdown();
    } catch (...) {
    }
}

UiResult<void> UiApp::Initialize(UiAppConfig config) {
    return m_impl->Initialize(std::move(config));
}

UiResult<void> UiApp::Shutdown() {
    if (m_impl->lifecycle != UiLifecycleState::Empty && !IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "Shutdown must run on the UI thread"));
    }
    if (m_impl->frame_active || m_impl->event_dispatch_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Shutdown cannot run during Tick or event dispatch"));
    }
    return m_impl->Cleanup(UiLifecycleState::Empty);
}

UiResult<UiTickResult> UiApp::Tick() {
    if (m_impl->lifecycle != UiLifecycleState::Ready && m_impl->lifecycle != UiLifecycleState::ExitRequested) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "Tick must run on the UI thread"));
    }
    if (m_impl->lifecycle == UiLifecycleState::ExitRequested) {
        return UiTickResult{false, true, UiLoopRate{UiLoopMode::WaitForEvent, 0.0}, m_impl->frame_index};
    }
    if (!m_impl->renderer ||
        !m_impl->winsys ||
        !m_impl->imgui_context) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (m_impl->frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Tick is not reentrant"));
    }

    FrameActivityGuard frame_activity(m_impl->frame_active);
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);

    const auto now = std::chrono::steady_clock::now();
    auto metrics_changed = m_impl->RefreshDisplayMetrics(false);
    if (!metrics_changed) {
        return std::unexpected(std::move(metrics_changed.error()));
    }
    auto drained = Detail::DrainDispatcher(m_impl->dispatcher, *this);
    if (!drained) {
        return std::unexpected(std::move(drained.error()));
    }
    const bool frame_requested = Detail::ConsumeFrameRequest(m_impl->dispatcher);
    if (*drained > 0 || frame_requested || *metrics_changed) {
        m_impl->last_activity = now;
    }
    if (Detail::ConsumeExitRequest(m_impl->dispatcher)) {
        m_impl->lifecycle = UiLifecycleState::ExitRequested;
        Detail::CloseDispatcher(m_impl->dispatcher);
        return UiTickResult{false, true, UiLoopRate{UiLoopMode::WaitForEvent, 0.0}, m_impl->frame_index};
    }

    const bool minimized = m_impl->winsys->IsMinimized();
    const bool idle = now - m_impl->last_activity >= m_impl->config.frame_policy.idle_after;
    UiLoopRate next_iteration = minimized
        ? m_impl->config.frame_policy.minimized
        : (idle ? m_impl->config.frame_policy.idle : m_impl->config.frame_policy.active);

    bool should_render = m_impl->last_render.time_since_epoch().count() == 0 ||
                         frame_requested ||
                         *metrics_changed ||
                         m_impl->previous_frame_demand == UiFrameDemand::Continuous ||
                         m_impl->dock_layouts.HasPendingWork();
    if (minimized && !m_impl->config.frame_policy.render_while_minimized) {
        should_render = false;
    } else if (!should_render) {
        switch (next_iteration.mode) {
        case UiLoopMode::Continuous:
            should_render = true;
            break;
        case UiLoopMode::RateLimited:
            should_render = now - m_impl->last_render >=
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<long double>{
                        1.0L / static_cast<long double>(next_iteration.frames_per_second)});
            break;
        case UiLoopMode::WaitForEvent:
            break;
        }
    }

    if (!should_render) {
        if (auto maintained = m_impl->dock_layouts.MaintainAfterFrame(now); !maintained) {
            return std::unexpected(std::move(maintained.error()));
        }
        if (m_impl->dock_layouts.HasPendingWork() && next_iteration.mode == UiLoopMode::WaitForEvent) {
            next_iteration = UiLoopRate{UiLoopMode::RateLimited, 10.0};
        }
        return UiTickResult{false, false, next_iteration, m_impl->frame_index};
    }

    if (auto prepared = m_impl->dock_layouts.PrepareBeforeFrame(); !prepared) {
        return std::unexpected(std::move(prepared.error()));
    }

    m_impl->renderer->NewFrame();
    m_impl->winsys->NewFrame();
    ImGui::NewFrame();
    m_impl->imgui_frame_active = true;
    m_impl->CollectDestroyedTexturesAfterNewFrame();

    if (auto dockspace = m_impl->dock_layouts.SubmitDockspace(); !dockspace) {
        ImGui::EndFrame();
        m_impl->imgui_frame_active = false;
        return std::unexpected(std::move(dockspace.error()));
    }

    ImGuiErrorRecoveryState recovery_state;
    ImGui::ErrorRecoveryStoreState(&recovery_state);
    const auto delta_time = m_impl->last_render.time_since_epoch().count() == 0
        ? std::chrono::nanoseconds{0}
        : std::chrono::duration_cast<std::chrono::nanoseconds>(now - m_impl->last_render);
    if (auto updated = m_impl->element_registry.Update(*this, m_impl->frame_index + 1, delta_time); !updated) {
        ImGuiIO& io = ImGui::GetIO();
        const bool recovery_asserts = io.ConfigErrorRecoveryEnableAssert;
        io.ConfigErrorRecoveryEnableAssert = false;
        ImGui::ErrorRecoveryTryToRecoverState(&recovery_state);
        io.ConfigErrorRecoveryEnableAssert = recovery_asserts;
        ImGui::EndFrame();
        m_impl->imgui_frame_active = false;
        return std::unexpected(std::move(updated.error()));
    } else {
        m_impl->previous_frame_demand = *updated;
    }

    ImGui::Render();
    m_impl->imgui_frame_active = false;
    const bool rendered = m_impl->renderer->Render();
    m_impl->ReleaseDestroyedTexturesAfterRender();
    if (!rendered) {
        m_impl->lifecycle = UiLifecycleState::Failed;
        Detail::CloseDispatcher(m_impl->dispatcher);
        return std::unexpected(MakeError(UiErrorCode::FrameRendering, "Renderer failed to submit the frame", true));
    }

    ++m_impl->frame_index;
    m_impl->last_render = now;
    if (m_impl->previous_frame_demand != UiFrameDemand::None ||
        ImGui::GetIO().WantTextInput ||
        ImGui::GetCurrentContext()->ActiveId != 0) {
        m_impl->last_activity = now;
    }
    if (m_impl->previous_frame_demand == UiFrameDemand::OneMoreFrame) {
        if (auto requested = Detail::RequestFrameWithoutWake(m_impl->dispatcher); !requested) {
            return std::unexpected(std::move(requested.error()));
        }
    } else if (m_impl->previous_frame_demand == UiFrameDemand::Continuous) {
        next_iteration = UiLoopRate{UiLoopMode::Continuous, 0.0};
    }
    if (auto maintained = m_impl->dock_layouts.MaintainAfterFrame(now); !maintained) {
        return std::unexpected(std::move(maintained.error()));
    }
    if (Detail::HasPendingDispatcherWork(m_impl->dispatcher)) {
        next_iteration = UiLoopRate{UiLoopMode::Continuous, 0.0};
    } else if (m_impl->dock_layouts.HasPendingWork() && next_iteration.mode == UiLoopMode::WaitForEvent) {
        next_iteration = UiLoopRate{UiLoopMode::RateLimited, 10.0};
    }
    return UiTickResult{true, false, next_iteration, m_impl->frame_index};
}

UiResult<UiEventDispatchResult> UiApp::DispatchEvent(const SDL_Event& event) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "DispatchEvent must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || !m_impl->winsys || !m_impl->imgui_context) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (Detail::ConsumeDispatcherWakeEvent(m_impl->dispatcher, event)) {
        return UiEventDispatchResult{true, false};
    }
    if (m_impl->frame_active || m_impl->event_dispatch_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Event dispatch is not reentrant"));
    }

    FrameActivityGuard event_activity(m_impl->event_dispatch_active);
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    UiEventAction action = UiEventAction::Pass;
    const bool application_quit_requested = m_impl->winsys->IsApplicationQuitEvent(event);
    const bool main_window_close_requested = m_impl->winsys->IsMainWindowCloseEvent(event);
    bool delivered = false;
    bool recognized = false;
    try {
        if (m_impl->config.event_hooks.before_imgui) {
            UiEventContext event_context{.app = *this,
                                         .event = event,
                                         .application_quit_requested = application_quit_requested,
                                         .main_window_close_requested = main_window_close_requested,
                                         .current_action = action,
                                         .delivered_to_imgui = false,
                                         .recognized_by_imgui = false};
            auto hooked = m_impl->config.event_hooks.before_imgui(event_context);
            if (!hooked) {
                return std::unexpected(std::move(hooked.error()));
            }
            action = *hooked;
        }

        if (action == UiEventAction::Pass) {
            delivered = true;
            recognized = m_impl->winsys->FeedEvent(event);
            if (application_quit_requested || main_window_close_requested) {
                action = UiEventAction::Exit;
            }
        }

        if (m_impl->config.event_hooks.after_imgui) {
            UiEventContext event_context{.app = *this,
                                         .event = event,
                                         .application_quit_requested = application_quit_requested,
                                         .main_window_close_requested = main_window_close_requested,
                                         .current_action = action,
                                         .delivered_to_imgui = delivered,
                                         .recognized_by_imgui = recognized};
            auto hooked = m_impl->config.event_hooks.after_imgui(event_context);
            if (!hooked) {
                return std::unexpected(std::move(hooked.error()));
            }
            if (*hooked == UiEventAction::Exit ||
                (*hooked == UiEventAction::Consume && action == UiEventAction::Pass)) {
                action = *hooked;
            }
        }
    } catch (const std::exception& exception) {
        return std::unexpected(MakeError(
            UiErrorCode::EventHandling,
            std::string{"Event hook threw while handling "}
                .append(DescribeEvent(event))
                .append(": ")
                .append(exception.what())));
    } catch (...) {
        return std::unexpected(MakeError(
            UiErrorCode::EventHandling,
            std::string{"Event hook threw an unknown exception while handling "}.append(DescribeEvent(event))));
    }

    m_impl->last_activity = std::chrono::steady_clock::now();
    if (action == UiEventAction::Exit) {
        m_impl->lifecycle = UiLifecycleState::ExitRequested;
        Detail::CloseDispatcher(m_impl->dispatcher);
    } else if (auto requested = Detail::RequestFrameWithoutWake(m_impl->dispatcher); !requested) {
        return std::unexpected(std::move(requested.error()));
    }
    return UiEventDispatchResult{action != UiEventAction::Pass, action == UiEventAction::Exit};
}

UiResult<void> UiApp::SetEventHooks(UiEventHooks hooks) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "SetEventHooks must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->event_dispatch_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Event hooks cannot be replaced in the current state"));
    }
    m_impl->config.event_hooks = std::move(hooks);
    return {};
}

UiDispatcher UiApp::Dispatcher() const noexcept {
    UiDispatcher dispatcher;
    dispatcher.m_state = m_impl->dispatcher;
    return dispatcher;
}

UiResult<UiElementId> UiApp::AddElement(std::unique_ptr<UiElement> element) {
    if (m_impl->lifecycle != UiLifecycleState::Ready) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "AddElement must run on the UI thread"));
    }
    return m_impl->element_registry.Add(std::move(element));
}

UiResult<bool> UiApp::RemoveElement(const UiElementId id) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "RemoveElement must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (id == InvalidUiElementId) {
        return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "UiElement ID is invalid"));
    }
    return m_impl->element_registry.Remove(id);
}

UiResult<UiTexture> UiApp::CreateTexture(const int width, const int height) {
    if (m_impl->lifecycle != UiLifecycleState::Ready ||
        !m_impl->texture_store ||
        !m_impl->texture_store->active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "CreateTexture must run on the UI thread"));
    }
    if (!Detail::ValidateTextureSize(width, height)) {
        return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "Texture dimensions are invalid or overflow RGBA32 storage"));
    }

    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    if ((ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasTextures) == 0) {
        return std::unexpected(MakeError(UiErrorCode::TextureCreation, "Renderer does not support managed textures"));
    }

    auto data = std::make_unique<ImTextureData>();
    data->Create(ImTextureFormat_RGBA32, width, height);
    ImTextureData* const data_ptr = data.get();
    m_impl->texture_store->textures.push_back(Detail::UiTextureEntry{std::move(data), false});
    ImGui::RegisterUserTexture(data_ptr);
    m_impl->texture_store->textures.back().registered = true;
    UiTexture texture;
    texture.m_impl.reset(new UiTexture::Impl{m_impl->texture_store, data_ptr});
    return texture;
}

UiResult<void> UiApp::SetVsync(const UiVsyncMode mode) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "SetVsync must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || !m_impl->renderer) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    int interval = 0;
    switch (mode) {
    case UiVsyncMode::Disabled:
        interval = 0;
        break;
    case UiVsyncMode::Enabled:
        interval = 1;
        break;
    default:
        return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "VSync mode is invalid"));
    }
    if (m_impl->config.vsync == mode) {
        return {};
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    if (dynamic_cast<UiRendererSdlGpu*>(m_impl->renderer.get()) != nullptr &&
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
        return std::unexpected(MakeError(
            UiErrorCode::InvalidState,
            "Runtime VSync changes are unsupported while SDL_GPU multi-viewport is enabled"));
    }
    if (!m_impl->renderer->SetVsync(interval)) {
        return std::unexpected(MakeError(UiErrorCode::RendererInitialization, "Failed to update VSync", true));
    }
    m_impl->config.vsync = mode;
    return {};
}

UiResult<void> UiApp::SetWindowTitle(std::string title) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "SetWindowTitle must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || !m_impl->winsys) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (m_impl->config.title == title)
        return {};
    if (!m_impl->winsys->SetTitle(title))
        return std::unexpected(MakeError(UiErrorCode::WindowCreation, "Failed to update the window title", true));
    m_impl->config.title = std::move(title);
    return {};
}

UiResult<UiDisplayMetrics> UiApp::DisplayMetrics() const {
    if (m_impl->lifecycle != UiLifecycleState::Ready || !m_impl->has_display_metrics) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "DisplayMetrics must run on the UI thread"));
    }
    return m_impl->display_metrics;
}

UiResult<void> UiApp::SetUserScale(const float scale) {
    if (m_impl->lifecycle != UiLifecycleState::Ready || !m_impl->has_display_metrics ||
        m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UI scale cannot change in the current state"));
    }
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "SetUserScale must run on the UI thread"));
    }
    UiScalingConfig candidate = m_impl->config.scaling;
    candidate.user_scale = scale;
    const auto effective = Detail::ResolveEffectiveUiScale(candidate, m_impl->display_metrics.system_ui_scale);
    if (!effective) {
        return std::unexpected(MakeError(UiErrorCode::InvalidArgument, "UI scale must be positive and finite"));
    }
    if (candidate.user_scale == m_impl->config.scaling.user_scale) {
        return {};
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    m_impl->config.scaling = candidate;
    auto refreshed = m_impl->RefreshDisplayMetrics(true);
    if (!refreshed) {
        return std::unexpected(std::move(refreshed.error()));
    }
    if (auto requested = Detail::RequestFrameWithoutWake(m_impl->dispatcher); !requested) {
        return std::unexpected(std::move(requested.error()));
    }
    return {};
}

UiResult<void> UiApp::SetReferenceStyle(const ImGuiStyle& style) {
    if (m_impl->lifecycle != UiLifecycleState::Ready || !m_impl->imgui_context ||
        !m_impl->has_display_metrics || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Reference style cannot change in the current state"));
    }
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "SetReferenceStyle must run on the UI thread"));
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    m_impl->reference_style = style;
    m_impl->reference_style.FontSizeBase = m_impl->config.font.size;
    m_impl->ApplyDisplayScale();
    if (auto requested = Detail::RequestFrameWithoutWake(m_impl->dispatcher); !requested) {
        return std::unexpected(std::move(requested.error()));
    }
    return {};
}

UiResult<void> UiApp::DefineDockLayout(UiDockLayout layout) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "DefineDockLayout must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    return m_impl->dock_layouts.Define(std::move(layout));
}

UiResult<bool> UiApp::RemoveDockLayout(const std::string_view id) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "RemoveDockLayout must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    return m_impl->dock_layouts.Remove(id);
}

UiResult<void> UiApp::ActivateDockLayout(const std::string_view id, const UiDockApplyMode mode) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "ActivateDockLayout must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "UiApp is not ready"));
    }
    auto activated = m_impl->dock_layouts.Activate(id, mode);
    if (activated) {
        (void)Dispatcher().RequestFrame();
    }
    return activated;
}

std::string UiApp::ActiveDockLayout() const {
    return m_impl->dock_layouts.Active();
}

UiResult<void> UiApp::SaveSettingsNow() {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "SaveSettingsNow must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Settings cannot be saved in the current state"));
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    return m_impl->dock_layouts.SaveNow(true);
}

UiResult<void> UiApp::ReloadSettings() {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "ReloadSettings must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Settings cannot be reloaded in the current state"));
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    auto reloaded = m_impl->dock_layouts.Reload();
    if (reloaded) {
        (void)Dispatcher().RequestFrame();
    }
    return reloaded;
}

UiResult<UiAssetInfo> UiApp::UpsertAsset(UiAssetSpec asset) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "UpsertAsset must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Assets can only change at a frame safe point"));
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    auto upserted = m_impl->assets.Upsert(std::move(asset));
    if (upserted) {
        (void)Dispatcher().RequestFrame();
    }
    return upserted;
}

UiResult<UiAssetInfo> UiApp::FindAsset(const std::string_view key) const {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "FindAsset must run on the UI thread"));
    }
    return m_impl->assets.Find(key);
}

UiResult<bool> UiApp::RemoveAsset(const UiAssetId id) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "RemoveAsset must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Assets can only change at a frame safe point"));
    }
    return m_impl->assets.Remove(id);
}

UiResult<UiFontId> UiApp::CreateFont(UiFontSpec font) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "CreateFont must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Fonts can only change at a frame safe point"));
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    auto created = m_impl->assets.CreateFont(font);
    if (created) {
        (void)Dispatcher().RequestFrame();
    }
    return created;
}

UiResult<void> UiApp::SetDefaultFont(const UiFontId id) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "SetDefaultFont must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Fonts can only change at a frame safe point"));
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    return m_impl->assets.SetDefaultFont(id);
}

UiResult<bool> UiApp::RemoveFont(const UiFontId id) {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "RemoveFont must run on the UI thread"));
    }
    if (m_impl->lifecycle != UiLifecycleState::Ready || m_impl->imgui_frame_active) {
        return std::unexpected(MakeError(UiErrorCode::InvalidState, "Fonts can only change at a frame safe point"));
    }
    ScopedUiContext context(m_impl->imgui_context, m_impl->implot_context);
    return m_impl->assets.RemoveFont(id);
}

UiResult<ImFont*> UiApp::GetFont(const UiFontId id) const {
    if (!IsMainThread()) {
        return std::unexpected(MakeError(UiErrorCode::WrongThread, "GetFont must run on the UI thread"));
    }
    return m_impl->assets.GetFont(id);
}

UiLifecycleState UiApp::State() const noexcept {
    return m_impl->lifecycle;
}

bool UiApp::IsMainThread() const noexcept {
    return m_impl->main_thread != std::thread::id{} && std::this_thread::get_id() == m_impl->main_thread;
}

std::string_view UiApp::RendererName() const noexcept {
    return m_impl->lifecycle == UiLifecycleState::Ready && m_impl->renderer
        ? m_impl->renderer->GetApiName()
        : std::string_view{};
}

} // namespace Uni::GUI
