CPMAddPackage(
  NAME SDL
  GITHUB_REPOSITORY libsdl-org/SDL
  GIT_SHALLOW 1
  GIT_TAG e290d16c47fed58467758051e74bd2c4bb24013c
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "SDL_RENDER ON"
)
