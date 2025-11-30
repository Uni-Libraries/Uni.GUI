add_library(imgui)

# common
target_sources(imgui PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/imgui/imgui.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_demo.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_draw.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_tables.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/imgui/imgui_widgets.cpp"

  "${CMAKE_CURRENT_LIST_DIR}/imgui/misc/cpp/imgui_stdlib.cpp"

  "${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_sdl3.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/imgui/backends/imgui_impl_sdlrenderer3.cpp"
)

target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_LIST_DIR}/imgui/")
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_LIST_DIR}/imgui/backends/")
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_LIST_DIR}/imgui/misc/cpp/")

target_compile_definitions(imgui PUBLIC "-DIMGUI_DEFINE_MATH_OPERATORS")

if(WIN32 AND BUILD_SHARED_LIBS)
  target_compile_definitions(imgui PRIVATE   "-DIMGUI_API=__declspec(dllexport)")
  target_compile_definitions(imgui INTERFACE "-DIMGUI_API=__declspec(dllimport)")
endif()

target_link_libraries(imgui PUBLIC SDL3::SDL3)

set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
