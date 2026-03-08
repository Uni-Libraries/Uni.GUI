#include "ui_texture_registry.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

namespace ImGui {
    void RegisterUserTexture(ImTextureData* tex);
    void UnregisterUserTexture(ImTextureData* tex);
}

namespace Uni::GUI::Detail {

namespace {
    thread_local UiTextureRegistry* g_ui_texture_active_registry = nullptr;

    [[nodiscard]] constexpr UiTextureHandle make_handle(const std::size_t index,
                                                        const std::uint32_t generation) noexcept {
        return (static_cast<UiTextureHandle>(generation) << 32U) |
               static_cast<UiTextureHandle>(index + 1U);
    }

    [[nodiscard]] bool decode_handle(const UiTextureHandle handle,
                                     std::size_t& index,
                                     std::uint32_t& generation) noexcept {
        if (handle == UiTextureHandleInvalid) {
            return false;
        }

        const auto encoded_index = static_cast<std::uint32_t>(handle & 0xFFFFFFFFULL);
        if (encoded_index == 0U) {
            return false;
        }

        index = static_cast<std::size_t>(encoded_index - 1U);
        generation = static_cast<std::uint32_t>(handle >> 32U);
        return generation != 0U;
    }

    [[nodiscard]] UiTextureRegistry::Slot* find_slot(UiTextureRegistry& registry, const UiTextureHandle handle) {
        std::size_t index = 0U;
        std::uint32_t generation = 0U;
        if (!decode_handle(handle, index, generation) || index >= registry.m_slots.size()) {
            return nullptr;
        }

        auto& slot = registry.m_slots[index];
        if (slot.generation != generation) {
            return nullptr;
        }

        return &slot;
    }

    [[nodiscard]] const UiTextureRegistry::Slot* find_slot(const UiTextureRegistry& registry,
                                                           const UiTextureHandle handle) {
        std::size_t index = 0U;
        std::uint32_t generation = 0U;
        if (!decode_handle(handle, index, generation) || index >= registry.m_slots.size()) {
            return nullptr;
        }

        const auto& slot = registry.m_slots[index];
        if (slot.generation != generation) {
            return nullptr;
        }

        return &slot;
    }

    [[nodiscard]] ImTextureData* find_texture(UiTextureRegistry& registry, const UiTextureHandle handle) {
        auto* slot = find_slot(registry, handle);
        return slot ? slot->texture.get() : nullptr;
    }

    [[nodiscard]] const ImTextureData* find_texture(const UiTextureRegistry& registry,
                                                    const UiTextureHandle handle) {
        const auto* slot = find_slot(registry, handle);
        return slot ? slot->texture.get() : nullptr;
    }

    [[nodiscard]] UiTextureHandle allocate_handle(UiTextureRegistry& registry) {
        std::size_t index = 0U;
        if (!registry.m_free_indices.empty()) {
            index = registry.m_free_indices.back();
            registry.m_free_indices.pop_back();
        } else {
            index = registry.m_slots.size();
            registry.m_slots.emplace_back();
        }

        return make_handle(index, registry.m_slots[index].generation);
    }

    void unregister_texture(ImTextureData& texture) {
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::UnregisterUserTexture(&texture);
        }
    }

    void retire_texture(UiTextureRegistry& registry, std::unique_ptr<ImTextureData> texture) {
        if (!texture) {
            return;
        }

        if (texture->Status != ImTextureStatus_Destroyed || texture->TexID != ImTextureID_Invalid) {
            texture->SetStatus(ImTextureStatus_WantDestroy);
            registry.m_retired.emplace_back(std::move(texture));
            return;
        }

        unregister_texture(*texture);
    }

    void set_full_update_rect(ImTextureData& texture) {
        texture.Updates.clear();
        texture.UpdateRect = {
            0,
            0,
            static_cast<unsigned short>(texture.Width),
            static_cast<unsigned short>(texture.Height)
        };
        texture.UsedRect = texture.UpdateRect;
    }

    void request_upload(ImTextureData& texture) {
        if (texture.TexID == ImTextureID_Invalid || texture.Status == ImTextureStatus_Destroyed) {
            texture.SetStatus(ImTextureStatus_WantCreate);
        } else {
            texture.SetStatus(ImTextureStatus_WantUpdates);
        }
    }

    void clear_rgba_pixels(ImTextureData& texture, const ImU32 rgba) {
        if (texture.Pixels == nullptr || texture.BytesPerPixel != 4) {
            return;
        }

        const std::uint8_t r = static_cast<std::uint8_t>((rgba >> IM_COL32_R_SHIFT) & 0xFFU);
        const std::uint8_t g = static_cast<std::uint8_t>((rgba >> IM_COL32_G_SHIFT) & 0xFFU);
        const std::uint8_t b = static_cast<std::uint8_t>((rgba >> IM_COL32_B_SHIFT) & 0xFFU);
        const std::uint8_t a = static_cast<std::uint8_t>((rgba >> IM_COL32_A_SHIFT) & 0xFFU);

        auto* px = texture.Pixels;
        const auto count = static_cast<std::size_t>(texture.Width) * static_cast<std::size_t>(texture.Height);
        for (std::size_t i = 0; i < count; ++i) {
            px[i * 4U + 0U] = r;
            px[i * 4U + 1U] = g;
            px[i * 4U + 2U] = b;
            px[i * 4U + 3U] = a;
        }
    }

    void mark_update_rect(ImTextureData& texture, int x, int y, int width, int height) {
        x = std::max(0, x);
        y = std::max(0, y);
        width = std::min(width, texture.Width - x);
        height = std::min(height, texture.Height - y);
        if (width <= 0 || height <= 0) {
            return;
        }

        const ImTextureRect rect{
            static_cast<unsigned short>(x),
            static_cast<unsigned short>(y),
            static_cast<unsigned short>(width),
            static_cast<unsigned short>(height)
        };

        texture.Updates.clear();
        texture.Updates.push_back(rect);
        texture.UpdateRect = rect;
        texture.UsedRect = rect;
        request_upload(texture);
    }
}

UiTextureRegistry::~UiTextureRegistry() {
    for (auto& slot : m_slots) {
        if (slot.texture) {
            unregister_texture(*slot.texture);
        }
    }
    for (auto& texture : m_retired) {
        if (texture) {
            unregister_texture(*texture);
        }
    }
}

UiTexture UiTextureRegistry::CreateTexture(const int width, const int height) {
    UiTexture texture{this, UiTextureHandleInvalid};
    if (!texture.CreateRGBA32(width, height)) {
        texture.Reset();
    }
    return texture;
}

bool UiTextureRegistry::Destroy(const UiTextureHandle handle) {
    auto* slot = find_slot(*this, handle);
    if (slot == nullptr) {
        return false;
    }

    std::size_t index = 0U;
    std::uint32_t generation = 0U;
    if (!decode_handle(handle, index, generation)) {
        return false;
    }

    retire_texture(*this, std::move(slot->texture));
    slot->generation++;
    m_free_indices.push_back(index);
    return true;
}

void UiTextureRegistry::CollectGarbage() {
    auto it = m_retired.begin();
    while (it != m_retired.end()) {
        auto& texture = *it;
        if (!texture) {
            it = m_retired.erase(it);
            continue;
        }

        if (texture->Status == ImTextureStatus_Destroyed && texture->TexID == ImTextureID_Invalid) {
            unregister_texture(*texture);
            it = m_retired.erase(it);
            continue;
        }

        ++it;
    }
}

bool UiTextureRegistry::CreateRGBA32(UiTextureHandle& handle, const int width, const int height) {
    if (width <= 0 || height <= 0 || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    auto* slot = find_slot(*this, handle);
    if (slot == nullptr) {
        handle = allocate_handle(*this);
        slot = find_slot(*this, handle);
    }
    if (slot == nullptr) {
        return false;
    }

    if (slot->texture && slot->texture->Width == width && slot->texture->Height == height &&
        slot->texture->Pixels != nullptr) {
        clear_rgba_pixels(*slot->texture, 0U);
        set_full_update_rect(*slot->texture);
        request_upload(*slot->texture);
        return true;
    }

    if (slot->texture) {
        retire_texture(*this, std::move(slot->texture));
    }

    auto texture = std::make_unique<ImTextureData>();
    texture->Create(ImTextureFormat_RGBA32, width, height);
    ImGui::RegisterUserTexture(texture.get());
    slot->texture = std::move(texture);
    return true;
}

bool UiTextureRegistry::IsValid(const UiTextureHandle handle) const {
    return find_texture(*this, handle) != nullptr;
}

bool UiTextureRegistry::IsCreated(const UiTextureHandle handle) const {
    const auto* texture = find_texture(*this, handle);
    return texture != nullptr && texture->Pixels != nullptr && texture->Width > 0 && texture->Height > 0;
}

int UiTextureRegistry::Width(const UiTextureHandle handle) const {
    const auto* texture = find_texture(*this, handle);
    return texture != nullptr ? texture->Width : 0;
}

int UiTextureRegistry::Height(const UiTextureHandle handle) const {
    const auto* texture = find_texture(*this, handle);
    return texture != nullptr ? texture->Height : 0;
}

int UiTextureRegistry::Pitch(const UiTextureHandle handle) const {
    const auto* texture = find_texture(*this, handle);
    return texture != nullptr ? texture->GetPitch() : 0;
}

unsigned char* UiTextureRegistry::Pixels(const UiTextureHandle handle) {
    auto* texture = find_texture(*this, handle);
    return texture != nullptr && texture->Pixels != nullptr
               ? static_cast<unsigned char*>(texture->GetPixels())
               : nullptr;
}

unsigned char* UiTextureRegistry::PixelsAt(const UiTextureHandle handle, const int x, const int y) {
    auto* texture = find_texture(*this, handle);
    return texture != nullptr && texture->Pixels != nullptr
               ? static_cast<unsigned char*>(texture->GetPixelsAt(x, y))
               : nullptr;
}

void UiTextureRegistry::Clear(const UiTextureHandle handle, const ImU32 rgba) {
    auto* texture = find_texture(*this, handle);
    if (texture == nullptr) {
        return;
    }

    clear_rgba_pixels(*texture, rgba);
}

void UiTextureRegistry::MarkFullUpdate(const UiTextureHandle handle) {
    auto* texture = find_texture(*this, handle);
    if (texture == nullptr || texture->Pixels == nullptr) {
        return;
    }

    set_full_update_rect(*texture);
    request_upload(*texture);
}

void UiTextureRegistry::MarkUpdateRect(const UiTextureHandle handle,
                                       int x,
                                       int y,
                                       int width,
                                       int height) {
    auto* texture = find_texture(*this, handle);
    if (texture == nullptr || texture->Pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }

    mark_update_rect(*texture, x, y, width, height);
}

ImTextureRef UiTextureRegistry::GetTexRef(const UiTextureHandle handle) const {
    const auto* texture = find_texture(*this, handle);
    return texture != nullptr ? const_cast<ImTextureData*>(texture)->GetTexRef() : ImTextureRef{};
}

void UiTextureSetActiveRegistry(UiTextureRegistry* registry) noexcept {
    g_ui_texture_active_registry = registry;
}

UiTextureRegistry* UiTextureGetActiveRegistry() noexcept {
    return g_ui_texture_active_registry;
}

} // namespace Uni::GUI::Detail
