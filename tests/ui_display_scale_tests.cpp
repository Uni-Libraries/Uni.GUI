#include "ui_scale.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using namespace Uni::GUI;
using namespace Uni::GUI::Detail;

[[noreturn]] void Fail(const std::string_view message) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
}

void Expect(const bool condition, const std::string_view message) {
    if (!condition) Fail(message);
}

bool Near(const float first, const float second) {
    return std::abs(first - second) < 0.0001f;
}

UiWindowMetrics Metrics(const float display_scale, const float pixel_density) {
    return {
        .window_size = {1280, 720},
        .framebuffer_size = {
            static_cast<int>(1280.0f * pixel_density),
            static_cast<int>(720.0f * pixel_density),
        },
        .display_scale = display_scale,
        .pixel_density = pixel_density,
    };
}

void TestPlatformScaleNormalization() {
    const auto mac = ResolveDisplayMetrics(Metrics(2.0f, 2.0f), UiScalingConfig{}, 1);
    const auto windows = ResolveDisplayMetrics(Metrics(2.0f, 1.0f), UiScalingConfig{}, 2);
    Expect(mac && Near(mac->system_ui_scale, 1.0f) && Near(mac->effective_ui_scale, 1.0f),
           "High-density framebuffer pixels must not double macOS window-coordinate UI sizes");
    Expect(windows && Near(windows->system_ui_scale, 2.0f) && Near(windows->effective_ui_scale, 2.0f),
           "Windows content scale must enlarge UI in native window coordinates");
}

void TestScaleModes() {
    UiScalingConfig fixed{.mode = UiScaleMode::Fixed, .fixed_scale = 1.5f, .user_scale = 1.25f};
    const auto fixed_metrics = ResolveDisplayMetrics(Metrics(2.0f, 1.0f), fixed, 1);
    Expect(fixed_metrics && Near(fixed_metrics->effective_ui_scale, 1.875f) &&
               Near(fixed_metrics->applied_ui_scale, 1.875f),
           "Fixed scaling must ignore system scale and include the user multiplier");

    UiScalingConfig manual{.mode = UiScaleMode::Manual, .user_scale = 1.25f};
    const auto manual_metrics = ResolveDisplayMetrics(Metrics(2.0f, 1.0f), manual, 1);
    Expect(manual_metrics && Near(manual_metrics->effective_ui_scale, 2.5f) &&
               Near(manual_metrics->applied_ui_scale, 1.0f),
           "Manual mode must report the effective scale without applying it to ImGui");
}

void TestValidationAndDimensions() {
    UiScalingConfig invalid;
    invalid.user_scale = std::numeric_limits<float>::quiet_NaN();
    Expect(!ValidScalingConfig(invalid), "Non-finite scale configuration must be rejected");
    Expect(!ResolveDisplayMetrics(Metrics(0.0f, 1.0f), UiScalingConfig{}, 1),
           "Invalid display scale must be rejected");
    Expect(!ResolveDisplayMetrics(Metrics(1.0f, 0.0f), UiScalingConfig{}, 1),
           "Invalid pixel density must be rejected");
    Expect(ScaleInitialWindowDimension(800, 1.25f) == 1000,
           "Initial reference dimensions must be scaled with rounding");
    Expect(!ScaleInitialWindowDimension(0, 1.0f) &&
               !ScaleInitialWindowDimension(std::numeric_limits<int>::max(), 2.0f),
           "Invalid or overflowing initial dimensions must be rejected");
}

void TestSemanticMetricEquality() {
    const auto first = ResolveDisplayMetrics(Metrics(1.25f, 1.0f), UiScalingConfig{}, 1);
    const auto second = ResolveDisplayMetrics(Metrics(1.2505f, 1.0f), UiScalingConfig{}, 2);
    const auto changed = ResolveDisplayMetrics(Metrics(1.5f, 1.0f), UiScalingConfig{}, 3);
    Expect(first && second && changed && SameDisplayMetrics(*first, *second) &&
               !SameDisplayMetrics(*first, *changed),
           "Display metric revisions must ignore insignificant scale jitter");
}

} // namespace

int main() {
    TestPlatformScaleNormalization();
    TestScaleModes();
    TestValidationAndDimensions();
    TestSemanticMetricEquality();
    return EXIT_SUCCESS;
}
