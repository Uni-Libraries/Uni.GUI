#pragma once

#include <uni/gui/asset.h>
#include <uni/gui/dispatcher.h>
#include <uni/gui/display.h>
#include <uni/gui/event.h>
#include <uni/gui/frame.h>
#include <uni/gui/layout.h>

#include <chrono>
#include <string>

namespace Uni::GUI {

/// Backend selection. Automatic tries SDL_GPU first and can fall back to SDL_Renderer.
enum class UiRendererPreference {
    Automatic,
    SdlGpu,
    SdlRenderer,
};

/// Policy for optional backend features; Required makes lack of support an initialization error.
enum class UiFeaturePolicy {
    Disabled,
    IfSupported,
    Required,
};

/// Requested presentation synchronization mode.
enum class UiVsyncMode {
    Disabled,
    Enabled,
};

/// Initial fallback font. Runtime fonts can be added later with UiApp::CreateFont().
struct UiFontConfig final {
    std::string path; ///< Empty selects embedded Roboto Medium; otherwise a font file path.
    float size{16.0f}; ///< Positive, finite base font size in reference UI units.
    UiGlyphRange glyph_range{UiGlyphRange::Default};
};

/// Storage for per-layout ImGui snapshots. A missing file is not an error.
/// Saves create a new sibling temporary file and replace the destination only after
/// a successful write and close. Use an application-controlled directory; independent
/// writers do not merge settings. The serialized envelope is limited to 16 MiB.
struct UiPersistenceConfig final {
    bool enabled{true};
    std::string path{"unigui.settings"}; ///< Non-empty and NUL-free when persistence is enabled.
    std::chrono::milliseconds save_debounce{1000}; ///< Non-negative delay before best-effort automatic saving.
    bool fail_on_load_error{}; ///< Fail initialization on load errors rather than logging and suppressing automatic writes.
};

/// Owned configuration passed by value to UiApp::Initialize(). Initial dimensions are in reference UI units.
struct UiAppConfig final {
    std::string title{"Uni.GUI"};
    int initial_width{1280};
    int initial_height{720};
    UiRendererPreference renderer{UiRendererPreference::Automatic};
    UiFeaturePolicy viewports{UiFeaturePolicy::IfSupported};
    UiVsyncMode vsync{UiVsyncMode::Enabled};
    UiFontConfig font;
    UiScalingConfig scaling;
    UiPersistenceConfig persistence;
    UiFramePolicy frame_policy;
    UiCommandQueueConfig command_queue;
    UiDockingConfig docking;
    UiAssetLimits asset_limits;
    UiEventHooks event_hooks;
};

} // namespace Uni::GUI
