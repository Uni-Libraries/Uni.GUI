# Display Scaling

UniGUI keeps UI scaling separate from framebuffer resolution. `UiDisplayMetrics` exposes the values needed by application-owned drawing:

- `window_size` uses SDL and Dear ImGui window coordinates.
- `framebuffer_size` uses physical render-target pixels.
- `display_scale` is SDL's combined scale from reference content to framebuffer pixels.
- `pixel_density` converts window coordinates to framebuffer pixels.
- `system_ui_scale` is `display_scale / pixel_density` and converts reference UI units to window coordinates.
- `effective_ui_scale` also includes the configured fixed scale or user multiplier.
- `applied_ui_scale` is the scale UniGUI applied to Dear ImGui. It remains `1` in manual mode.

This distinction is required across platforms. A high-density macOS window can have `display_scale == 2` and `pixel_density == 2`, so its UI scale in window coordinates is `1`. A Windows window at 200% can have `display_scale == 2` and `pixel_density == 1`, so its UI scale is `2`.

## Configuration

`UiScalingConfig::high_pixel_density` requests a native-density framebuffer independently of UI sizing. `UiScalingConfig::mode` controls how the UI scale is selected:

- `Automatic` uses `system_ui_scale * user_scale`.
- `Fixed` uses `fixed_scale * user_scale`, which is useful for deterministic tests and fixed-size installations.
- `Manual` reports the automatic effective scale but leaves Dear ImGui style and font scaling to the application.

`initial_width` and `initial_height` are reference UI units. UniGUI converts them to the initial display's window coordinates using the selected scale policy. Invalid or overflowing converted dimensions fail window creation.

```cpp
Uni::GUI::UiAppConfig config;
config.initial_width = 1280;
config.initial_height = 720;
config.scaling.high_pixel_density = true;
config.scaling.mode = Uni::GUI::UiScaleMode::Automatic;
config.scaling.user_scale = 1.0f;
```

Use `UiApp::SetUserScale()` to change the accessibility/application multiplier at a frame-safe point. `UiApp::DisplayMetrics()` returns the current snapshot; `revision` changes when its semantic values change.

## Styles

Automatic and fixed modes rebuild the scaled Dear ImGui style from an unscaled reference style, avoiding cumulative rounding when a window moves between displays. Set an application theme with `UiApp::SetReferenceStyle()` after initialization instead of mutating scaled size fields directly:

```cpp
ImGuiStyle style;
ImGui::StyleColorsLight(&style);
style.WindowRounding = 5.0f;
if (auto applied = app.SetReferenceStyle(style); !applied) {
    Report(applied.error());
}
```

Colors can be part of the reference style. UniGUI keeps `FontSizeBase` synchronized with `UiFontConfig::size`.

Dear ImGui style spacing is global. With platform viewports on monitors using different scales, fonts and framebuffer density can follow each viewport, but reference style spacing follows the main-window scale. Use manual mode when an application needs to own a different multi-monitor policy.

## Custom Drawing

Application geometry expressed in reference UI units should be multiplied by `effective_ui_scale` when automatic or fixed mode is used. Renderer backends already consume framebuffer density through Dear ImGui draw data; do not multiply UI geometry by `pixel_density` a second time.

The Nodes editor normalizes font metrics back to graph units and composes UI scale with editor zoom. Persisted graph positions and sizes therefore do not change when the window moves to another display.
