CPMAddPackage(
  NAME imgui-implot
  GITHUB_REPOSITORY epezent/implot
  GIT_TAG f156599faefe316f7dd20fe6c783bf87c8bb6fd9
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

