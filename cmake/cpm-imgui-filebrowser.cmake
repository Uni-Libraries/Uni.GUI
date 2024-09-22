CPMAddPackage(
  NAME imgui-filebrowser
  GITHUB_REPOSITORY AirGuanZ/imgui-filebrowser
  VERSION 20240901
  GIT_TAG b8fedfb673a4e13413bcac789d4c0b63bc19d948
  DOWNLOAD_ONLY True
)

if(imgui-filebrowser_ADDED)
  add_library(imgui-filebrowser INTERFACE)
  target_include_directories(imgui-filebrowser INTERFACE "${imgui-filebrowser_SOURCE_DIR}")
endif()
