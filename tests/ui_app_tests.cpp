#include <uni/gui/app.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

namespace {

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

class EmptyElement final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        return Uni::GUI::UiElementUpdate{};
    }
};

} // namespace

int main() {
    using namespace Uni::GUI;

    UiApp app;
    Expect(app.State() == UiLifecycleState::Empty, "A new application must be empty");
    Expect(app.RendererName().empty(), "An empty application must not expose a renderer");

    auto frame = app.Tick();
    Expect(!frame && frame.error().code == UiErrorCode::InvalidState,
           "Rendering before initialization must return InvalidState");

    auto added = app.AddElement(std::make_unique<EmptyElement>());
    Expect(!added && added.error().code == UiErrorCode::InvalidState,
           "Adding an element before initialization must return InvalidState");

    auto texture = app.CreateTexture(16, 16);
    Expect(!texture && texture.error().code == UiErrorCode::InvalidState,
           "Creating a texture before initialization must return InvalidState");

    UiAppConfig invalid_config;
    invalid_config.initial_width = 0;
    auto initialized = app.Initialize(std::move(invalid_config));
    Expect(!initialized && initialized.error().code == UiErrorCode::InvalidArgument,
           "Invalid configuration must return InvalidArgument");
    Expect(app.State() == UiLifecycleState::Empty,
           "Configuration validation must not enter a partial lifecycle state");

    UiAppConfig oversized_config;
    oversized_config.initial_width = 65536;
    Expect(!app.Initialize(std::move(oversized_config)), "Oversized dimensions must be rejected");

    UiAppConfig non_finite_font;
    non_finite_font.font.size_pixels = std::numeric_limits<float>::infinity();
    Expect(!app.Initialize(std::move(non_finite_font)), "Non-finite font size must be rejected");

    UiAppConfig invalid_renderer;
    invalid_renderer.renderer = static_cast<UiRendererPreference>(999);
    Expect(!app.Initialize(std::move(invalid_renderer)), "Invalid renderer enum must be rejected");

    UiAppConfig invalid_viewports;
    invalid_viewports.viewports = static_cast<UiFeaturePolicy>(999);
    Expect(!app.Initialize(std::move(invalid_viewports)), "Invalid viewport enum must be rejected");

    UiAppConfig invalid_vsync;
    invalid_vsync.vsync = static_cast<UiVsyncMode>(999);
    Expect(!app.Initialize(std::move(invalid_vsync)), "Invalid VSync enum must be rejected");

    UiAppConfig invalid_glyph_range;
    invalid_glyph_range.font.glyph_range = static_cast<UiGlyphRange>(999);
    Expect(!app.Initialize(std::move(invalid_glyph_range)), "Invalid glyph range enum must be rejected");

    UiAppConfig too_slow_frame_rate;
    too_slow_frame_rate.frame_policy.idle = {UiLoopMode::RateLimited, std::numeric_limits<double>::denorm_min()};
    Expect(!app.Initialize(std::move(too_slow_frame_rate)), "Unrepresentably slow frame rate must be rejected");

    UiAppConfig too_fast_frame_rate;
    too_fast_frame_rate.frame_policy.idle = {UiLoopMode::RateLimited, std::numeric_limits<double>::max()};
    Expect(!app.Initialize(std::move(too_fast_frame_rate)), "Unrepresentably fast frame rate must be rejected");

    UiAppConfig excessive_idle_delay;
    excessive_idle_delay.frame_policy.idle_after = std::chrono::milliseconds::max();
    Expect(!app.Initialize(std::move(excessive_idle_delay)), "Unrepresentable idle delay must be rejected");

    UiAppConfig excessive_save_delay;
    excessive_save_delay.persistence.save_debounce = std::chrono::milliseconds::max();
    Expect(!app.Initialize(std::move(excessive_save_delay)), "Unrepresentable save debounce must be rejected");

    Expect(app.Shutdown().has_value(), "First Shutdown must succeed");
    Expect(app.Shutdown().has_value(), "Second Shutdown must succeed");
    Expect(app.State() == UiLifecycleState::Empty, "Shutdown must be idempotent");
    return EXIT_SUCCESS;
}
