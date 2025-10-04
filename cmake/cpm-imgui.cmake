CPMAddPackage(
  NAME imgui
  GITHUB_REPOSITORY ocornut/imgui
  VERSION 2025.10.03-docking
  GIT_TAG 8f3f4282288b17aee798b6fade6787a8a17843ca
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

  target_compile_definitions(imgui PUBLIC "-DIMGUI_DEFINE_MATH_OPERATORS")

  if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(imgui PRIVATE "-DIMGUI_API=__declspec(dllexport)")
    target_compile_definitions(imgui INTERFACE  "-DIMGUI_API=__declspec(dllimport)")
  endif()

  
  target_link_libraries(imgui PUBLIC SDL3::SDL3)

  set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
