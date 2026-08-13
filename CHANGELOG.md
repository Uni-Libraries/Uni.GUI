# Changelog

All notable changes to UniGUI are documented in this file.

## [Unreleased]

## [4.0.0] - 2026-08-13

- Replaces flat and project-specific node property representations with one recursive `PropertyValue` API for nulls, scalars, vectors, typed references, arrays, objects, and opaque extensions.
- Adds centralized semantic property validation, resource limits, and recursive copy accounting.
- Replaces the version 4 property wire representation with exact version 5 recursive typed envelopes.
- Adds one property-path contract across commands, UI editing, policy payloads, revision impacts, and configurable pin-schema dependencies.

## [3.0.0] - 2026-08-07

- Replaces fixed descriptor pin arrays with one `NodePinSchema` API for fixed and property-configurable schemas.
- Adds explicit schema property dependencies, atomic instantiation overrides, cached default schemas, and schema-aware transaction mutation.
- Reconciles descriptor-owned pins and connections with exact undo/redo, configurable invalid-connection handling, and descriptor-version checks.

## [2.0.0] - 2026-08-04

- Replaces the DPI booleans with automatic, fixed, and manual scaling policies, public display metrics, runtime user scaling, and reference-style management.
- Replaces project-specific node playback/runtime header state with owned text lines, application-registered glyphs, generic badges, and deferred header actions.
- Makes node-editor graph, input, overlay, minimap, and custom-body coordinates compose UI scale with editor zoom while preserving display-independent persisted geometry.
- Adds immutable custom type-compatibility policies with atomic registry updates, generalized converter discovery, and deferred dependency tracking.
- Makes editor undo respect the active graph policy and strengthens callback, collapse, and lifecycle behavior.

## [1.0.0] - 2026-08-02

- Initial release

[Unreleased]: https://github.com/Uni-Libraries/Uni.GUI/compare/v4.0.0...HEAD
[4.0.0]: https://github.com/Uni-Libraries/Uni.GUI/releases/tag/v4.0.0
[3.0.0]: https://github.com/Uni-Libraries/Uni.GUI/releases/tag/v3.0.0
[2.0.0]: https://github.com/Uni-Libraries/Uni.GUI/releases/tag/v2.0.0
[1.0.0]: https://github.com/Uni-Libraries/Uni.GUI/releases/tag/v1.0.0
