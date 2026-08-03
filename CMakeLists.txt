cmake_minimum_required(VERSION 3.26)

set(UNIGUI_VERSION "2.0.0")
set(UNIGUI_RELEASE_DATE "2026-08-04")
string(REGEX MATCH
    "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"
    UNIGUI_VERSION_MATCH
    "${UNIGUI_VERSION}"
)
if(NOT UNIGUI_VERSION_MATCH STREQUAL UNIGUI_VERSION)
    message(FATAL_ERROR "Invalid canonical UniGUI version: ${UNIGUI_VERSION}")
endif()
set(UNIGUI_VERSION_CORE "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

project(UniGUI VERSION "${UNIGUI_VERSION_CORE}" LANGUAGES CXX)

set(UNIGUI_ABI_VERSION "${PROJECT_VERSION_MAJOR}")

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(UNIGUI_BUILD_DEMO "Build the UniGUI demo" ${PROJECT_IS_TOP_LEVEL})
option(UNIGUI_BUILD_TESTS "Build UniGUI tests" ${PROJECT_IS_TOP_LEVEL})
option(UNIGUI_ENABLE_RUNTIME_TESTS "Enable tests that require a graphical display" OFF)
option(UNIGUI_BUILD_TEST_ENGINE_TESTS "Build Dear ImGui Test Engine integration tests" OFF)
option(UNIGUI_BUILD_BENCHMARKS "Build UniGUI performance regression benchmarks" OFF)
option(UNIGUI_INSTALL "Generate UniGUI install rules" ${PROJECT_IS_TOP_LEVEL})
option(UNIGUI_ENABLE_ABI_BASELINE_CHECK "Compare the shared UniGUI library with a libabigail baseline" OFF)
set(UNIGUI_ABI_BASELINE_DIR "" CACHE PATH
    "Directory containing released UniGUI, ImGui, and ImPlot ABI and symbol manifests")

if(UNIGUI_ENABLE_ABI_BASELINE_CHECK)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT BUILD_SHARED_LIBS OR NOT UNIGUI_BUILD_TESTS)
        message(FATAL_ERROR
            "UNIGUI_ENABLE_ABI_BASELINE_CHECK requires Linux, BUILD_SHARED_LIBS=ON, and UNIGUI_BUILD_TESTS=ON")
    endif()
    foreach(baseline_name IN ITEMS UniGUI UniGUI-imgui UniGUI-implot)
        foreach(baseline_extension IN ITEMS abi symbols)
            set(baseline_file
                "${UNIGUI_ABI_BASELINE_DIR}/${baseline_name}.${baseline_extension}")
            if(NOT EXISTS "${baseline_file}" OR IS_DIRECTORY "${baseline_file}")
                message(FATAL_ERROR "Missing released ABI baseline: ${baseline_file}")
            endif()
        endforeach()
    endforeach()
    find_program(UNIGUI_ABIDIFF_EXECUTABLE NAMES abidiff REQUIRED)
    find_program(UNIGUI_NM_EXECUTABLE NAMES nm llvm-nm REQUIRED)
    find_program(UNIGUI_READELF_EXECUTABLE NAMES readelf llvm-readelf REQUIRED)
endif()

include(3rdparty/nlohmann_json.cmake)

if(UNIGUI_BUILD_TEST_ENGINE_TESTS AND (NOT UNIGUI_BUILD_TESTS OR NOT UNIGUI_ENABLE_RUNTIME_TESTS))
    message(FATAL_ERROR "UNIGUI_BUILD_TEST_ENGINE_TESTS requires UNIGUI_BUILD_TESTS and UNIGUI_ENABLE_RUNTIME_TESTS")
endif()

if(UNIGUI_BUILD_TEST_ENGINE_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        imgui_test_engine
        GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
        GIT_TAG 508a8fc8dacac2f346d353fed31b9bc90ed29adc
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(imgui_test_engine)
endif()

find_package(SDL3 3.4 CONFIG REQUIRED)

include(3rdparty/imgui.cmake)
include(3rdparty/implot.cmake)

add_subdirectory(src)

if(UNIGUI_BUILD_TESTS OR UNIGUI_BUILD_BENCHMARKS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(UNIGUI_BUILD_DEMO)
    add_subdirectory(src_demo)
endif()

if(UNIGUI_INSTALL)
    set(unigui_cmake_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/UniGUI")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/UniGUIConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/UniGUIConfig.cmake"
        INSTALL_DESTINATION "${unigui_cmake_install_dir}"
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/UniGUIConfigVersion.cmake"
        VERSION "${UNIGUI_VERSION_CORE}"
        COMPATIBILITY SameMajorVersion
    )

    install(
        EXPORT UniGUITargets
        FILE UniGUITargets.cmake
        NAMESPACE UniGUI::
        DESTINATION "${unigui_cmake_install_dir}"
    )
    install(
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/UniGUIConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/UniGUIConfigVersion.cmake"
        DESTINATION "${unigui_cmake_install_dir}"
    )
    set(unigui_license_install_dir "${CMAKE_INSTALL_DATADIR}/licenses/UniGUI")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${unigui_license_install_dir}")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/imgui/LICENSE.txt"
        DESTINATION "${unigui_license_install_dir}"
        RENAME LICENSE.imgui)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/implot/LICENSE"
        DESTINATION "${unigui_license_install_dir}"
        RENAME LICENSE.implot)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/Roboto.LICENSE.txt"
        DESTINATION "${unigui_license_install_dir}"
        RENAME LICENSE.Roboto)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/nlohmann_json/LICENSE.MIT"
        DESTINATION "${unigui_license_install_dir}"
        RENAME LICENSE.nlohmann-json)
    set(unigui_doc_install_dir "${CMAKE_INSTALL_DATADIR}/doc/UniGUI")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/ABI_POLICY.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/display_scaling.md"
        DESTINATION "${unigui_doc_install_dir}/docs")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/nodes"
        DESTINATION "${unigui_doc_install_dir}/docs")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
        DESTINATION "${unigui_doc_install_dir}")
endif()

if(PROJECT_IS_TOP_LEVEL AND UNIGUI_INSTALL)
    set(UNIGUI_PACKAGE_PLATFORM_SUFFIX "" CACHE STRING
        "Explicit release archive platform suffix")
    if(UNIGUI_PACKAGE_PLATFORM_SUFFIX STREQUAL "")
        set(UNIGUI_PACKAGE_PLATFORM_SUFFIX
            "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(CPACK_PACKAGE_NAME "UniGUI")
    set(CPACK_PACKAGE_VENDOR "UniGUI contributors")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++23 SDL3 application shell for Dear ImGui and ImPlot")
    set(CPACK_PACKAGE_VERSION "${UNIGUI_VERSION}")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    set(CPACK_PACKAGE_FILE_NAME
        "UniGUI-${UNIGUI_VERSION}-${UNIGUI_PACKAGE_PLATFORM_SUFFIX}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_SOURCE_PACKAGE_FILE_NAME "UniGUI-${UNIGUI_VERSION}-Source")
    set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
    set(CPACK_SOURCE_IGNORE_FILES
        "/\\.git/"
        "/\\.git$"
        "/\\.github/"
        "/build[^/]*/"
        "/sdl-source/"
        "/release-consumer/"
        "/abi-baseline/"
        "/release-abi/"
        "/stage/"
        "/\\.cache/"
        "/\\.kilo/"
        "/docs/audit\\.md$"
        "~$"
    )
    include(CPack)
endif()
