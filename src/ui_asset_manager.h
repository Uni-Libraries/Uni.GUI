#pragma once

#include <uni/gui/asset.h>
#include <uni/gui/error.h>

#include <imgui.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Uni::GUI::Detail {

class UiAssetManager final {
public:
    void Configure(UiAssetLimits limits, ImFont* fallback_font);

    [[nodiscard]] UiResult<UiAssetInfo> Upsert(UiAssetSpec spec);
    [[nodiscard]] UiResult<UiAssetInfo> Find(std::string_view key) const;
    [[nodiscard]] UiResult<bool> Remove(UiAssetId id);

    [[nodiscard]] UiResult<UiFontId> CreateFont(const UiFontSpec& spec);
    [[nodiscard]] UiResult<void> SetDefaultFont(UiFontId id);
    [[nodiscard]] UiResult<bool> RemoveFont(UiFontId id);
    [[nodiscard]] UiResult<ImFont*> GetFont(UiFontId id) const;

    void Reset() noexcept;

private:
    struct AssetRecord final {
        UiAssetInfo info;
        UiAssetBytes bytes;
    };

    struct FontRecord final {
        UiFontId id{InvalidUiFontId};
        UiFontSpec spec;
        ImFont* font{};
        bool removable{true};
    };

    [[nodiscard]] UiResult<UiAssetBytes> ReadSource(const UiAssetSource& source) const;
    [[nodiscard]] UiResult<ImFont*> AddFont(const UiFontSpec& spec, const UiAssetBytes& bytes) const;

    UiAssetLimits m_limits;
    std::unordered_map<UiAssetId, AssetRecord> m_assets;
    std::unordered_map<std::string, UiAssetId> m_asset_keys;
    std::unordered_map<UiFontId, FontRecord> m_fonts;
    UiAssetId m_next_asset_id{1};
    UiFontId m_next_font_id{2};
    UiFontId m_default_font_id{1};
};

} // namespace Uni::GUI::Detail
