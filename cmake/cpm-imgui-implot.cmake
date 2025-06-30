CPMAddPackage(
  NAME imgui-implot
  GITHUB_REPOSITORY epezent/implot
  VERSION 0.17_20250519
  GIT_TAG 3da8bd34299965d3b0ab124df743fe3e076fa222
  DOWNLOAD_ONLY True
)

if(imgui-implot_ADDED)
  add_library(imgui-implot)
  target_sources(imgui-implot PRIVATE
          "${imgui-implot_SOURCE_DIR}/implot.cpp"
          "${imgui-implot_SOURCE_DIR}/implot_demo.cpp"
          "${imgui-implot_SOURCE_DIR}/implot_items.cpp"
  )

  if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(imgui-implot PRIVATE "-DIMPLOT_API=__declspec(dllexport)")
    target_compile_definitions(imgui-implot INTERFACE  "-DIMPLOT_API=__declspec(dllimport)")
  endif()

  target_include_directories(imgui-implot PUBLIC "${imgui-implot_SOURCE_DIR}")
  target_link_libraries(imgui-implot PUBLIC imgui)
  set_target_properties(imgui-implot PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

