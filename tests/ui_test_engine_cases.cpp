#include "ui_test_engine_cases.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>

#include <memory>
#include <utility>

namespace {

class DockWindowsElement final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        ImGui::Begin("TE Left");
        ImGui::TextUnformatted("Left docked window");
        ImGui::End();
        ImGui::Begin("TE Right");
        ImGui::TextUnformatted("Right docked window");
        ImGui::End();
        return Uni::GUI::UiElementUpdate{};
    }
};

class RegistryChild final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState&) override {
        ImGui::Begin("Registry Child");
        const bool remove = ImGui::Button("Remove self");
        ImGui::End();
        return Uni::GUI::UiElementUpdate{!remove, Uni::GUI::UiFrameDemand::None};
    }
};

class RegistryRoot final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState& state) override {
        ImGui::Begin("Registry Root");
        if (ImGui::Button("Add child") && !m_has_child) {
            auto child = state.app.AddElement(std::make_unique<RegistryChild>());
            if (!child) {
                ImGui::End();
                return std::unexpected(std::move(child.error()));
            }
            m_has_child = true;
        }
        ImGui::End();
        return Uni::GUI::UiElementUpdate{};
    }

private:
    bool m_has_child{};
};

class TextureElement final : public Uni::GUI::UiElement {
public:
    Uni::GUI::UiResult<Uni::GUI::UiElementUpdate> Update(Uni::GUI::UiState& state) override {
        if (!m_initialized) {
            m_initialized = true;
            auto texture = state.app.CreateTexture(4, 4);
            if (!texture) {
                return std::unexpected(std::move(texture.error()));
            }
            m_texture = std::move(*texture);
            m_texture.Clear(IM_COL32(40, 120, 200, 255));
        }

        ImGui::Begin("Texture Test");
        if (m_texture) {
            ImGui::Image(m_texture.GetRef(), ImVec2(32.0f, 32.0f));
            if (ImGui::Button("Update")) {
                auto* pixel = static_cast<unsigned char*>(m_texture.PixelsAt(1, 1));
                if (pixel) {
                    pixel[0] = 255;
                    pixel[1] = 20;
                    pixel[2] = 20;
                    pixel[3] = 255;
                    m_texture.UpdateRect(1, 1, 1, 1);
                }
            }
            if (ImGui::Button("Destroy")) {
                m_texture.Destroy();
            }
        }
        ImGui::End();
        return Uni::GUI::UiElementUpdate{};
    }

    [[nodiscard]] const Uni::GUI::UiTexture& Texture() const noexcept {
        return m_texture;
    }

private:
    Uni::GUI::UiTexture m_texture;
    bool m_initialized{};
};

TextureElement* g_texture_element{};

} // namespace

void RegisterUniGuiTestEngineCases(ImGuiTestEngine* engine) {
    ImGuiTest* test = IM_REGISTER_TEST(engine, "unigui", "docking_layout");
    test->TestFunc = [](ImGuiTestContext* context) {
        context->Yield(3);
        ImGuiWindow* left = ImGui::FindWindowByName("TE Left");
        ImGuiWindow* right = ImGui::FindWindowByName("TE Right");
        IM_CHECK(left != nullptr && right != nullptr);
        IM_CHECK(left->DockNode != nullptr && right->DockNode != nullptr);
        IM_CHECK(left->DockNode->ParentNode == right->DockNode->ParentNode);
        IM_CHECK(left->Pos.x < right->Pos.x);
        std::size_t ini_size = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&ini_size);
        IM_CHECK(ini_size > 0);
        IM_CHECK(strstr(ini, "[Docking][Data]") != nullptr);
    };

    test = IM_REGISTER_TEST(engine, "unigui", "element_registry");
    test->TestFunc = [](ImGuiTestContext* context) {
        context->SetRef("Registry Root");
        context->ItemClick("Add child");
        context->Yield();
        IM_CHECK(context->ItemExists("//Registry Child/Remove self"));
        context->ItemClick("//Registry Child/Remove self");
        context->Yield(2);
        IM_CHECK(!context->ItemExists("//Registry Child/Remove self"));
        IM_CHECK(context->ItemExists("//Registry Root/Add child"));
    };

    test = IM_REGISTER_TEST(engine, "unigui", "textures");
    test->TestFunc = [](ImGuiTestContext* context) {
        context->Yield(3);
        IM_CHECK(g_texture_element != nullptr);
        IM_CHECK(static_cast<bool>(g_texture_element->Texture()));
        ImTextureRef reference = g_texture_element->Texture().GetRef();
        IM_CHECK(reference._TexData != nullptr);
        IM_CHECK_EQ(reference._TexData->Status, ImTextureStatus_OK);
        IM_CHECK(reference._TexData->TexID != ImTextureID_Invalid);
        context->ItemClick("//Texture Test/Update");
        context->Yield(2);
        reference = g_texture_element->Texture().GetRef();
        IM_CHECK(reference._TexData != nullptr);
        IM_CHECK_EQ(reference._TexData->Status, ImTextureStatus_OK);
        IM_CHECK(reference._TexData->Updates.empty());
        context->ItemClick("//Texture Test/Destroy");
        context->Yield(3);
        IM_CHECK(!static_cast<bool>(g_texture_element->Texture()));
    };

    RegisterUniGuiNodesTestEngineCases(engine);
}

Uni::GUI::UiResult<void> InstallUniGuiTestElements(Uni::GUI::UiApp& app) {
    if (auto added = app.AddElement(std::make_unique<DockWindowsElement>()); !added) {
        return std::unexpected(std::move(added.error()));
    }
    if (auto added = app.AddElement(std::make_unique<RegistryRoot>()); !added) {
        return std::unexpected(std::move(added.error()));
    }
    auto texture = std::make_unique<TextureElement>();
    g_texture_element = texture.get();
    if (auto added = app.AddElement(std::move(texture)); !added) {
        g_texture_element = nullptr;
        return std::unexpected(std::move(added.error()));
    }
    return InstallUniGuiNodesTestElement(app);
}
