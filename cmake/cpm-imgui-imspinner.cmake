CPMAddPackage(
  NAME imgui-imspinner
  GITHUB_REPOSITORY dalerank/imspinner
  GIT_SHALLOW 1
  GIT_TAG ff2eb8c3230debc878db689c9113498a7de97804
  DOWNLOAD_ONLY True
)

if(imgui-imspinner_ADDED)
  add_library(imgui-imspinner INTERFACE)
  target_include_directories(imgui-imspinner INTERFACE "${imgui-imspinner_SOURCE_DIR}")
endif()
