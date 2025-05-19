CPMAddPackage(
  NAME imgui-imspinner
  GITHUB_REPOSITORY dalerank/imspinner
  VERSION 20250102
  GIT_TAG 5e9b1c235a207a73df2b566aa7f373f0746126fe
  DOWNLOAD_ONLY True
)

if(imgui-imspinner_ADDED)
  add_library(imgui-imspinner INTERFACE)
  target_include_directories(imgui-imspinner INTERFACE "${imgui-imspinner_SOURCE_DIR}")
endif()
