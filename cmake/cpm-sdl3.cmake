CPMAddPackage(
  NAME SDL
  GITHUB_REPOSITORY libsdl-org/SDL
  VERSION 3.2.8
  GIT_TAG release-3.2.8
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "SDL_RENDER ON"
)
