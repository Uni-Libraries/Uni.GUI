cmake_minimum_required(VERSION 3.26)

project(UniGUIPackageConsumer LANGUAGES CXX)

include(CTest)

find_package(UniGUI 2.0 CONFIG REQUIRED)
find_package(UniGUI 2.0.0 EXACT CONFIG REQUIRED)

if(NOT UniGUI_VERSION STREQUAL "2.0.0" OR
   NOT UniGUI_ABI_VERSION STREQUAL "2")
    message(FATAL_ERROR
        "Unexpected installed package metadata: version=${UniGUI_VERSION}, "
        "ABI=${UniGUI_ABI_VERSION}")
endif()

foreach(required_target IN ITEMS UniGUI::UniGUI UniGUI::Main UniGUI::imgui UniGUI::implot)
    if(NOT TARGET "${required_target}")
        message(FATAL_ERROR "Missing imported target: ${required_target}")
    endif()
endforeach()

add_executable(unigui_package_consumer main.cpp)
target_link_libraries(unigui_package_consumer PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_package_consumer PROPERTIES CXX_EXTENSIONS OFF)
add_test(NAME unigui.package-consumer COMMAND unigui_package_consumer)

add_executable(unigui_package_group_presentation_linkage group_presentation_linkage.cpp)
target_link_libraries(unigui_package_group_presentation_linkage PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_package_group_presentation_linkage PROPERTIES CXX_EXTENSIONS OFF)
add_test(
    NAME unigui.package-group-presentation-linkage
    COMMAND unigui_package_group_presentation_linkage)

add_library(unigui_package_headers OBJECT
    header_ui_app.cpp
    header_ui_asset.cpp
    header_ui_callbacks.cpp
    header_ui_config.cpp
    header_ui_dispatcher.cpp
    header_ui_display.cpp
    header_ui_element.cpp
    header_ui_error.cpp
    header_ui_event.cpp
    header_ui_export.cpp
    header_ui_frame.cpp
    header_ui_layout.cpp
    header_ui_nodes.cpp
    header_ui_nodes_graph_asset.cpp
    header_ui_nodes_io.cpp
    header_ui_nodes_routing.cpp
    header_ui_nodes_snapshot.cpp
    header_ui_nodes_workspace.cpp
    header_ui_state.cpp
    header_ui_texture.cpp
)
target_link_libraries(unigui_package_headers PRIVATE UniGUI::UniGUI)
set_target_properties(unigui_package_headers PROPERTIES CXX_EXTENSIONS OFF)

add_library(unigui_package_imgui_header OBJECT header_imgui.cpp)
target_link_libraries(unigui_package_imgui_header PRIVATE UniGUI::imgui)
set_target_properties(unigui_package_imgui_header PROPERTIES CXX_EXTENSIONS OFF)

add_library(unigui_package_implot_header OBJECT header_implot.cpp)
target_link_libraries(unigui_package_implot_header PRIVATE UniGUI::implot)
set_target_properties(unigui_package_implot_header PROPERTIES CXX_EXTENSIONS OFF)

add_executable(unigui_package_startup callbacks.cpp)
target_link_libraries(unigui_package_startup PRIVATE UniGUI::Main)
set_target_properties(unigui_package_startup PROPERTIES CXX_EXTENSIONS OFF)
