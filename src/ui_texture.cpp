#include <uni/gui/texture.h>

#include "ui_texture_internal.h"

#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Uni::GUI {

UiTexture::UiTexture() noexcept = default;

UiTexture::~UiTexture() {
    Destroy();
}

UiTexture::UiTexture(UiTexture&& other) noexcept = default;

UiTexture& UiTexture::operator=(UiTexture&& other) noexcept {
    if (this != &other) {
        Destroy();
        m_impl = std::move(other.m_impl);
    }
    return *this;
}

UiTexture::operator bool() const noexcept {
    if (!m_impl) {
        return false;
    }
    const auto store = m_impl->LockStore();
    return m_impl->IsUsable(store);
}

ImTextureRef UiTexture::GetRef() const noexcept {
    if (!m_impl) {
        return {};
    }
    const auto store = m_impl->LockStore();
    return m_impl->IsUsable(store) ? m_impl->data->GetTexRef() : ImTextureRef{};
}

int UiTexture::Width() const noexcept {
    if (!m_impl) {
        return 0;
    }
    const auto store = m_impl->LockStore();
    return m_impl->IsUsable(store) ? m_impl->data->Width : 0;
}

int UiTexture::Height() const noexcept {
    if (!m_impl) {
        return 0;
    }
    const auto store = m_impl->LockStore();
    return m_impl->IsUsable(store) ? m_impl->data->Height : 0;
}

int UiTexture::Pitch() const noexcept {
    if (!m_impl) {
        return 0;
    }
    const auto store = m_impl->LockStore();
    return m_impl->IsUsable(store) ? m_impl->data->GetPitch() : 0;
}

void* UiTexture::Pixels() noexcept {
    if (!m_impl) {
        return nullptr;
    }
    const auto store = m_impl->LockStore();
    return m_impl->IsUsable(store) ? m_impl->data->GetPixels() : nullptr;
}

const void* UiTexture::Pixels() const noexcept {
    if (!m_impl) {
        return nullptr;
    }
    const auto store = m_impl->LockStore();
    return m_impl->IsUsable(store) ? m_impl->data->GetPixels() : nullptr;
}

void* UiTexture::PixelsAt(const int x, const int y) noexcept {
    if (!m_impl) {
        return nullptr;
    }
    const auto store = m_impl->LockStore();
    if (!m_impl->IsUsable(store) || x < 0 || y < 0 || x >= m_impl->data->Width || y >= m_impl->data->Height) {
        return nullptr;
    }
    return m_impl->data->GetPixelsAt(x, y);
}

const void* UiTexture::PixelsAt(const int x, const int y) const noexcept {
    if (!m_impl) {
        return nullptr;
    }
    const auto store = m_impl->LockStore();
    if (!m_impl->IsUsable(store) || x < 0 || y < 0 || x >= m_impl->data->Width || y >= m_impl->data->Height) {
        return nullptr;
    }
    return m_impl->data->GetPixelsAt(x, y);
}

bool UiTexture::Clear(const std::uint32_t rgba) {
    if (!m_impl) {
        return false;
    }
    const auto store = m_impl->LockStore();
    if (!m_impl->IsUsable(store)) {
        return false;
    }

    const auto r = static_cast<std::uint8_t>((rgba >> IM_COL32_R_SHIFT) & 0xFFU);
    const auto g = static_cast<std::uint8_t>((rgba >> IM_COL32_G_SHIFT) & 0xFFU);
    const auto b = static_cast<std::uint8_t>((rgba >> IM_COL32_B_SHIFT) & 0xFFU);
    const auto a = static_cast<std::uint8_t>((rgba >> IM_COL32_A_SHIFT) & 0xFFU);

    auto* pixels = static_cast<std::uint8_t*>(m_impl->data->GetPixels());
    const auto count = static_cast<std::size_t>(m_impl->data->Width) * static_cast<std::size_t>(m_impl->data->Height);
    for (std::size_t i = 0; i < count; ++i) {
        pixels[i * 4U + 0U] = r;
        pixels[i * 4U + 1U] = g;
        pixels[i * 4U + 2U] = b;
        pixels[i * 4U + 3U] = a;
    }

    return Update();
}

bool UiTexture::Update() {
    return UpdateRect(0, 0, Width(), Height());
}

bool UiTexture::UpdateRect(const int x, const int y, const int width, const int height) {
    if (!m_impl) {
        return false;
    }
    const auto store = m_impl->LockStore();
    if (!m_impl->IsUsable(store)) {
        return false;
    }

    const auto rect = Detail::ClipTextureRect(m_impl->data->Width, m_impl->data->Height, x, y, width, height);
    if (!rect) {
        return false;
    }

    ImTextureDataQueueUpload(m_impl->data, rect->x, rect->y, rect->width, rect->height);
    return true;
}

bool UiTexture::Destroy() noexcept {
    if (!m_impl || !m_impl->data) {
        return false;
    }

    const auto store = m_impl->LockStore();
    const bool scheduled = store && store->active;
    if (scheduled) {
        m_impl->data->WantDestroyNextFrame = true;
    }
    m_impl.reset();
    return scheduled;
}

} // namespace Uni::GUI
