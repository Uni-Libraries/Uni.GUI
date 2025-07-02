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

# remove garbage CDash build targets
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)


#
# Project
#

project(uni.gui
  LANGUAGES CXX
  VERSION 0.0.1
)



#
# Settings
#

# C++ version
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared libs
option(BUILD_SHARED_LIBS "Build shared libs" OFF)


#
# Dependencies
#

# PM
include(cmake/cpm.cmake)

# SDL3
if(WIN32)
    include(cmake/cpm-sdl3.cmake)
else()
    find_package(SDL3 REQUIRED)
endif()

# ImGUI
include(cmake/cpm-imgui.cmake)
include(cmake/cpm-imgui-implot.cmake)
include(cmake/cpm-imgui-imspinner.cmake)
include(cmake/cpm-imgui-notify.cmake)



#
# Libs & Apps
#

add_subdirectory(src)

if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(src_gui)
endif()
