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

enum class UiGlyphRange {
    Default,
    Cyrillic,
};

using UiAssetId = std::uint64_t;
using UiFontId = std::uint64_t;
inline constexpr UiAssetId InvalidUiAssetId = 0;
inline constexpr UiFontId InvalidUiFontId = 0;

using UiAssetBytes = std::shared_ptr<const std::vector<std::byte>>;
using UiAssetSource = std::variant<std::string, UiAssetBytes>;

struct UiAssetSpec final {
    std::string key;
    UiAssetSource source;
};

struct UiAssetInfo final {
    UiAssetId id{InvalidUiAssetId};
    std::string key;
    std::uint64_t generation{};
    std::size_t byte_size{};
};

struct UiFontSpec final {
    UiAssetId asset{InvalidUiAssetId};
    float size_pixels{14.0f};
    UiGlyphRange glyph_range{UiGlyphRange::Cyrillic};
    bool make_default{};
};

struct UiAssetLimits final {
    std::size_t max_asset_bytes{64U * 1024U * 1024U};
    std::size_t max_assets{1024};
    std::size_t max_fonts{64};
};

} // namespace Uni::GUI
