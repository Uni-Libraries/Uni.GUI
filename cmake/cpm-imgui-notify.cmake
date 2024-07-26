CPMAddPackage(
  NAME imgui-notify
  GITHUB_REPOSITORY patrickcjk/imgui-notify
  GIT_TAG eb35d8d9505c6f59a54e9fdb402952e2e4faf1ee
  DOWNLOAD_ONLY True
)

if(imgui-notify_ADDED)
  add_library(imgui-notify INTERFACE)
  target_include_directories(imgui-notify INTERFACE "${imgui-notify_SOURCE_DIR}/example/src")
endif()
