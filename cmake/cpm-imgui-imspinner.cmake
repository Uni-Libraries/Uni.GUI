CPMAddPackage(
  NAME imgui-imspinner
  GITHUB_REPOSITORY dalerank/imspinner
  VERSION 20241228
  GIT_TAG fde80fb920131471d6a4b02a0e51d8ab87e8586f
  DOWNLOAD_ONLY True
)

if(imgui-imspinner_ADDED)
  add_library(imgui-imspinner INTERFACE)
  target_include_directories(imgui-imspinner INTERFACE "${imgui-imspinner_SOURCE_DIR}")
endif()
