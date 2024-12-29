CPMAddPackage(
  NAME imgui-implot
  GITHUB_REPOSITORY epezent/implot
  VERSION 0.17_20241226
  GIT_TAG f1b0792cd3e239f615d4f20b9647d37594de8c56
  DOWNLOAD_ONLY True
)

if(imgui-implot_ADDED)
  add_library(imgui-implot STATIC)
  target_sources(imgui-implot PRIVATE
          "${imgui-implot_SOURCE_DIR}/implot.cpp"
          "${imgui-implot_SOURCE_DIR}/implot_demo.cpp"
          "${imgui-implot_SOURCE_DIR}/implot_items.cpp"
  )

  target_include_directories(imgui-implot PUBLIC "${imgui-implot_SOURCE_DIR}")
  target_link_libraries(imgui-implot PUBLIC imgui)
endif()

