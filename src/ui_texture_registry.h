#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "ui_texture.h"

namespace Uni::GUI::Detail {

class UiTextureRegistry {
public:
    struct Slot {
        std::uint32_t generation{1U};
        std::unique_ptr<ImTextureData> texture{};
    };

public:
    UiTextureRegistry() = default;
    ~UiTextureRegistry();

    UiTextureRegistry(const UiTextureRegistry&) = delete;
    UiTextureRegistry& operator=(const UiTextureRegistry&) = delete;
    UiTextureRegistry(UiTextureRegistry&&) = delete;
    UiTextureRegistry& operator=(UiTextureRegistry&&) = delete;

    [[nodiscard]] UiTexture CreateTexture(int width, int height);
    bool Destroy(UiTextureHandle handle);
    void CollectGarbage();

    [[nodiscard]] bool CreateRGBA32(UiTextureHandle& handle, int width, int height);
    [[nodiscard]] bool IsValid(UiTextureHandle handle) const;
    [[nodiscard]] bool IsCreated(UiTextureHandle handle) const;
    [[nodiscard]] int Width(UiTextureHandle handle) const;
    [[nodiscard]] int Height(UiTextureHandle handle) const;
    [[nodiscard]] int Pitch(UiTextureHandle handle) const;

    [[nodiscard]] unsigned char* Pixels(UiTextureHandle handle);
    [[nodiscard]] unsigned char* PixelsAt(UiTextureHandle handle, int x, int y);

    void Clear(UiTextureHandle handle, ImU32 rgba);
    void MarkFullUpdate(UiTextureHandle handle);
    void MarkUpdateRect(UiTextureHandle handle, int x, int y, int width, int height);

    [[nodiscard]] ImTextureRef GetTexRef(UiTextureHandle handle) const;

    // Internal concrete storage kept in the header intentionally to avoid pimpl.
    std::vector<Slot> m_slots{};
    std::vector<std::size_t> m_free_indices{};
    std::vector<std::unique_ptr<ImTextureData>> m_retired{};
};

void UiTextureSetActiveRegistry(UiTextureRegistry* registry) noexcept;
UiTextureRegistry* UiTextureGetActiveRegistry() noexcept;

} // namespace Uni::GUI::Detail
