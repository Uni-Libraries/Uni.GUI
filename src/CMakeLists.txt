include(GenerateExportHeader)

add_library(unigui)
add_library(UniGUI::UniGUI ALIAS unigui)

set(unigui_public_include_dir "${PROJECT_SOURCE_DIR}/include")
set(unigui_generated_include_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${unigui_generated_include_dir}/uni/gui")

target_sources(unigui
    PRIVATE
        ui_app.cpp
        ui_asset_manager.cpp
        ui_dispatcher.cpp
        ui_dock_layout.cpp
        ui_element_registry.cpp
        ui_font.cpp
        ui_scale.cpp
        nodes/commands/clipboard.cpp
        nodes/commands/command_stack.cpp
        nodes/commands/graph.cpp
        nodes/commands/group.cpp
        nodes/commands/hierarchy.cpp
        nodes/commands/link.cpp
        nodes/commands/node.cpp
        nodes/commands/pin.cpp
        nodes/commands/protection.cpp
        nodes/commands/transaction.cpp
        nodes/commands/transaction_commit.cpp
        nodes/commands/transaction_journal.cpp
        nodes/commands/transaction_presentation.cpp
        nodes/commands/transaction_protection.cpp
        nodes/commands/transaction_semantic.cpp
        nodes/editor/clipboard.cpp
        nodes/editor/context_menu.cpp
        nodes/editor/editor.cpp
        nodes/editor/editor_context.cpp
        nodes/editor/frame.cpp
        nodes/editor/frame_helpers.cpp
        nodes/editor/geometry.cpp
        nodes/editor/geometry_cache.cpp
        nodes/editor/hit_testing.cpp
        nodes/editor/input_state.cpp
        nodes/editor/minimap.cpp
        nodes/editor/navigation.cpp
        nodes/editor/rendering.cpp
        nodes/editor/rendering_overlays.cpp
        nodes/editor/selection.cpp
        nodes/editor/shortcuts.cpp
        ui_nodes_graph.cpp
        ui_nodes_graph_validation.cpp
        ui_nodes_graph_asset.cpp
        ui_nodes_io.cpp
        ui_nodes_json.cpp
        ui_nodes_layout.cpp
        ui_nodes_node_ui.cpp
        ui_nodes_presentation.cpp
        ui_nodes_policy.cpp
        ui_nodes_registry.cpp
        ui_nodes_routing.cpp
        ui_nodes_snapshot.cpp
        ui_texture.cpp
        ui_winsys_sdl.cpp
        ui_renderer_sdl.cpp
        ui_renderer_sdlgpu.cpp
    PUBLIC
        FILE_SET public_headers TYPE HEADERS
        BASE_DIRS "${unigui_public_include_dir}"
        FILES
            "${unigui_public_include_dir}/uni/gui/app.h"
            "${unigui_public_include_dir}/uni/gui/asset.h"
            "${unigui_public_include_dir}/uni/gui/callbacks.h"
            "${unigui_public_include_dir}/uni/gui/config.h"
            "${unigui_public_include_dir}/uni/gui/dispatcher.h"
            "${unigui_public_include_dir}/uni/gui/display.h"
            "${unigui_public_include_dir}/uni/gui/element.h"
            "${unigui_public_include_dir}/uni/gui/error.h"
            "${unigui_public_include_dir}/uni/gui/event.h"
            "${unigui_public_include_dir}/uni/gui/frame.h"
            "${unigui_public_include_dir}/uni/gui/layout.h"
            "${unigui_public_include_dir}/uni/gui/nodes/commands.h"
            "${unigui_public_include_dir}/uni/gui/nodes/editor.h"
            "${unigui_public_include_dir}/uni/gui/nodes/error.h"
            "${unigui_public_include_dir}/uni/gui/nodes/graph.h"
            "${unigui_public_include_dir}/uni/gui/nodes/graph_asset.h"
            "${unigui_public_include_dir}/uni/gui/nodes/ids.h"
            "${unigui_public_include_dir}/uni/gui/nodes/io.h"
            "${unigui_public_include_dir}/uni/gui/nodes/layout.h"
            "${unigui_public_include_dir}/uni/gui/nodes/node_ui.h"
            "${unigui_public_include_dir}/uni/gui/nodes/nodes.h"
            "${unigui_public_include_dir}/uni/gui/nodes/presentation.h"
            "${unigui_public_include_dir}/uni/gui/nodes/policy.h"
            "${unigui_public_include_dir}/uni/gui/nodes/registry.h"
            "${unigui_public_include_dir}/uni/gui/nodes/routing.h"
            "${unigui_public_include_dir}/uni/gui/nodes/selection.h"
            "${unigui_public_include_dir}/uni/gui/nodes/snapshot.h"
            "${unigui_public_include_dir}/uni/gui/nodes/workspace.h"
            "${unigui_public_include_dir}/uni/gui/state.h"
            "${unigui_public_include_dir}/uni/gui/texture.h"
)

generate_export_header(unigui
    BASE_NAME uni_gui
    EXPORT_FILE_NAME "${unigui_generated_include_dir}/uni/gui/export.h"
)
target_sources(unigui
    PUBLIC
        FILE_SET generated_headers TYPE HEADERS
        BASE_DIRS "${unigui_generated_include_dir}"
        FILES "${unigui_generated_include_dir}/uni/gui/export.h"
)

target_compile_features(unigui PUBLIC cxx_std_23)
target_include_directories(unigui
    PUBLIC
        "$<BUILD_INTERFACE:${unigui_public_include_dir}>"
        "$<BUILD_INTERFACE:${unigui_generated_include_dir}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(unigui
    PUBLIC
        unigui_imgui
        unigui_implot
    PRIVATE
        SDL3::SDL3
        "$<BUILD_INTERFACE:unigui_nlohmann_json>"
)
if(WIN32)
    target_link_libraries(unigui PRIVATE advapi32)
endif()

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(unigui PUBLIC UNI_GUI_STATIC_DEFINE)
endif()

set_target_properties(unigui PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    EXPORT_NAME UniGUI
    OUTPUT_NAME UniGUI
    POSITION_INDEPENDENT_CODE ON
    VERSION "${UNIGUI_VERSION_CORE}"
    SOVERSION "${UNIGUI_ABI_VERSION}"
    VISIBILITY_INLINES_HIDDEN ON
)
if(WIN32 AND BUILD_SHARED_LIBS)
    set_property(TARGET unigui PROPERTY OUTPUT_NAME "UniGUI-${UNIGUI_ABI_VERSION}")
endif()

if(APPLE)
    set_property(TARGET unigui PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(unigui_version_script "${CMAKE_CURRENT_BINARY_DIR}/unigui.map")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/unigui.map.in" "${unigui_version_script}" @ONLY)
    set_property(TARGET unigui PROPERTY INSTALL_RPATH "$ORIGIN")
    set_property(TARGET unigui APPEND PROPERTY LINK_DEPENDS "${unigui_version_script}")
    target_link_options(unigui PRIVATE "LINKER:--version-script=${unigui_version_script}")
endif()

add_library(unigui_main STATIC ui_main.cpp)
add_library(UniGUI::Main ALIAS unigui_main)
target_compile_features(unigui_main PUBLIC cxx_std_23)
target_link_libraries(unigui_main PUBLIC unigui SDL3::SDL3)
set_target_properties(unigui_main PROPERTIES
    CXX_EXTENSIONS OFF
    EXPORT_NAME Main
    OUTPUT_NAME UniGUI-main
    POSITION_INDEPENDENT_CODE ON
)

if(UNIGUI_INSTALL)
    install(TARGETS unigui unigui_main
        EXPORT UniGUITargets
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        FILE_SET public_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
        FILE_SET generated_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
endif()
