#include <uni/gui/texture.h>
#include "ui_texture_internal.h"

#include <imgui_internal.h>

#include <climits>
#include <cstdlib>
#include <iostream>
#include <type_traits>

namespace {

void Expect(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using Uni::GUI::Detail::ClipTextureRect;
    using Uni::GUI::Detail::ValidateTextureSize;
    using Uni::GUI::UiTexture;

    static_assert(!std::is_copy_constructible_v<UiTexture>);
    static_assert(!std::is_copy_assignable_v<UiTexture>);
    static_assert(std::is_nothrow_move_constructible_v<UiTexture>);
    static_assert(std::is_nothrow_move_assignable_v<UiTexture>);

    UiTexture empty;
    Expect(!empty, "A default texture must be invalid");
    Expect(empty.Width() == 0 && empty.Height() == 0 && empty.Pitch() == 0,
           "An invalid texture must report zero dimensions");
    Expect(empty.Pixels() == nullptr && empty.PixelsAt(0, 0) == nullptr,
           "An invalid texture must not expose pixels");
    Expect(!empty.Update() && !empty.UpdateRect(0, 0, 1, 1) && !empty.Destroy(),
           "Operations on an invalid texture must fail");

    Expect(!ValidateTextureSize(-1, 1), "Negative width must be rejected");
    Expect(!ValidateTextureSize(1, -1), "Negative height must be rejected");
    Expect(!ValidateTextureSize(0, 1), "Zero width must be rejected");
    Expect(!ValidateTextureSize(1, 0), "Zero height must be rejected");
    Expect(ValidateTextureSize(1, 1), "A 1x1 texture must be accepted");
    Expect(ValidateTextureSize(65535, 1), "The ImTextureRect dimension limit must be accepted");
    Expect(!ValidateTextureSize(65536, 1), "Dimensions above ImTextureRect capacity must be rejected");
    Expect(!ValidateTextureSize(65535, 65535), "Signed byte-size overflow must be rejected");

    const auto inside = ClipTextureRect(100, 80, 10, 20, 30, 40);
    Expect(inside && inside->x == 10 && inside->y == 20 && inside->width == 30 && inside->height == 40,
           "An inside rectangle must remain unchanged");

    const auto clipped = ClipTextureRect(100, 80, -10, -20, 30, 40);
    Expect(clipped && clipped->x == 0 && clipped->y == 0 && clipped->width == 20 && clipped->height == 20,
           "A partially outside rectangle must be clipped");

    Expect(!ClipTextureRect(100, 80, 100, 0, 1, 1),
           "A rectangle beyond the right edge must be rejected");
    Expect(!ClipTextureRect(100, 80, 0, 80, 1, 1),
           "A rectangle beyond the bottom edge must be rejected");
    Expect(!ClipTextureRect(100, 80, 0, 0, 0, 1),
           "A rectangle with zero width must be rejected");
    Expect(!ClipTextureRect(100, 80, INT_MAX, INT_MAX, INT_MAX, INT_MAX),
           "Overflowing positive endpoints must be safely rejected");
    Expect(!ClipTextureRect(100, 80, INT_MIN, INT_MIN, 1, 1),
           "Overflowing negative endpoints must be safely rejected");

    ImTextureData texture_data;
    texture_data.Create(ImTextureFormat_RGBA32, 4, 4);
    ImTextureDataQueueUpload(&texture_data, 0, 0, 4, 4);
    Expect(texture_data.Status == ImTextureStatus_WantCreate && texture_data.Updates.empty(),
           "A first-frame update must preserve WantCreate and use the full initial upload");

    texture_data.SetStatus(ImTextureStatus_OK);
    ImTextureDataQueueUpload(&texture_data, 1, 1, 2, 2);
    Expect(texture_data.Status == ImTextureStatus_WantUpdates && texture_data.Updates.Size == 1,
           "An initialized texture must queue incremental updates");

    return EXIT_SUCCESS;
}
