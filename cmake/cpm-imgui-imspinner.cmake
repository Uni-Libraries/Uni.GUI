CPMAddPackage(
  NAME imgui-imspinner
  GITHUB_REPOSITORY dalerank/imspinner
  VERSION 20240807
  GIT_TAG fde83465c0efaca55cf60716f217599bbf929707
  DOWNLOAD_ONLY True
)

if(imgui-imspinner_ADDED)
  add_library(imgui-imspinner INTERFACE)
  target_include_directories(imgui-imspinner INTERFACE "${imgui-imspinner_SOURCE_DIR}")
endif()
