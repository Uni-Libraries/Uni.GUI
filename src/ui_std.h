#pragma once

//
// Includes
//

// stdlib
#include <string>

// imgui
#include <imgui.h>
#include <imgui_stdlib.h>
#include <implot.h>


//
// Public
//

namespace ImGui {
    bool Begin(const std::string &name, bool *p_open = nullptr, ImGuiWindowFlags flags = 0);

    bool BeginPopupModal(const std::string& name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);

    bool BeginTabItem(const std::string& label, bool* p_open = nullptr, ImGuiTabItemFlags flags = 0);

    void BulletText(const std::string &text);

    void BulletText(std::string_view text);

    bool Button(const std::string &label, const ImVec2 &size_arg = ImVec2(0, 0));

    bool Checkbox(const std::string& label, bool* v);

    bool CollapsingHeader(const std::string& label, ImGuiTreeNodeFlags flags = 0);

    void OpenPopup(const std::string& id, ImGuiPopupFlags popup_flags = 0);

    bool RadioButton(const std::string& label, bool active);

    void TableSetupColumn(const std::string &label, ImGuiTableColumnFlags flags = 0, float init_width_or_weight = 0.0f,
                          ImGuiID user_id = 0);

    void TextColored(const ImVec4& col, const std::string& fmt);

    void TextUnformatted(const std::string &text);

    bool TreeNode(const std::string& label);
}

namespace ImPlot {
    bool BeginPlot(const std::string& title_id, const ImVec2& size, ImPlotFlags flags);
}