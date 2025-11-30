#
# CMake
#

# set CMake minimum version
# v3.20: cxx_std_23
# v3.21: PROJECT_IS_TOP_LEVEL
# v3.26: -E copy -t
cmake_minimum_required(VERSION 3.26)

# module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")



#
# Project
#

project(uni.gui
  LANGUAGES CXX
)



#
# Settings
#

# C++ version
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared libs
option(BUILD_SHARED_LIBS "Build shared libs" ON)



#
# Dependencies
#

include(3rdparty/sdl.cmake)
include(3rdparty/imgui.cmake)
include(3rdparty/implot.cmake)



#
# Libs & Apps
#

add_subdirectory(src)
if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(src_demo)
endif()
