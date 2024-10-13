CPMAddPackage(
  NAME imgui-filebrowser
  GITHUB_REPOSITORY AirGuanZ/imgui-filebrowser
  VERSION 20241007
  GIT_TAG 60d4e09ab1270d94d0115ad8ec40f939e801e105
  DOWNLOAD_ONLY True
)

if(imgui-filebrowser_ADDED)
  add_library(imgui-filebrowser INTERFACE)
  target_include_directories(imgui-filebrowser INTERFACE "${imgui-filebrowser_SOURCE_DIR}")
endif()
