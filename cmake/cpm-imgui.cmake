CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  VERSION 1.91.9b
  GIT_TAG v1.91.9b
  DOWNLOAD_ONLY True
)

if(imgui_ADDED)
  add_library(imgui)

  # common
  target_sources(imgui PRIVATE
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
  )

  # platform
  target_sources(imgui PRIVATE
      "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
  )

  # renderer
  target_sources(imgui PRIVATE
      "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
  )
  target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")
  target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}/backends/")
  target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}/misc/cpp/")

  target_link_libraries(imgui PUBLIC SDL3-static)

  set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
