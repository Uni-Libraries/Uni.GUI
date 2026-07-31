#pragma once

#include <uni/gui/export.h>

#include <imgui.h>

#include <cstdint>
#include <memory>

namespace Uni::GUI {

class UiApp;

class UNI_GUI_EXPORT UiTexture final {
public:
    UiTexture() noexcept;
    ~UiTexture();
    UiTexture(UiTexture&& other) noexcept;
    UiTexture& operator=(UiTexture&& other) noexcept;
    UiTexture(const UiTexture&) = delete;
    UiTexture& operator=(const UiTexture&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    [[nodiscard]] ImTextureRef GetRef() const noexcept;
    [[nodiscard]] int Width() const noexcept;
    [[nodiscard]] int Height() const noexcept;
    [[nodiscard]] int Pitch() const noexcept;
    [[nodiscard]] void* Pixels() noexcept;
    [[nodiscard]] const void* Pixels() const noexcept;
    [[nodiscard]] void* PixelsAt(int x, int y) noexcept;
    [[nodiscard]] const void* PixelsAt(int x, int y) const noexcept;

    bool Clear(std::uint32_t rgba = 0);
    bool Update();
    bool UpdateRect(int x, int y, int width, int height);
    bool Destroy() noexcept;

private:
    struct Impl;

    friend class UiApp;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Uni::GUI
