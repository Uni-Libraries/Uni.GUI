#include "ui_texture.h"

#include "ui_texture_registry.h"

namespace Uni::GUI {

UiTexture::UiTexture(Detail::UiTextureRegistry* registry, const UiTextureHandle handle) noexcept
    : m_registry(registry),
      m_handle(handle) {
}

bool UiTexture::IsValid() const noexcept {
    auto* reg = registry();
    return reg != nullptr && reg->IsValid(m_handle);
}

UiTextureHandle UiTexture::Handle() const noexcept {
    return m_handle;
}

void UiTexture::Reset() noexcept {
    m_registry = nullptr;
    m_handle = UiTextureHandleInvalid;
}

bool UiTexture::CreateRGBA32(const int width, const int height) {
    auto* reg = registry();
    if (reg == nullptr) {
        return false;
    }

    m_registry = reg;
    return reg->CreateRGBA32(m_handle, width, height);
}

void UiTexture::Destroy() {
    auto* reg = registry();
    if (reg != nullptr) {
        reg->Destroy(m_handle);
    }
    m_handle = UiTextureHandleInvalid;
}

void UiTexture::Clear(const ImU32 rgba) {
    if (auto* reg = registry()) {
        reg->Clear(m_handle, rgba);
    }
}

bool UiTexture::IsCreated() const {
    auto* reg = registry();
    return reg != nullptr && reg->IsCreated(m_handle);
}

int UiTexture::Width() const {
    auto* reg = registry();
    return reg != nullptr ? reg->Width(m_handle) : 0;
}

int UiTexture::Height() const {
    auto* reg = registry();
    return reg != nullptr ? reg->Height(m_handle) : 0;
}

int UiTexture::Pitch() const {
    auto* reg = registry();
    return reg != nullptr ? reg->Pitch(m_handle) : 0;
}

unsigned char* UiTexture::Pixels() {
    auto* reg = registry();
    return reg != nullptr ? reg->Pixels(m_handle) : nullptr;
}

unsigned char* UiTexture::PixelsAt(const int x, const int y) {
    auto* reg = registry();
    return reg != nullptr ? reg->PixelsAt(m_handle, x, y) : nullptr;
}

void UiTexture::MarkFullUpdate() {
    if (auto* reg = registry()) {
        reg->MarkFullUpdate(m_handle);
    }
}

void UiTexture::MarkUpdateRect(int x, int y, int width, int height) {
    if (auto* reg = registry()) {
        reg->MarkUpdateRect(m_handle, x, y, width, height);
    }
}

ImTextureRef UiTexture::GetTexRef() const {
    auto* reg = registry();
    return reg != nullptr ? reg->GetTexRef(m_handle) : ImTextureRef{};
}

Detail::UiTextureRegistry* UiTexture::registry() const noexcept {
    return m_registry != nullptr ? m_registry : Detail::UiTextureGetActiveRegistry();
}

} // namespace Uni::GUI
