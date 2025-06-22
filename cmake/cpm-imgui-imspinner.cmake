CPMAddPackage(
  NAME imgui-imspinner
  GITHUB_REPOSITORY dalerank/imspinner
  VERSION 2025.06.17
  GIT_TAG 34e4040ec4cf3d90031eb4e1379060baa9c62b33
  DOWNLOAD_ONLY True
)

if(imgui-imspinner_ADDED)
  add_library(imgui-imspinner INTERFACE)
  target_include_directories(imgui-imspinner INTERFACE "${imgui-imspinner_SOURCE_DIR}")
endif()
