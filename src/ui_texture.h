#pragma once

// uni.gui
#include <cstdint>

#include "uni.gui_export.h"

// forward declaration
struct ImTextureData;
struct ImTextureRef;


//
// CLass
//
namespace Uni::GUI {

class UNI_GUI_EXPORT UiTexture final{
public:
    // Ctor
    UiTexture();
    ~UiTexture();
    UiTexture(const UiTexture&) = delete;
    UiTexture& operator=(const UiTexture&) = delete;

    // Accessors
    [[nodiscard]] ImTextureRef GetRef();

    // Properties
    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] int Pitch() const;

    // Data
    [[nodiscard]] void* Pixels();
    [[nodiscard]] const void* Pixels() const;
    [[nodiscard]] void* PixelsAt(int x, int y);
    [[nodiscard]] const void* PixelsAt(int x, int y) const;

    // Operations
    bool Clear(uint32_t rgba = 0);
    bool Create(int width, int height);
    bool Update();
    bool UpdateRect(int x, int y, int rect_width, int rect_height);
private:
    ImTextureData* m_data{nullptr};
};

} // namespace Uni::GUI
