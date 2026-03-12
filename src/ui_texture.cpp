//
// Includes
//

// stdlib
#include <cstddef>
#include <cstdint>

// imgui
#include <imgui.h>

// uni.gui
#include "ui_texture.h"

#include "imgui_internal.h"



//
// Implementation
//

namespace Uni::GUI {

//
// Ctor
//

UiTexture::UiTexture(int width, int height) noexcept {
    m_data = new ImTextureData;
    m_data->Create(ImTextureFormat_RGBA32, width, height);
    ImGui::RegisterUserTexture(m_data);
}

UiTexture::~UiTexture() {
    ImGui::UnregisterUserTexture(m_data);
    delete m_data;
}


//
// Accessors
//

ImTextureRef UiTexture::GetRef() {
    return m_data->GetTexRef();
}



//
// Properties
//

int UiTexture::Width() const { return m_data->Width; }

int UiTexture::Height() const { return m_data->Height; }

int UiTexture::Pitch() const { return m_data->GetPitch(); }



//
// Data
//

void* UiTexture::Pixels() { return m_data->GetPixels(); }

const void* UiTexture::Pixels() const { return m_data->GetPixels(); }

void* UiTexture::PixelsAt(const int x, const int y) { return m_data->GetPixelsAt(x, y); }

const void* UiTexture::PixelsAt(int x, int y) const { return m_data->GetPixelsAt(x, y); }



//
// Operations
//

void UiTexture::Clear(const uint32_t rgba) {
    const std::uint8_t r = static_cast<std::uint8_t>((rgba >> IM_COL32_R_SHIFT) & 0xFFU);
    const std::uint8_t g = static_cast<std::uint8_t>((rgba >> IM_COL32_G_SHIFT) & 0xFFU);
    const std::uint8_t b = static_cast<std::uint8_t>((rgba >> IM_COL32_B_SHIFT) & 0xFFU);
    const std::uint8_t a = static_cast<std::uint8_t>((rgba >> IM_COL32_A_SHIFT) & 0xFFU);

    auto* px = static_cast<uint8_t*>(Pixels());
    const auto count = static_cast<std::size_t>(Width()) * static_cast<std::size_t>(Height());
    for (std::size_t i = 0; i < count; ++i) {
        px[i * 4U + 0U] = r;
        px[i * 4U + 1U] = g;
        px[i * 4U + 2U] = b;
        px[i * 4U + 3U] = a;
    }
}

} // namespace Uni::GUI
