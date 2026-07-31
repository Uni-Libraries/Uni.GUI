#pragma once

#include <uni/gui/app.h>

struct ImGuiTestEngine;

void RegisterUniGuiTestEngineCases(ImGuiTestEngine* engine);
void RegisterUniGuiNodesTestEngineCases(ImGuiTestEngine* engine);
Uni::GUI::UiResult<void> InstallUniGuiTestElements(Uni::GUI::UiApp& app);
Uni::GUI::UiResult<void> InstallUniGuiNodesTestElement(Uni::GUI::UiApp& app);
