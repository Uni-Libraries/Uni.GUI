CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  VERSION 1.90.9_20240715
  GIT_TAG 5ea96c15cd87a5d9a55b35addc0f6340cf873dc6
  DOWNLOAD_ONLY True
)

if(imgui_ADDED)
  add_library(imgui STATIC)

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
endif()
