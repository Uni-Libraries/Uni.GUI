CPMAddPackage(
  NAME imgui-node-editor
  GITHUB_REPOSITORY thedmd/imgui-node-editor
  GIT_TAG e78e447900909a051817a760efe13fe83e6e1afc
  DOWNLOAD_ONLY True
)

if(imgui-node-editor_ADDED)
    add_library(imgui-node-editor STATIC)
    target_sources(imgui-node-editor PRIVATE
      "${imgui-node-editor_SOURCE_DIR}/crude_json.cpp"
      "${imgui-node-editor_SOURCE_DIR}/imgui_canvas.cpp"
      "${imgui-node-editor_SOURCE_DIR}/imgui_node_editor.cpp"
      "${imgui-node-editor_SOURCE_DIR}/imgui_node_editor_api.cpp"
    )

    target_include_directories(imgui-node-editor PUBLIC "${imgui-node-editor_SOURCE_DIR}")
    target_link_libraries(imgui-node-editor PUBLIC imgui)
endif()
