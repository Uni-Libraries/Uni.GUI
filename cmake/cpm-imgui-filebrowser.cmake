CPMAddPackage(
  NAME imgui-filebrowser
  GITHUB_REPOSITORY AirGuanZ/imgui-filebrowser
  GIT_SHALLOW 1
  GIT_TAG 17a87fedfa997fb2b504a6b3088cc44e64fcac6a
  DOWNLOAD_ONLY True
)

if(imgui-filebrowser_ADDED)
  add_library(imgui-filebrowser INTERFACE)
  target_include_directories(imgui-filebrowser INTERFACE "${imgui-filebrowser_SOURCE_DIR}")
endif()
