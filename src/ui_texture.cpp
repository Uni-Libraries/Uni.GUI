//
// Includes
//

// stdlib
#include <algorithm>
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

UiTexture::UiTexture() = default;

UiTexture::~UiTexture() {
    if (m_data) {
        ImGui::UnregisterUserTexture(m_data);
        delete m_data;
        m_data = nullptr;
    }
}



//
// Accessors
//

ImTextureRef UiTexture::GetRef() {
    if (!m_data) {
        return {};
    }
    return m_data->GetTexRef();
}



//
// Properties
//

int UiTexture::Width() const {
    if (!m_data) {
        return 0;
    }
    return m_data->Width;
}

int UiTexture::Height() const {
    if (!m_data) {
        return 0;
    }
    return m_data->Height;
}

int UiTexture::Pitch() const {
    if (!m_data) {
        return 0;
    }
    return m_data->GetPitch();
}



//
// Data
//

void* UiTexture::Pixels() {
    if (!m_data) {
        return nullptr;
    }
    return m_data->GetPixels();
}

const void* UiTexture::Pixels() const {
    if (!m_data) {
        return nullptr;
    }
    return m_data->GetPixels();
}

void* UiTexture::PixelsAt(const int x, const int y) {
    if (!m_data) {
        return nullptr;
    }
    return m_data->GetPixelsAt(x, y);
}

const void* UiTexture::PixelsAt(int x, int y) const {
    if (!m_data) {
        return nullptr;
    }
    return m_data->GetPixelsAt(x, y);
}



//
// Operations
//

bool UiTexture::Clear(const uint32_t rgba) {
    if (!m_data) {
        return false;
    }

    const auto r = static_cast<std::uint8_t>((rgba >> IM_COL32_R_SHIFT) & 0xFFU);
    const auto g = static_cast<std::uint8_t>((rgba >> IM_COL32_G_SHIFT) & 0xFFU);
    const auto b = static_cast<std::uint8_t>((rgba >> IM_COL32_B_SHIFT) & 0xFFU);
    const auto a = static_cast<std::uint8_t>((rgba >> IM_COL32_A_SHIFT) & 0xFFU);

    auto* px = static_cast<uint8_t*>(Pixels());
    if (!px) {
        return false;
    }

    const auto count = static_cast<std::size_t>(Width()) * static_cast<std::size_t>(Height());
    for (std::size_t i = 0; i < count; ++i) {
        px[i * 4U + 0U] = r;
        px[i * 4U + 1U] = g;
        px[i * 4U + 2U] = b;
        px[i * 4U + 3U] = a;
    }

    return Update();
}

bool UiTexture::Create(int width, int height) {
    if (m_data) {
        return false;
    }

    if (width > USHRT_MAX || height > USHRT_MAX) {
        return false;
    }

    m_data = new ImTextureData;
    m_data->Create(ImTextureFormat_RGBA32, width, height);
    ImGui::RegisterUserTexture(m_data);
    return true;
}

bool UiTexture::Update() { return UpdateRect(0, 0, Width(), Height()); }

bool UiTexture::UpdateRect(const int x, const int y, const int width, const int height) {
    if (!m_data || !m_data->Pixels || width <= 0 || height <= 0 || Width() <= 0 || Height() <= 0) {
        return false;
    }

    const int x0 = std::clamp(x, 0, Width());
    const int y0 = std::clamp(y, 0, Height());
    const int x1 = std::clamp(x + width, 0, Width());
    const int y1 = std::clamp(y + height, 0, Height());
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }

    if (m_data->Status != ImTextureStatus_OK && m_data->Status != ImTextureStatus_WantUpdates) {
        return false;
    }

    const ImTextureRect req{
        static_cast<unsigned short>(x0),
        static_cast<unsigned short>(y0),
        static_cast<unsigned short>(x1 - x0),
        static_cast<unsigned short>(y1 - y0),
    };

    if (m_data->UpdateRect.w == 0 || m_data->UpdateRect.h == 0) {
        m_data->UpdateRect = req;
    } else {
        const int update_x0 = std::min<int>(m_data->UpdateRect.x, req.x);
        const int update_y0 = std::min<int>(m_data->UpdateRect.y, req.y);
        const int update_x1 = std::max<int>(static_cast<int>(m_data->UpdateRect.x) + static_cast<int>(m_data->UpdateRect.w), req.x + req.w);
        const int update_y1 = std::max<int>(static_cast<int>(m_data->UpdateRect.y) + static_cast<int>(m_data->UpdateRect.h), req.y + req.h);
        m_data->UpdateRect.x = static_cast<unsigned short>(update_x0);
        m_data->UpdateRect.y = static_cast<unsigned short>(update_y0);
        m_data->UpdateRect.w = static_cast<unsigned short>(update_x1 - update_x0);
        m_data->UpdateRect.h = static_cast<unsigned short>(update_y1 - update_y0);
    }

    if (m_data->UsedRect.w == 0 || m_data->UsedRect.h == 0) {
        m_data->UsedRect = req;
    } else {
        const int used_x0 = std::min<int>(m_data->UsedRect.x, req.x);
        const int used_y0 = std::min<int>(m_data->UsedRect.y, req.y);
        const int used_x1 = std::max<int>(static_cast<int>(m_data->UsedRect.x) + static_cast<int>(m_data->UsedRect.w), req.x + req.w);
        const int used_y1 = std::max<int>(static_cast<int>(m_data->UsedRect.y) + static_cast<int>(m_data->UsedRect.h), req.y + req.h);
        m_data->UsedRect.x = static_cast<unsigned short>(used_x0);
        m_data->UsedRect.y = static_cast<unsigned short>(used_y0);
        m_data->UsedRect.w = static_cast<unsigned short>(used_x1 - used_x0);
        m_data->UsedRect.h = static_cast<unsigned short>(used_y1 - used_y0);
    }

    m_data->SetStatus(ImTextureStatus_WantUpdates);
    m_data->Updates.push_back(req);
    return true;
}

} // namespace Uni::GUI
