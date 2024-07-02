#
# CMake
#

# set CMake minimum version
# v3.20: cxx_std_23
cmake_minimum_required(VERSION 3.20)

# module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/")

# remove garbage CDash build targets
set_property(GLOBAL PROPERTY CTEST_TARGETS_ADDED 1)

# C++ version
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)



#
# Project
#

project(uni.gui
  LANGUAGES CXX
  VERSION 0.0.1
)



#
# Dependencies
#

# PM
include(cmake/cpm.cmake)

# SDL3
include(cmake/cpm-sdl3.cmake)

# ImGUI
include(cmake/cpm-imgui.cmake)
include(cmake/cpm-imgui-filebrowser.cmake)
include(cmake/cpm-imgui-implot.cmake)
include(cmake/cpm-imgui-imspinner.cmake)
include(cmake/cpm-imgui-notify.cmake)
include(cmake/cpm-imgui-node-editor.cmake)



#
# Libs & Apps
#

add_subdirectory(src)
add_subdirectory(src_gui)