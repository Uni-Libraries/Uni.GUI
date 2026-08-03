# Uni.GUI

Uni.GUI is a C++23 application shell for Dear ImGui and ImPlot.

The current version is `1.0.0`, released on 2026-08-02. Release details are in the [changelog](CHANGELOG.md), and compatibility guarantees are in the [ABI policy](docs/ABI_POLICY.md).

## Requirements

- CMake 3.26 or newer
- C++23 compiler and standard library with `std::expected`
- SDL 3.4 or newer with a CMake package
- Initialized Dear ImGui and ImPlot submodules

The CI matrix covers Linux with GCC and Clang, Windows with MSVC, and macOS on Intel and ARM64 with AppleClang. Debug, Release, shared, static, sanitizers, Dear ImGui Test Engine, installation, and an external package consumer are checked.
Linux CI and release jobs build SDL 3.4.12 from a pinned commit; Windows and macOS use the pinned vcpkg SDL 3.4.12 port.

## Build

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Options:

- `BUILD_SHARED_LIBS=ON|OFF`: shared or static UniGUI, ImGui, and ImPlot.
- `UNIGUI_BUILD_DEMO=ON|OFF`: build the demo.
- `UNIGUI_BUILD_TESTS=ON|OFF`: build unit and lifecycle tests.
- `UNIGUI_BUILD_BENCHMARKS=ON|OFF`: build and register the Nodes performance regression benchmarks.
- `UNIGUI_ENABLE_RUNTIME_TESTS=ON|OFF`: enable tests requiring a graphical display.
- `UNIGUI_BUILD_TEST_ENGINE_TESTS=ON|OFF`: build the pinned Dear ImGui Test Engine suite; requires runtime tests.
- `UNIGUI_INSTALL=ON|OFF`: generate install and CMake package rules.
- `UNIGUI_ENABLE_ABI_BASELINE_CHECK=ON|OFF`: compare all three Linux DSOs against released ABIXML and symbol manifests in `UNIGUI_ABI_BASELINE_DIR`.

Runtime tests can be executed in a headless Linux environment:

```sh
xvfb-run -a ctest --test-dir build --output-on-failure
```

Release archives can be generated with CPack:

```sh
cpack --config build/CPackConfig.cmake -C Release -G TGZ
cpack --config build/CPackSourceConfig.cmake -G TGZ
```

## Installed Package

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/prefix
cmake --build build --parallel
cmake --install build
```

External project:

```cmake
find_package(UniGUI 1.0 CONFIG REQUIRED)

target_link_libraries(my_core PRIVATE UniGUI::UniGUI)
target_link_libraries(my_callback_application PRIVATE UniGUI::Main)
```

Canonical package targets:

- `UniGUI::UniGUI`
- `UniGUI::Main`
- `UniGUI::imgui`
- `UniGUI::implot`

Public headers use the `uni/gui` prefix:

```cpp
#include <uni/gui/app.h>
#include <uni/gui/callbacks.h>
#include <uni/gui/element.h>
```

SDL3 remains an external dependency resolved by `find_dependency(SDL3 3.4 CONFIG)`. Nodes JSON persistence uses the vendored, header-only nlohmann/json 3.12.0 library as a private implementation detail, so it does not become a package consumer dependency.

## Nodes

`Uni::GUI::Nodes` is a retained semantic graph model and Dear ImGui node-editor framework. It provides typed nodes and pins, mutation-authorized transactions and undo/redo, deferred authorization, deterministic JSON persistence, hierarchy and reusable graph assets, immutable semantic snapshots, custom node UI, animated link flow, debug overlays, and pluggable routing. It deliberately does not provide graph evaluation, scheduling, or an execution runtime; applications keep those concerns in their own runtime layer.

The umbrella header and package target are:

```cpp
#include <uni/gui/nodes/nodes.h>
```

```cmake
target_link_libraries(my_editor PRIVATE UniGUI::UniGUI)
```

Nodes documentation:

- [Overview and documentation map](docs/nodes/README.md)
- [Getting started](docs/nodes/getting_started.md)
- [Semantic model](docs/nodes/semantic_model.md)
- [Custom nodes, UI, and converters](docs/nodes/custom_nodes.md)
- [Commands, transactions, and undo](docs/nodes/commands_and_transactions.md)
- [Persistence and migrations](docs/nodes/persistence.md)
- [Display scaling](docs/display_scaling.md)

Mutable Nodes objects and callbacks follow the UniGUI main-thread contract. Capture `GraphDocumentSnapshot` on the UI thread for const worker reads, and marshal mutations through `UiDispatcher`; see [threading and callback contracts](docs/nodes/threading_and_callbacks.md).

## Configuration

`UiAppConfig` is the complete startup configuration:

```cpp
Uni::GUI::UiAppConfig config;
config.title = "Engineering tool";
config.initial_width = 1440;
config.initial_height = 900;
config.renderer = Uni::GUI::UiRendererPreference::Automatic;
config.viewports = Uni::GUI::UiFeaturePolicy::IfSupported;
config.vsync = Uni::GUI::UiVsyncMode::Enabled;
config.font.size = 16.0f;
config.font.glyph_range = Uni::GUI::UiGlyphRange::Default;
config.scaling.high_pixel_density = true;
config.scaling.mode = Uni::GUI::UiScaleMode::Automatic;
config.scaling.user_scale = 1.0f;
config.persistence.enabled = true;
config.persistence.path = "engineering-tool.settings";
config.persistence.save_debounce = std::chrono::milliseconds{750};
config.frame_policy.active = {Uni::GUI::UiLoopMode::Continuous, 0.0};
config.frame_policy.idle = {Uni::GUI::UiLoopMode::RateLimited, 10.0};
config.frame_policy.minimized = {Uni::GUI::UiLoopMode::WaitForEvent, 0.0};

Uni::GUI::UiApp app;
if (auto initialized = app.Initialize(std::move(config)); !initialized) {
    Log(initialized.error().message);
}
```

Renderer policies:

- `Automatic`: try SDL_GPU, then SDL_Renderer.
- `SdlGpu`: require SDL_GPU without fallback.
- `SdlRenderer`: require SDL_Renderer.

The SDL_GPU path uses SDL 3.4 device properties to disable optional features unused by the bundled ImGui backend, allow D3D12 Tier 1 resource binding, and emit backend/device/driver diagnostics during initialization. Validation and verbose GPU logging are enabled in non-`NDEBUG` builds.

Changing VSync at runtime is supported for the main SDL_GPU window. When multi-viewport is enabled, changing to a different mode returns `UiErrorCode::InvalidState` because the upstream backend stores one present mode for secondary swapchains; requesting the already active mode remains a no-op success.

Viewport policies:

- `Disabled`: never enable platform viewports.
- `IfSupported`: enable only when both backends advertise support.
- `Required`: fail initialization if support is unavailable.

An empty font path selects the embedded Roboto Medium font. A non-empty path is loaded through `AddFontFromFileTTF()`.

Window dimensions and font size use reference UI units. High-density framebuffer allocation, automatic/fixed/manual UI scaling, and the runtime display metrics API are documented in [display scaling](docs/display_scaling.md).

## Errors And Lifecycle

Operations that can fail return `std::expected<T, UiError>`. `UiError` contains a typed `UiErrorCode` and a diagnostic message, including SDL details when applicable.

```cpp
auto tick = app.Tick();
if (!tick) {
    Report(tick.error().code, tick.error().message);
} else if (tick->exit_requested) {
    BeginShutdown();
}
```

Lifecycle:

```text
Empty -> Initializing -> Ready -> ShuttingDown -> Empty
                       -> Failed -> ShuttingDown -> Empty
```

- Configuration validation happens before entering `Initializing`.
- Partial initialization failures release all acquired resources and enter `Failed`.
- `Shutdown()` is public and idempotent.
- Frame, event, registry, texture, and VSync operations require `Ready`.
- ImGui and ImPlot contexts are private to `UiApp`; previously current contexts are restored after every operation.
- Renderer errors propagate to the SDL callback loop instead of being reduced to log-only failures.

## Callback Application

An executable linked to `UniGUI::Main` provides three callbacks:

```cpp
#include <uni/gui/callbacks.h>

Uni::GUI::UiAppConfig uni_gui_app_configure(int argc, char** argv) {
    Uni::GUI::UiAppConfig config;
    config.title = "My application";
    return config;
}

Uni::GUI::UiResult<void>
uni_gui_app_initialize(Uni::GUI::UiApp& app) {
    auto added = app.AddElement(std::make_unique<MyWindow>());
    if (!added) {
        return std::unexpected(std::move(added.error()));
    }
    return {};
}

Uni::GUI::UiResult<void> uni_gui_app_finalize(Uni::GUI::UiApp&) {
    return {};
}
```

The callback runner catches application exceptions before they cross the SDL C callback boundary.

## Element Ownership

`UiApp` exclusively owns registered elements:

```cpp
auto id = app.AddElement(std::make_unique<MyWindow>());
if (!id) {
    Report(id.error().message);
}

auto removed = app.RemoveElement(*id);
```

`UiElement::Update()` returns an update request. Set `keep_alive` to `false` to remove the element after the current iteration, and use `frame_demand` when the element needs animation:

```cpp
Uni::GUI::UiResult<Uni::GUI::UiElementUpdate>
MyWindow::Update(Uni::GUI::UiState&) {
    ImGui::Begin("Window");
    ImGui::TextUnformatted("Hello");
    ImGui::End();
    return Uni::GUI::UiElementUpdate{
        .keep_alive = m_keep_open,
        .frame_demand = Uni::GUI::UiFrameDemand::None,
    };
}
```

Adding or removing elements from inside `Update()` is safe. Mutations are committed after iteration, and newly added elements begin updating on the next frame. `UiElementId` remains stable until removal.

## Textures

`UiApp` owns texture descriptors and GPU lifetime. `UiTexture` is a move-only PIMPL handle.

```cpp
auto created = state.app.CreateTexture(256, 256);
if (!created) {
    Report(created.error().message);
    return false;
}

Uni::GUI::UiTexture texture = std::move(*created);
texture.Clear(IM_COL32(30, 80, 160, 255));
ImGui::Image(texture.GetRef(), ImVec2(256.0f, 256.0f));
```

- `Pixels()` returns the complete RGBA32 CPU buffer.
- `PixelsAt(x, y)` returns `nullptr` for invalid coordinates.
- `Update()` queues a complete upload.
- `UpdateRect()` clips partial updates without signed overflow.
- `Destroy()` invalidates the handle immediately and defers GPU destruction safely.
- A handle may outlive its application and then behaves as invalid.

Do not retain `ImTextureRef` after destroying its `UiTexture`.

## Dispatcher And Scheduling

`UiDispatcher` is the only thread-safe UniGUI entry point. A worker can post a move-only command, request a frame, or request exit. Commands execute on the UI thread before `ImGui::NewFrame()` and commands posted by a command are deferred until the next tick.

```cpp
Uni::GUI::UiDispatcher dispatcher = app.Dispatcher();

std::thread worker([dispatcher, data = LoadData()]() mutable {
    auto posted = dispatcher.Post(
        [data = std::move(data)](Uni::GUI::UiApp& ui) mutable -> Uni::GUI::UiResult<void> {
            ApplyData(ui, std::move(data));
            return {};
        });
    if (posted) {
        auto completed = posted->Wait();
        ReportIfFailed(completed);
    }
});
```

`UiCommandTicket::Wait()` rejects a pending wait on the UI thread with `UiErrorCode::WouldDeadlock`. Outstanding commands are cancelled when the application shuts down. `UiTickResult::next_iteration` tells callback runners whether to continue immediately, rate-limit, or wait for an event.

Dispatcher requests are coalesced into one reserved SDL event so SDL's `waitevent` callback mode advances to `SDL_AppIterate()` without polling. Numeric callback rates preserve fractional values supported by SDL 3.4, such as `59.94` Hz.

## Event Hooks

Typed hooks run before and after the ImGui SDL backend. They may pass, consume, or convert an event into an exit request.

```cpp
Uni::GUI::UiEventHooks hooks;
hooks.before_imgui = [](Uni::GUI::UiEventContext& event) -> Uni::GUI::UiResult<Uni::GUI::UiEventAction> {
    return IsApplicationShortcut(event.event)
        ? Uni::GUI::UiEventAction::Consume
        : Uni::GUI::UiEventAction::Pass;
};
auto installed = app.SetEventHooks(std::move(hooks));
```

## Docking And Persistence

Docking layouts are declarative split graphs. Each final leaf receives zero or more named ImGui windows. Layout snapshots are persisted independently, so switching layouts restores each layout's last state.

```cpp
Uni::GUI::UiDockLayout layout;
layout.id = "editing";
layout.splits.push_back({"root", Uni::GUI::UiDockSide::Left, 0.3f, "tools", "main"});
layout.placements.push_back({"Inspector", "tools"});
layout.placements.push_back({"Viewport", "main"});

auto defined = app.DefineDockLayout(std::move(layout));
auto activated = app.ActivateDockLayout("editing", Uni::GUI::UiDockApplyMode::RestoreOrBuild);
```

The settings file uses a bounded, versioned binary envelope around Dear ImGui INI snapshots. Writes use a temporary file followed by atomic replacement. `SaveSettingsNow()` and `ReloadSettings()` are safe-point operations on the UI thread.

## Assets And Fonts

Assets are immutable file or shared-byte sources with stable IDs and monotonically increasing generations. UniGUI copies shared-byte inputs into manager-owned storage, so mutation of a caller's original buffer cannot invalidate an asset. Replacing an asset preserves its ID. Runtime fonts retain the asset bytes required by the ImGui atlas.

```cpp
Uni::GUI::UiAssetSpec asset;
asset.key = "editor-font";
asset.source = std::string{"fonts/Editor.ttf"};
auto stored = app.UpsertAsset(std::move(asset));

if (stored) {
    auto font = app.CreateFont({stored->id, 15.0f, Uni::GUI::UiGlyphRange::Cyrillic, true});
}
```

`UiFontId` remains stable when its source asset is replaced, but the old `ImFont*` does not. Resolve the ID again with `GetFont()` after every successful source replacement, and do not retain the pointer after `RemoveFont()`.

## Threading

Except for `UiDispatcher` operations and `UiCommandTicket` observation/waiting, all UniGUI operations are main-thread-only. This includes `UiApp`, `UiElement`, `UiTexture`, every `Uni::GUI::Nodes` object, Dear ImGui, and ImPlot. Use `UiDispatcher` from worker threads; checked cross-thread calls return `UiErrorCode::WrongThread` where applicable, while Nodes relies on the caller to honor the contract. An initialized `UiApp` must also be destroyed on its owning UI thread; violating this lifetime contract terminates rather than attempting unsafe cross-thread SDL/ImGui cleanup.

## ABI Policy

`UiApp`, `UiTexture`, and stateful Nodes classes use PIMPL. The shared library exports the public classes and functions marked by `UNI_GUI_EXPORT`, including the `Uni::GUI::Nodes` subsystem; internal renderer, SDL windowing, texture-store, and implementation types remain hidden and are not installed.

Release `1.0.0` starts the stable ABI major `1`. Compatible `1.x` releases keep SOVERSION `1`, while incompatible ABI changes require a new major release. Shared-library consumers must still use the same C++ ABI family and the bundled ImGui/ImPlot package targets. The complete contract is in the [ABI policy](docs/ABI_POLICY.md).
