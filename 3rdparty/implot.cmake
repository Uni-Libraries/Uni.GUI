add_library(implot)

target_sources(implot PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/implot/implot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/implot/implot_demo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/implot/implot_items.cpp"
)

if(WIN32 AND BUILD_SHARED_LIBS)
  target_compile_definitions(implot PRIVATE   "-DIMPLOT_API=__declspec(dllexport)")
  target_compile_definitions(implot INTERFACE "-DIMPLOT_API=__declspec(dllimport)")
endif()

target_include_directories(implot PUBLIC "${CMAKE_CURRENT_LIST_DIR}/implot/")
target_link_libraries(implot PUBLIC imgui)
set_target_properties(implot PROPERTIES POSITION_INDEPENDENT_CODE ON)
