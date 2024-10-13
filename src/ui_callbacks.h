#pragma once

//
// Includes
//

// stdlib
#include <memory>
#include <string>
#include <vector>

// Uni.Gui
#include "ui_element.h"



//
// Function
//

std::string uni_gui_app_name_get();

std::string uni_gui_app_version_get();

std::vector<std::shared_ptr<Uni::GUI::UiElement>> uni_gui_app_initialize();
