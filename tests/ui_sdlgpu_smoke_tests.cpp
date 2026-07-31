#include <uni/gui/app.h>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>

int main() {
    using namespace Uni::GUI;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL video unavailable: " << SDL_GetError() << '\n';
        return 77;
    }
    constexpr SDL_GPUShaderFormat shader_formats =
        SDL_GPU_SHADERFORMAT_SPIRV |
        SDL_GPU_SHADERFORMAT_DXIL |
        SDL_GPU_SHADERFORMAT_DXBC |
        SDL_GPU_SHADERFORMAT_MSL |
        SDL_GPU_SHADERFORMAT_METALLIB;
    const bool gpu_supported = SDL_GPUSupportsShaderFormats(shader_formats, nullptr);
    SDL_Quit();
    if (!gpu_supported) {
        std::cerr << "No compatible SDL_GPU backend is available\n";
        return 77;
    }

    const auto make_config = [](const UiFeaturePolicy viewports) {
        UiAppConfig config;
        config.title = "UniGUI SDL 3.4 GPU smoke test";
        config.initial_width = 320;
        config.initial_height = 240;
        config.renderer = UiRendererPreference::SdlGpu;
        config.viewports = viewports;
        config.vsync = UiVsyncMode::Enabled;
        config.persistence.enabled = false;
        config.docking.enabled = false;
        return config;
    };

    UiApp app;
    if (auto initialized = app.Initialize(make_config(UiFeaturePolicy::Disabled)); !initialized) {
        std::cerr << initialized.error().message << '\n';
        (void)app.Shutdown();
        return EXIT_FAILURE;
    }
    if (auto ticked = app.Tick(); !ticked) {
        std::cerr << ticked.error().message << '\n';
        (void)app.Shutdown();
        return EXIT_FAILURE;
    }
    if (auto changed_vsync = app.SetVsync(UiVsyncMode::Disabled); !changed_vsync) {
        std::cerr << changed_vsync.error().message << '\n';
        (void)app.Shutdown();
        return EXIT_FAILURE;
    }
    if (auto shutdown = app.Shutdown(); !shutdown) {
        std::cerr << shutdown.error().message << '\n';
        return EXIT_FAILURE;
    }

    UiApp viewport_app;
    if (auto initialized = viewport_app.Initialize(make_config(UiFeaturePolicy::Required)); !initialized) {
        const bool unsupported = initialized.error().code == UiErrorCode::BackendInitialization;
        (void)viewport_app.Shutdown();
        if (unsupported) {
            std::cout << "SDL platform backend does not support multi-viewport; VSync guard skipped\n";
            return EXIT_SUCCESS;
        }
        std::cerr << initialized.error().message << '\n';
        return EXIT_FAILURE;
    }
    if (auto ticked = viewport_app.Tick(); !ticked) {
        std::cerr << ticked.error().message << '\n';
        (void)viewport_app.Shutdown();
        return EXIT_FAILURE;
    }
    if (auto unchanged_vsync = viewport_app.SetVsync(UiVsyncMode::Enabled); !unchanged_vsync) {
        std::cerr << unchanged_vsync.error().message << '\n';
        (void)viewport_app.Shutdown();
        return EXIT_FAILURE;
    }
    const auto changed_viewport_vsync = viewport_app.SetVsync(UiVsyncMode::Disabled);
    if (changed_viewport_vsync || changed_viewport_vsync.error().code != UiErrorCode::InvalidState) {
        std::cerr << "Runtime VSync change must be rejected while SDL_GPU multi-viewport is enabled\n";
        (void)viewport_app.Shutdown();
        return EXIT_FAILURE;
    }
    if (auto shutdown = viewport_app.Shutdown(); !shutdown) {
        std::cerr << shutdown.error().message << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
