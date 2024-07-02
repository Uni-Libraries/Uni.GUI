//
// Includes
//

#include "ui_std.h"



//
// Public
//

namespace ImGui {

    bool Begin(const std::string &name, bool *p_open, ImGuiWindowFlags flags) {
        return ImGui::Begin(name.c_str(), p_open, flags);
    }

    bool BeginPopupModal(const std::string &name, bool *p_open, ImGuiWindowFlags flags) {
        return ImGui::BeginPopupModal(name.c_str(), p_open, flags);
    }

    bool BeginTabItem(const std::string &label, bool *p_open, ImGuiTabItemFlags flags) {
        return ImGui::BeginTabItem(label.c_str(), p_open, flags);
    }

    void BulletText(const std::string &text) {
        ImGui::BulletText("%s", text.c_str());
    }

    void BulletText(std::string_view text) {
        ImGui::BulletText("%s", text.data());
    }

    bool Button(const std::string &label, const ImVec2 &size_arg) {
        return ImGui::Button(label.c_str(), size_arg);
    }

    bool Checkbox(const std::string &label, bool *v) {
        return Checkbox(label.c_str(), v);
    }

    bool CollapsingHeader(const std::string &label, ImGuiTreeNodeFlags flags) {
        return ImGui::CollapsingHeader(label.c_str(), flags);
    }

    void OpenPopup(const std::string &id, ImGuiPopupFlags popup_flags) {
        ImGui::OpenPopup(id.c_str(), popup_flags);
    }

    bool RadioButton(const std::string &label, bool active) {
        return ImGui::RadioButton(label.c_str(), active);
    }

    void TableSetupColumn(const std::string &label, ImGuiTableColumnFlags flags, float init_width_or_weight,
                                 ImGuiID user_id) {
        ImGui::TableSetupColumn(label.c_str(), flags, init_width_or_weight, user_id);
    }

    void TextUnformatted(const std::string &text) {
        return ImGui::TextUnformatted(text.c_str());
    }

    bool TreeNode(const std::string &label) {
        return ImGui::TreeNode(label.c_str());
    }

    void TextColored(const ImVec4 &col, const std::string &fmt) {
        ImGui::TextColored(col, "%s", fmt.c_str());
    }
}

namespace ImPlot {
    bool BeginPlot(const std::string &title_id, const ImVec2 &size, ImPlotFlags flags) {
        return ImPlot::BeginPlot(title_id.c_str(), size, flags);
    }
}
