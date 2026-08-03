#include "ui_test_engine_bridge.h"
#include "ui_test_engine_cases.h"

#include <uni/gui/app.h>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_internal.h>

#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {

ImGuiTestEngine* g_engine{};

Uni::GUI::UiAppConfig MakeTestConfig() {
    Uni::GUI::UiAppConfig config;
    config.title = "UniGUI Test Engine";
    config.initial_width = 800;
    config.initial_height = 600;
    config.renderer = Uni::GUI::UiRendererPreference::SdlRenderer;
    config.viewports = Uni::GUI::UiFeaturePolicy::Disabled;
    config.vsync = Uni::GUI::UiVsyncMode::Disabled;
    config.persistence.enabled = false;
    config.scaling.high_pixel_density = false;
    config.scaling.mode = Uni::GUI::UiScaleMode::Fixed;
    config.scaling.fixed_scale = 1.0f;
    config.frame_policy.idle = {Uni::GUI::UiLoopMode::Continuous, 0.0};

    Uni::GUI::UiDockLayout layout;
    layout.id = "test";
    layout.splits.push_back({"root", Uni::GUI::UiDockSide::Left, 0.5f, "left", "right"});
    layout.placements.push_back({"TE Left", "left"});
    layout.placements.push_back({"TE Right", "right"});
    config.docking.layouts.push_back(std::move(layout));
    config.docking.initial_layout = "test";
    return config;
}

} // namespace

namespace Uni::GUI::Detail {

void StartUiTestEngine(ImGuiContext* context) {
    ImGuiTestEngine_Start(g_engine, context);
}

} // namespace Uni::GUI::Detail

int main(int argc, char** argv) {
    std::vector<std::string> filters;
    bool arguments_valid = true;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument.starts_with("--filter=") && argument.size() > 9) {
            filters.push_back(argument.substr(9));
        } else {
            std::cerr << "Unknown or malformed argument: " << argument << '\n';
            arguments_valid = false;
        }
    }
    if (!arguments_valid) return 1;
    if (filters.empty()) filters.emplace_back("tests");

    g_engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& test_io = ImGuiTestEngine_GetIO(g_engine);
    test_io.ConfigSavedSettings = false;
    test_io.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    test_io.ConfigNoThrottle = true;
    test_io.ConfigCaptureEnabled = false;
    test_io.ConfigLogToTTY = true;
    test_io.ConfigVerboseLevel = ImGuiTestVerboseLevel_Warning;
    test_io.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    test_io.CheckDrawDataIntegrity = true;

    RegisterUniGuiTestEngineCases(g_engine);
    bool filters_valid = true;
    for (const auto& filter : filters) {
        std::string engine_filter = filter;
        if (engine_filter == "*") {
            engine_filter = "tests";
        } else {
            if (engine_filter.starts_with('*')) engine_filter.erase(0, 1);
            if (engine_filter.ends_with('*')) engine_filter.pop_back();
            if (engine_filter.contains('*')) {
                std::cerr << "Test filters support wildcards only at their boundaries: " << filter << '\n';
                filters_valid = false;
                continue;
            }
        }
        int matching_tests = 0;
        for (ImGuiTest* test : g_engine->TestsAll) {
            if (test->Group == ImGuiTestGroup_Tests &&
                ImGuiTestEngine_PassFilter(test, engine_filter.c_str())) {
                ++matching_tests;
            }
        }
        if (matching_tests == 0) {
            std::cerr << "Test filter matched no tests: " << filter << '\n';
            filters_valid = false;
            continue;
        }
        ImGuiTestEngine_QueueTests(g_engine, ImGuiTestGroup_Tests, engine_filter.c_str());
    }
    ImGuiTestEngineResultSummary queued_summary;
    ImGuiTestEngine_GetResultSummary(g_engine, &queued_summary);
    const int expected_tests = queued_summary.CountInQueue;
    if (!filters_valid) {
        ImGuiTestEngine_DestroyContext(g_engine);
        g_engine = nullptr;
        return 1;
    }

    Uni::GUI::UiApp app;
    auto initialized = app.Initialize(MakeTestConfig());
    if (!initialized) {
        std::cerr << initialized.error().message << '\n';
        ImGuiTestEngine_DestroyContext(g_engine);
        return 1;
    }
    auto installed = InstallUniGuiTestElements(app);
    if (!installed) {
        std::cerr << installed.error().message << '\n';
        ImGuiTestEngine_Stop(g_engine);
        (void)app.Shutdown();
        ImGuiTestEngine_DestroyContext(g_engine);
        return 1;
    }

    bool run_succeeded = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{45};
    while (!ImGuiTestEngine_IsTestQueueEmpty(g_engine) &&
           std::chrono::steady_clock::now() < deadline) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            auto dispatched = app.DispatchEvent(event);
            if (!dispatched) {
                std::cerr << dispatched.error().message << '\n';
                run_succeeded = false;
                break;
            }
        }
        if (!run_succeeded) break;
        auto ticked = app.Tick();
        if (!ticked) {
            std::cerr << ticked.error().message << '\n';
            run_succeeded = false;
            break;
        }
    }
    if (!ImGuiTestEngine_IsTestQueueEmpty(g_engine)) {
        std::cerr << "Test Engine queue did not finish before the runner deadline\n";
        run_succeeded = false;
        (void)ImGuiTestEngine_TryAbortEngine(g_engine);
        for (int drain_frame = 0; drain_frame < 8; ++drain_frame) {
            auto drained = app.Tick();
            if (!drained) break;
        }
    }

    ImGuiContext* previous_context = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_engine->UiContextTarget);
    ImGuiTestEngine_Stop(g_engine);
    ImGui::SetCurrentContext(previous_context);
    ImGuiTestEngineResultSummary summary;
    ImGuiTestEngine_GetResultSummary(g_engine, &summary);
    auto shutdown = app.Shutdown();
    if (!shutdown) {
        std::cerr << shutdown.error().message << '\n';
    }
    ImGuiTestEngine_DestroyContext(g_engine);
    g_engine = nullptr;

    return expected_tests > 0 &&
           run_succeeded &&
           summary.CountTested == expected_tests &&
           summary.CountSuccess == expected_tests &&
           summary.CountInQueue == 0 &&
           shutdown.has_value()
        ? 0
        : 1;
}
