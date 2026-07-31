#include "ui_asset_manager.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace Uni::GUI::Detail {

void UiAssetManager::Configure(UiAssetLimits limits, ImFont* fallback_font) {
    Reset();
    m_limits = limits;
    m_fonts.emplace(1, FontRecord{1, {}, fallback_font, false});
    m_default_font_id = 1;
}

UiResult<UiAssetInfo> UiAssetManager::Upsert(UiAssetSpec spec) {
    if (spec.key.empty()) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Asset key must not be empty"});
    }
    auto loaded = ReadSource(spec.source);
    if (!loaded) {
        return std::unexpected(std::move(loaded.error()));
    }

    const auto key_iterator = m_asset_keys.find(spec.key);
    if (key_iterator == m_asset_keys.end()) {
        if (m_assets.size() >= m_limits.max_assets) {
            return std::unexpected(UiError{UiErrorCode::AssetTooLarge, "Asset count limit is reached"});
        }
        const UiAssetId id = m_next_asset_id++;
        UiAssetInfo info{id, std::move(spec.key), 1, (*loaded)->size()};
        m_asset_keys.emplace(info.key, id);
        m_assets.emplace(id, AssetRecord{info, std::move(*loaded)});
        return info;
    }

    AssetRecord& record = m_assets.at(key_iterator->second);
    std::vector<std::pair<UiFontId, ImFont*>> replacements;
    for (const auto& [font_id, font] : m_fonts) {
        if (font.spec.asset == record.info.id) {
            auto replacement = AddFont(font.spec, *loaded);
            if (!replacement) {
                for (const auto& [unused_id, created] : replacements) {
                    ImGui::GetIO().Fonts->RemoveFont(created);
                }
                return std::unexpected(std::move(replacement.error()));
            }
            replacements.emplace_back(font_id, *replacement);
        }
    }

    for (const auto& [font_id, replacement] : replacements) {
        FontRecord& font = m_fonts.at(font_id);
        ImFont* old_font = font.font;
        font.font = replacement;
        if (font_id == m_default_font_id) {
            ImGui::GetIO().FontDefault = replacement;
        }
        ImGui::GetIO().Fonts->RemoveFont(old_font);
    }

    record.bytes = std::move(*loaded);
    ++record.info.generation;
    record.info.byte_size = record.bytes->size();
    return record.info;
}

UiResult<UiAssetInfo> UiAssetManager::Find(const std::string_view key) const {
    const auto key_iterator = m_asset_keys.find(std::string{key});
    if (key_iterator == m_asset_keys.end()) {
        return std::unexpected(UiError{UiErrorCode::AssetNotFound, "Asset key is not registered"});
    }
    return m_assets.at(key_iterator->second).info;
}

UiResult<bool> UiAssetManager::Remove(const UiAssetId id) {
    const auto iterator = m_assets.find(id);
    if (iterator == m_assets.end()) {
        return false;
    }
    for (const auto& [font_id, font] : m_fonts) {
        if (font.spec.asset == id) {
            return std::unexpected(UiError{UiErrorCode::InvalidState, "Asset is still used by a runtime font"});
        }
    }
    m_asset_keys.erase(iterator->second.info.key);
    m_assets.erase(iterator);
    return true;
}

UiResult<UiFontId> UiAssetManager::CreateFont(const UiFontSpec& spec) {
    if (m_fonts.size() >= m_limits.max_fonts) {
        return std::unexpected(UiError{UiErrorCode::AssetTooLarge, "Font count limit is reached"});
    }
    const auto asset = m_assets.find(spec.asset);
    if (asset == m_assets.end()) {
        return std::unexpected(UiError{UiErrorCode::AssetNotFound, "Font asset is not registered"});
    }
    auto font = AddFont(spec, asset->second.bytes);
    if (!font) {
        return std::unexpected(std::move(font.error()));
    }

    const UiFontId id = m_next_font_id++;
    m_fonts.emplace(id, FontRecord{id, spec, *font, true});
    if (spec.make_default) {
        m_default_font_id = id;
        ImGui::GetIO().FontDefault = *font;
    }
    return id;
}

UiResult<void> UiAssetManager::SetDefaultFont(const UiFontId id) {
    const auto iterator = m_fonts.find(id);
    if (iterator == m_fonts.end()) {
        return std::unexpected(UiError{UiErrorCode::FontNotFound, "Font ID is not registered"});
    }
    m_default_font_id = id;
    ImGui::GetIO().FontDefault = iterator->second.font;
    return {};
}

UiResult<bool> UiAssetManager::RemoveFont(const UiFontId id) {
    const auto iterator = m_fonts.find(id);
    if (iterator == m_fonts.end()) {
        return false;
    }
    if (!iterator->second.removable) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Fallback font cannot be removed"});
    }
    if (m_default_font_id == id) {
        m_default_font_id = 1;
        ImGui::GetIO().FontDefault = m_fonts.at(1).font;
    }
    ImGui::GetIO().Fonts->RemoveFont(iterator->second.font);
    m_fonts.erase(iterator);
    return true;
}

UiResult<ImFont*> UiAssetManager::GetFont(const UiFontId id) const {
    const auto iterator = m_fonts.find(id);
    if (iterator == m_fonts.end()) {
        return std::unexpected(UiError{UiErrorCode::FontNotFound, "Font ID is not registered"});
    }
    return iterator->second.font;
}

void UiAssetManager::Reset() noexcept {
    m_assets.clear();
    m_asset_keys.clear();
    m_fonts.clear();
    m_next_asset_id = 1;
    m_next_font_id = 2;
    m_default_font_id = 1;
}

UiResult<UiAssetBytes> UiAssetManager::ReadSource(const UiAssetSource& source) const {
    if (const auto* bytes = std::get_if<UiAssetBytes>(&source)) {
        if (!*bytes || (*bytes)->empty()) {
            return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Asset bytes must not be empty"});
        }
        if ((*bytes)->size() > m_limits.max_asset_bytes) {
            return std::unexpected(UiError{UiErrorCode::AssetTooLarge, "Asset exceeds configured byte limit"});
        }
        return std::make_shared<const std::vector<std::byte>>(**bytes);
    }

    const std::string& path = std::get<std::string>(source);
    if (path.empty()) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Asset path must not be empty"});
    }
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::unexpected(UiError{UiErrorCode::AssetIo, "Failed to inspect asset file"});
    }
    if (size == 0 || size > m_limits.max_asset_bytes || size > static_cast<std::uintmax_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(UiError{UiErrorCode::AssetTooLarge, "Asset file size is invalid"});
    }

    auto buffer = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(buffer->data()), static_cast<std::streamsize>(buffer->size()))) {
        return std::unexpected(UiError{UiErrorCode::AssetIo, "Failed to read asset file"});
    }
    return std::make_shared<const std::vector<std::byte>>(std::move(*buffer));
}

UiResult<ImFont*> UiAssetManager::AddFont(const UiFontSpec& spec, const UiAssetBytes& bytes) const {
    if (!std::isfinite(spec.size_pixels) || spec.size_pixels <= 0.0f) {
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Runtime font size must be positive and finite"});
    }
    if (!bytes || bytes->empty() || bytes->size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(UiError{UiErrorCode::FontInitialization, "Runtime font bytes are invalid"});
    }

    const ImWchar* ranges = nullptr;
    switch (spec.glyph_range) {
    case UiGlyphRange::Default:
        break;
    case UiGlyphRange::Cyrillic:
        ranges = ImGui::GetIO().Fonts->GetGlyphRangesCyrillic();
        break;
    default:
        return std::unexpected(UiError{UiErrorCode::InvalidArgument, "Runtime font glyph range is invalid"});
    }

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<std::byte*>(bytes->data()),
        static_cast<int>(bytes->size()),
        spec.size_pixels,
        &font_config,
        ranges);
    if (!font) {
        return std::unexpected(UiError{UiErrorCode::FontInitialization, "Dear ImGui rejected runtime font data"});
    }
    return font;
}

} // namespace Uni::GUI::Detail
