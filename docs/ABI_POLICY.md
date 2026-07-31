# UniGUI ABI Policy

## Current Stability

Release `1.0.0` establishes stable ABI major `1`. Within the supported compiler, standard-library, runtime, architecture, and build-mode family, later `1.x` releases preserve the public binary interfaces documented here. An incompatible public ABI change requires a new major release.

## Binary Boundary

The shared library exports `UiApp`, `UiTexture`, `UiDispatcher`, `UiCommandTicket`, and the classes in `Uni::GUI::Nodes` through the generated `uni/gui/export.h` declarations. Stateful node classes use PIMPL so graph storage, registries, command history, and editor interaction state are absent from installed class layouts. `Command` is an exported polymorphic extension point; its vtable and RTTI are part of the binary boundary. `UiApp` and `UiTexture` likewise use PIMPL so their private SDL, renderer, ImGui context, registry, asset, layout, and texture lifecycle state is absent from installed class layouts. Dispatcher and ticket objects contain only a shared pointer to private state.

Concrete renderer and window-system implementations are private. Their headers are not installed and their ELF symbols use hidden visibility.

## Toolchain Compatibility

Public interfaces use C++23 standard-library types including `std::expected`, `std::function`, `std::span`, `std::variant`, `std::any`, `std::shared_ptr`, `std::unique_ptr`, `std::string`, and `std::string_view`. Shared-library consumers must use a compatible compiler, standard library, runtime library, architecture, and build mode.

On Windows, applications and UniGUI must use ABI-compatible MSVC runtime settings. Static targets propagate `UNI_GUI_STATIC_DEFINE` to disable DLL import/export annotations.

## Dear ImGui And ImPlot

`UiTexture::GetRef()` exposes the bundled Dear ImGui `ImTextureRef` type. Consumers must link the exported `UniGUI::imgui` and `UniGUI::implot` dependency closure selected by `UniGUI::UniGUI`; mixing unrelated Dear ImGui or ImPlot binaries in the same context is unsupported.

Dear ImGui Test Engine is test-only. Its definitions and symbols are not linked into or installed with the production package.

## Versioning

Incompatible ABI changes require a major version increment. Minor and patch releases preserve ABI within the same compiler/runtime family and may add backward-compatible APIs.

The UniGUI, bundled Dear ImGui, and bundled ImPlot shared targets use the package major as their loader identity; for release `1.0.0`, SOVERSION is `1`. On ELF platforms, the core shared library ELF symbol version node is `UNIGUI_1` and remains unchanged throughout compatible `1.x` releases. Windows DLL and import-library names carry the same ABI major. The CMake package version uses `SameMajorVersion`: a newer `1.x` package can satisfy a `1.0` request, while a `2.x` package cannot.

Linux builds always test built and installed SONAMEs and the core ELF version node when shared libraries are enabled. The ABI lane captures ABIXML and exported-symbol manifests for all three DSOs. Once a release baseline exists, `UNIGUI_ENABLE_ABI_BASELINE_CHECK=ON` and `UNIGUI_ABI_BASELINE_DIR` make removal or incompatible mutation of that released surface a blocking test. Release artifacts contain provenance-attested baselines; no synthetic or unreviewed baseline is accepted as release evidence.

Nodes JSON persistence uses vendored nlohmann/json as a private header-only implementation detail. Its types do not cross the public API or ABI boundary, and package consumers do not need to provide it.

## Thread Boundary

`UiDispatcher` is the only public type designed for calls from worker threads. All other UniGUI operations are main-thread-only. This is a behavioral contract rather than an ABI compatibility guarantee.

Public `RegistrySnapshot` values are immutable owning views of one `RegistryCatalog` generation. `RegistryUpdate` applies atomic persistent-map changes, preserving generation and record identities for semantic no-ops. `ConversionRecipe` is a small owning handle to one exact immutable recipe, while `ConversionRegistrationToken` identifies a live registration in its catalog. Callback invocation leases and selective deferred-operation dependency records are private implementation details and are not application ABI. Their internal ownership keeps an active callback generation alive if its catalog is moved or destroyed.

## SDL Dependency

SDL 3.4 or newer remains an external CMake package dependency. Exported targets retain the concrete SDL shared or static target selected by the producer package so their binary dependency model cannot change silently at consumption time. Consumers must provide the matching SDL3 CMake target family; SDL 3.2 compatibility is not provided.

## Supported Consumption

The supported binary consumption path is:

```cmake
find_package(UniGUI 1.0 CONFIG REQUIRED)
target_link_libraries(application PRIVATE UniGUI::UniGUI)
```

Callback applications link `UniGUI::Main`. Direct linking to package implementation libraries or private symbols is unsupported.
