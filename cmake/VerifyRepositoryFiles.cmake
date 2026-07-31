if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

set(REQUIRED_PACKAGE_FILES
    CMakeLists.txt
    LICENSE
    README.md
    CHANGELOG.md
    docs/ABI_POLICY.md
    docs/nodes/README.md
    3rdparty/Roboto.LICENSE.txt
    3rdparty/imgui/LICENSE.txt
    3rdparty/implot/LICENSE
    3rdparty/nlohmann_json/LICENSE.MIT
    cmake/UniGUIConfig.cmake.in
    cmake/ValidateBinaryPackage.cmake
    cmake/ValidateInstalledPackage.cmake
    cmake/VerifyAbiBaseline.cmake
    cmake/VerifyDocumentationLinks.cmake
    cmake/VerifyElfAbi.cmake
    cmake/VerifyPackageVersion.cmake
    cmake/VerifyReleaseVersion.cmake
    cmake/VerifyRepositoryFiles.cmake
    cmake/VerifyVersionMetadata.cmake
    src/unigui.map.in
)

foreach(REQUIRED_FILE IN LISTS REQUIRED_PACKAGE_FILES)
    set(ABSOLUTE_FILE "${SOURCE_DIR}/${REQUIRED_FILE}")
    if(NOT EXISTS "${ABSOLUTE_FILE}" OR IS_DIRECTORY "${ABSOLUTE_FILE}")
        message(FATAL_ERROR "Required package file is missing: ${REQUIRED_FILE}")
    endif()
    file(SIZE "${ABSOLUTE_FILE}" FILE_SIZE)
    if(FILE_SIZE EQUAL 0)
        message(FATAL_ERROR "Required package file is empty: ${REQUIRED_FILE}")
    endif()
endforeach()

if(REQUIRE_TRACKED)
    find_package(Git REQUIRED)
    set(REQUIRED_TRACKED_PATHS ${REQUIRED_PACKAGE_FILES})
    list(REMOVE_ITEM REQUIRED_TRACKED_PATHS
        3rdparty/imgui/LICENSE.txt
        3rdparty/implot/LICENSE
    )
    list(APPEND REQUIRED_TRACKED_PATHS
        3rdparty/imgui
        3rdparty/implot
    )
    foreach(REQUIRED_FILE IN LISTS REQUIRED_TRACKED_PATHS)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ls-files --error-unmatch -- "${REQUIRED_FILE}"
            RESULT_VARIABLE TRACKED_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT TRACKED_RESULT EQUAL 0)
            message(FATAL_ERROR "Required package file is not tracked by git: ${REQUIRED_FILE}")
        endif()
    endforeach()
endif()

list(LENGTH REQUIRED_PACKAGE_FILES REQUIRED_FILE_COUNT)
message(STATUS "Verified ${REQUIRED_FILE_COUNT} non-empty repository package files")
