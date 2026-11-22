#pragma once

#include <uni/gui/error.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

struct ImFont;

namespace Uni::GUI {

/// Requested glyph range for the font atlas.
enum class UiGlyphRange {
    Default,
    Cyrillic,
};

/// App-local asset handle. Replacing a registered key preserves this ID.
using UiAssetId = std::uint64_t;
/// App-local font handle. Use UiApp::GetFont() to resolve its current atlas pointer.
using UiFontId = std::uint64_t;
inline constexpr UiAssetId InvalidUiAssetId = 0;
inline constexpr UiFontId InvalidUiFontId = 0;

/// Shared input bytes; the manager copies them on insertion or replacement.
using UiAssetBytes = std::shared_ptr<const std::vector<std::byte>>;
/// File path or non-empty in-memory data, read at upsert time.
using UiAssetSource = std::variant<std::string, UiAssetBytes>;

/// Register source data under a non-empty key, or replace the data for that key.
struct UiAssetSpec final {
    std::string key;
    UiAssetSource source;
};

/// Value snapshot of registered asset metadata; generation starts at 1 and advances on replacement.
struct UiAssetInfo final {
    UiAssetId id{InvalidUiAssetId};
    std::string key;
    std::uint64_t generation{};
    std::size_t byte_size{};
};

/// Atlas font backed by a registered asset. The asset cannot be removed while the font uses it.
struct UiFontSpec final {
    UiAssetId asset{InvalidUiAssetId};
    float size_pixels{14.0f};
    UiGlyphRange glyph_range{UiGlyphRange::Cyrillic};
    bool make_default{};
};

/// Positive resource limits. The byte limit is per asset, and the font limit includes the fallback font.
struct UiAssetLimits final {
    std::size_t max_asset_bytes{64U * 1024U * 1024U};
    std::size_t max_assets{1024};
    std::size_t max_fonts{64};
};

} // namespace Uni::GUI
