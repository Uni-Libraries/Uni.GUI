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
    inline bool Begin(const std::string &name, bool *p_open = nullptr, ImGuiWindowFlags flags = 0) {
        return ImGui::Begin(name.c_str(), p_open, flags);
    }

    inline bool BeginPopupModal(const std::string& name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0) {
        return ImGui::BeginPopupModal(name.c_str(), p_open, flags);
    }

    inline bool BeginTabItem(const std::string& label, bool* p_open = nullptr, ImGuiTabItemFlags flags = 0) {
        return ImGui::BeginTabItem(label.c_str(), p_open, flags);
    }

    inline void BulletText(const std::string &text){
        ImGui::BulletText("%s", text.c_str());
    }

    inline void BulletText(std::string_view text){
        ImGui::BulletText("%s", text.data());
    }

    inline bool Button(const std::string &label, const ImVec2 &size_arg = ImVec2(0, 0)){
        return ImGui::Button(label.c_str(), size_arg);
    }

    inline bool Checkbox(const std::string& label, bool* v) {
        return ImGui::Checkbox(label.c_str(), v);
    }

    inline bool CollapsingHeader(const std::string& label, ImGuiTreeNodeFlags flags = 0) {
        return ImGui::CollapsingHeader(label.c_str(), flags);
    }

    inline void OpenPopup(const std::string& id, ImGuiPopupFlags popup_flags = 0){
        ImGui::OpenPopup(id.c_str(), popup_flags);
    }

    inline bool RadioButton(const std::string& label, bool active) {
        return ImGui::RadioButton(label.c_str(), active);
    }

    inline void TableSetupColumn(const std::string &label, ImGuiTableColumnFlags flags = 0, float init_width_or_weight = 0.0f,
                          ImGuiID user_id = 0) {
        ImGui::TableSetupColumn(label.c_str(), flags, init_width_or_weight, user_id);
    }

    inline void TextColored(const ImVec4& col, const std::string& fmt) {
        ImGui::TextColored(col, "%s", fmt.c_str());
    }

    inline void TextUnformatted(const std::string &text) {
        ImGui::TextUnformatted(text.c_str());
    }

    inline bool TreeNode(const std::string& label) {
        return ImGui::TreeNode(label.c_str());
    }
}

namespace ImPlot {
    inline bool BeginPlot(const std::string& title_id, const ImVec2& size, ImPlotFlags flags) {
        return ImPlot::BeginPlot(title_id.c_str(), size, flags);
    }
}
