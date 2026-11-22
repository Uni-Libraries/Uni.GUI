#pragma once

#include <uni/gui/export.h>

#include <imgui.h>

#include <cstdint>
#include <memory>

namespace Uni::GUI {

class UiApp;

/// Move-only, UI-thread handle to an app-owned RGBA32 texture. A handle may outlive
/// the app, but then behaves as empty. Do not retain pixel pointers or ImTextureRef
/// after destroying the texture or shutting down its app.
class UNI_GUI_EXPORT UiTexture final {
public:
    UiTexture() noexcept;
    ~UiTexture();
    UiTexture(UiTexture&& other) noexcept;
    UiTexture& operator=(UiTexture&& other) noexcept;
    UiTexture(const UiTexture&) = delete;
    UiTexture& operator=(const UiTexture&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept;
    /// Borrow the texture reference for ImGui drawing; returns an empty reference if unusable.
    [[nodiscard]] ImTextureRef GetRef() const noexcept;
    [[nodiscard]] int Width() const noexcept;
    [[nodiscard]] int Height() const noexcept;
    /// Row stride in bytes, or zero if unusable.
    [[nodiscard]] int Pitch() const noexcept;
    /// Borrow the CPU pixel buffer; use Update() or UpdateRect() after writing.
    [[nodiscard]] void* Pixels() noexcept;
    [[nodiscard]] const void* Pixels() const noexcept;
    /// Borrow one pixel, or nullptr outside the texture bounds or when unusable.
    [[nodiscard]] void* PixelsAt(int x, int y) noexcept;
    [[nodiscard]] const void* PixelsAt(int x, int y) const noexcept;

    /// Fill with a packed IM_COL32 color and queue a full upload. Returns false if unusable.
    bool Clear(std::uint32_t rgba = 0);
    /// Queue a full upload of the CPU buffer, not an immediate GPU transfer.
    bool Update();
    /// Queue a clipped rectangle. Returns false if unusable or if the intersection has no area.
    bool UpdateRect(int x, int y, int width, int height);
    /// Invalidate the handle immediately; returns true if deferred destruction was scheduled with a live app.
    bool Destroy() noexcept;

private:
    struct Impl;

    friend class UiApp;

    std::unique_ptr<Impl> m_impl;
};

} // namespace Uni::GUI
