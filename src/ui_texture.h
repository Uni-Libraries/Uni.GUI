#pragma once

#include <cstdint>
#include <memory>

#include <imgui.h>

#include "uni.gui_export.h"

namespace Uni::GUI {

class UiApp;

using UiTextureHandle = std::uint64_t;
inline constexpr UiTextureHandle UiTextureHandleInvalid = 0;

namespace Detail {
    class UiTextureRegistry;
}

class UNI_GUI_EXPORT UiTexture {
public:
    UiTexture() = default;
    UiTexture(Detail::UiTextureRegistry* registry, UiTextureHandle handle) noexcept;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] UiTextureHandle Handle() const noexcept;
    void Reset() noexcept;

    bool CreateRGBA32(int width, int height);
    void Destroy();

    void Clear(ImU32 rgba = 0);
    [[nodiscard]] bool IsCreated() const;

    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] int Pitch() const;

    unsigned char* Pixels();
    unsigned char* PixelsAt(int x, int y);

    void MarkFullUpdate();
    void MarkUpdateRect(int x, int y, int width, int height);

    [[nodiscard]] ImTextureRef GetTexRef() const;

private:
    [[nodiscard]] Detail::UiTextureRegistry* registry() const noexcept;

private:
    Detail::UiTextureRegistry* m_registry{};
    UiTextureHandle m_handle{UiTextureHandleInvalid};

    friend class UiApp;
};

} // namespace Uni::GUI
