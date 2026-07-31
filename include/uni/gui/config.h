#pragma once

#include <uni/gui/asset.h>
#include <uni/gui/dispatcher.h>
#include <uni/gui/event.h>
#include <uni/gui/frame.h>
#include <uni/gui/layout.h>

#include <chrono>
#include <string>

namespace Uni::GUI {

enum class UiRendererPreference {
    Automatic,
    SdlGpu,
    SdlRenderer,
};

enum class UiFeaturePolicy {
    Disabled,
    IfSupported,
    Required,
};

enum class UiVsyncMode {
    Disabled,
    Enabled,
};

struct UiFontConfig final {
    std::string path;
    float size_pixels{12.0f};
    UiGlyphRange glyph_range{UiGlyphRange::Cyrillic};
};

struct UiDpiConfig final {
    bool high_density_window{true};
    bool scale_style{true};
    bool scale_fonts{true};
    bool scale_viewports{true};
};

struct UiPersistenceConfig final {
    bool enabled{true};
    std::string path{"unigui.settings"};
    std::chrono::milliseconds save_debounce{1000};
    bool fail_on_load_error{};
};

struct UiAppConfig final {
    std::string title{"Uni.GUI"};
    int initial_width{1280};
    int initial_height{720};
    UiRendererPreference renderer{UiRendererPreference::Automatic};
    UiFeaturePolicy viewports{UiFeaturePolicy::IfSupported};
    UiVsyncMode vsync{UiVsyncMode::Enabled};
    UiFontConfig font;
    UiDpiConfig dpi;
    UiPersistenceConfig persistence;
    UiFramePolicy frame_policy;
    UiCommandQueueConfig command_queue;
    UiDockingConfig docking;
    UiAssetLimits asset_limits;
    UiEventHooks event_hooks;
};

} // namespace Uni::GUI
