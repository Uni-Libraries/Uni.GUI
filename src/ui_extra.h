#pragma once

#include <imgui.h>

bool inline operator==(const ImVec2& lhs, const ImVec2& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}
