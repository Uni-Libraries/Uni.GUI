#pragma once

#include <uni/gui/texture.h>

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace Uni::GUI::Detail {

struct UiTextureEntry final {
    std::unique_ptr<ImTextureData> data;
    bool registered{};
};

struct UiTextureStore final {
    ImGuiContext* context{};
    bool active{};
    std::vector<UiTextureEntry> textures;
};

struct ClippedTextureRect final {
    int x{};
    int y{};
    int width{};
    int height{};
};

[[nodiscard]] inline bool ValidateTextureSize(const int width, const int height) noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }

    constexpr auto max_dimension = static_cast<std::uint64_t>(std::numeric_limits<unsigned short>::max());
    const auto w = static_cast<std::uint64_t>(width);
    const auto h = static_cast<std::uint64_t>(height);
    constexpr std::uint64_t bytes_per_pixel = 4;

    return w <= max_dimension &&
           h <= max_dimension &&
           w <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()) / bytes_per_pixel / h;
}

[[nodiscard]] inline std::optional<ClippedTextureRect> ClipTextureRect(
    const int texture_width,
    const int texture_height,
    const int x,
    const int y,
    const int width,
    const int height) noexcept {
    if (texture_width <= 0 || texture_height <= 0 || width <= 0 || height <= 0) {
        return std::nullopt;
    }

    const auto left = static_cast<std::int64_t>(x);
    const auto top = static_cast<std::int64_t>(y);
    const auto right = left + static_cast<std::int64_t>(width);
    const auto bottom = top + static_cast<std::int64_t>(height);
    const auto texture_right = static_cast<std::int64_t>(texture_width);
    const auto texture_bottom = static_cast<std::int64_t>(texture_height);

    const auto x0 = std::clamp<std::int64_t>(left, 0, texture_right);
    const auto y0 = std::clamp<std::int64_t>(top, 0, texture_bottom);
    const auto x1 = std::clamp<std::int64_t>(right, 0, texture_right);
    const auto y1 = std::clamp<std::int64_t>(bottom, 0, texture_bottom);
    if (x1 <= x0 || y1 <= y0) {
        return std::nullopt;
    }

    return ClippedTextureRect{
        static_cast<int>(x0),
        static_cast<int>(y0),
        static_cast<int>(x1 - x0),
        static_cast<int>(y1 - y0),
    };
}

} // namespace Uni::GUI::Detail

namespace Uni::GUI {

struct UiTexture::Impl final {
    std::weak_ptr<Detail::UiTextureStore> store;
    ImTextureData* data{};

    [[nodiscard]] std::shared_ptr<Detail::UiTextureStore> LockStore() const noexcept {
        return store.lock();
    }

    [[nodiscard]] bool IsUsable(const std::shared_ptr<Detail::UiTextureStore>& owner) const noexcept {
        return owner &&
               owner->active &&
               data &&
               !data->WantDestroyNextFrame &&
               data->Status != ImTextureStatus_WantDestroy &&
               data->Status != ImTextureStatus_Destroyed;
    }
};

} // namespace Uni::GUI
