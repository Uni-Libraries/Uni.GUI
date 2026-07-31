if(NOT DEFINED INSTALL_PREFIX OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR OR NOT DEFINED EXPECT_STATIC)
    message(FATAL_ERROR "INSTALL_PREFIX, SOURCE_DIR, BUILD_DIR, and EXPECT_STATIC are required")
endif()

if(NOT DEFINED INSTALL_DATADIR)
    set(INSTALL_DATADIR share)
endif()

file(GLOB_RECURSE PACKAGE_CONFIGS "${INSTALL_PREFIX}/*/cmake/UniGUI/*.cmake")
if(NOT PACKAGE_CONFIGS)
    message(FATAL_ERROR "Installed UniGUI CMake package was not found")
endif()

set(EXPORT_CONTENT "")
set(PACKAGE_CONFIG_FILE "")
foreach(CONFIG_FILE IN LISTS PACKAGE_CONFIGS)
    file(READ "${CONFIG_FILE}" CONFIG_CONTENT)
    string(APPEND EXPORT_CONTENT "${CONFIG_CONTENT}")
    get_filename_component(CONFIG_NAME "${CONFIG_FILE}" NAME)
    if(CONFIG_NAME STREQUAL "UniGUIConfig.cmake")
        set(PACKAGE_CONFIG_FILE "${CONFIG_FILE}")
        set(PACKAGE_CONFIG_CONTENT "${CONFIG_CONTENT}")
    endif()
endforeach()
if(PACKAGE_CONFIG_FILE STREQUAL "")
    message(FATAL_ERROR "Installed UniGUIConfig.cmake was not found")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/VerifyVersionMetadata.cmake")

foreach(package_contract IN ITEMS
        "set(UniGUI_VERSION \"${UNIGUI_METADATA_VERSION}\")"
        "set(UniGUI_ABI_VERSION \"${UNIGUI_METADATA_ABI_VERSION}\")")
    string(FIND "${PACKAGE_CONFIG_CONTENT}" "${package_contract}" contract_position)
    if(contract_position EQUAL -1)
        message(FATAL_ERROR
            "Installed UniGUIConfig.cmake is missing metadata contract: ${package_contract}")
    endif()
endforeach()

file(TO_CMAKE_PATH "${SOURCE_DIR}" SOURCE_PATH)
file(TO_CMAKE_PATH "${BUILD_DIR}" BUILD_PATH)
foreach(FORBIDDEN_PATH IN ITEMS "${SOURCE_PATH}" "${BUILD_PATH}")
    string(FIND "${EXPORT_CONTENT}" "${FORBIDDEN_PATH}" PATH_POSITION)
    if(NOT PATH_POSITION EQUAL -1)
        message(FATAL_ERROR "Installed package leaks build/source path: ${FORBIDDEN_PATH}")
    endif()
endforeach()

if(EXPECT_STATIC)
    string(FIND "${EXPORT_CONTENT}" "UNI_GUI_STATIC_DEFINE" STATIC_DEFINE_POSITION)
    if(STATIC_DEFINE_POSITION EQUAL -1)
        message(FATAL_ERROR "Static package does not propagate UNI_GUI_STATIC_DEFINE")
    endif()
else()
    string(FIND "${EXPORT_CONTENT}" "UNI_GUI_STATIC_DEFINE" STATIC_DEFINE_POSITION)
    if(NOT STATIC_DEFINE_POSITION EQUAL -1)
        message(FATAL_ERROR "Shared package unexpectedly propagates UNI_GUI_STATIC_DEFINE")
    endif()
endif()

if(NOT EXISTS "${INSTALL_PREFIX}/include/uni/gui/export.h")
    message(FATAL_ERROR "Generated public export header was not installed")
endif()

set(REQUIRED_INSTALLED_FILES
    ${INSTALL_DATADIR}/licenses/UniGUI/LICENSE
    ${INSTALL_DATADIR}/licenses/UniGUI/LICENSE.imgui
    ${INSTALL_DATADIR}/licenses/UniGUI/LICENSE.implot
    ${INSTALL_DATADIR}/licenses/UniGUI/LICENSE.Roboto
    ${INSTALL_DATADIR}/licenses/UniGUI/LICENSE.nlohmann-json
    ${INSTALL_DATADIR}/doc/UniGUI/README.md
    ${INSTALL_DATADIR}/doc/UniGUI/CHANGELOG.md
    ${INSTALL_DATADIR}/doc/UniGUI/docs/ABI_POLICY.md
    ${INSTALL_DATADIR}/doc/UniGUI/docs/nodes/README.md
)
foreach(REQUIRED_FILE IN LISTS REQUIRED_INSTALLED_FILES)
    set(ABSOLUTE_FILE "${INSTALL_PREFIX}/${REQUIRED_FILE}")
    if(NOT EXISTS "${ABSOLUTE_FILE}" OR IS_DIRECTORY "${ABSOLUTE_FILE}")
        message(FATAL_ERROR "Required installed package file is missing: ${REQUIRED_FILE}")
    endif()
    file(SIZE "${ABSOLUTE_FILE}" FILE_SIZE)
    if(FILE_SIZE EQUAL 0)
        message(FATAL_ERROR "Required installed package file is empty: ${REQUIRED_FILE}")
    endif()
endforeach()

set(INSTALLED_DOC_ROOT "${INSTALL_PREFIX}/${INSTALL_DATADIR}/doc/UniGUI")
foreach(DOCUMENT_PATH IN ITEMS
        README.md
        CHANGELOG.md
        docs/ABI_POLICY.md
        docs/nodes/README.md)
    file(SHA256 "${SOURCE_DIR}/${DOCUMENT_PATH}" SOURCE_DOCUMENT_HASH)
    file(SHA256 "${INSTALLED_DOC_ROOT}/${DOCUMENT_PATH}" INSTALLED_DOCUMENT_HASH)
    if(NOT INSTALLED_DOCUMENT_HASH STREQUAL SOURCE_DOCUMENT_HASH)
        message(FATAL_ERROR
            "Installed documentation differs from release metadata source: ${DOCUMENT_PATH}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${INSTALLED_DOC_ROOT}"
        -P "${CMAKE_CURRENT_LIST_DIR}/VerifyDocumentationLinks.cmake"
    RESULT_VARIABLE DOCUMENTATION_RESULT
    OUTPUT_VARIABLE DOCUMENTATION_OUTPUT
    ERROR_VARIABLE DOCUMENTATION_ERROR
)
if(NOT DOCUMENTATION_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Installed documentation link validation failed:\n${DOCUMENTATION_OUTPUT}${DOCUMENTATION_ERROR}")
endif()

if(NOT EXPECT_STATIC AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    get_filename_component(PACKAGE_CMAKE_DIR "${PACKAGE_CONFIG_FILE}" DIRECTORY)
    get_filename_component(PACKAGE_CMAKE_PARENT "${PACKAGE_CMAKE_DIR}" DIRECTORY)
    get_filename_component(INSTALL_LIBRARY_DIR "${PACKAGE_CMAKE_PARENT}" DIRECTORY)
    set(CORE_LIBRARY
        "${INSTALL_LIBRARY_DIR}/libUniGUI.so.${UNIGUI_METADATA_ABI_VERSION}")
    set(IMGUI_LIBRARY
        "${INSTALL_LIBRARY_DIR}/libUniGUI-imgui.so.${UNIGUI_METADATA_ABI_VERSION}")
    set(IMPLOT_LIBRARY
        "${INSTALL_LIBRARY_DIR}/libUniGUI-implot.so.${UNIGUI_METADATA_ABI_VERSION}")
    set(EXPECTED_ABI_VERSION "${UNIGUI_METADATA_ABI_VERSION}")
    include("${CMAKE_CURRENT_LIST_DIR}/VerifyElfAbi.cmake")
elseif(NOT EXPECT_STATIC AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    find_program(OTOOL_EXECUTABLE NAMES otool REQUIRED)
    get_filename_component(PACKAGE_CMAKE_DIR "${PACKAGE_CONFIG_FILE}" DIRECTORY)
    get_filename_component(PACKAGE_CMAKE_PARENT "${PACKAGE_CMAKE_DIR}" DIRECTORY)
    get_filename_component(INSTALL_LIBRARY_DIR "${PACKAGE_CMAKE_PARENT}" DIRECTORY)
    foreach(library_name IN ITEMS UniGUI UniGUI-imgui UniGUI-implot)
        set(library_path
            "${INSTALL_LIBRARY_DIR}/lib${library_name}.${UNIGUI_METADATA_ABI_VERSION}.dylib")
        if(NOT EXISTS "${library_path}" OR IS_DIRECTORY "${library_path}")
            message(FATAL_ERROR "Installed Mach-O library is missing: ${library_path}")
        endif()
        execute_process(
            COMMAND "${OTOOL_EXECUTABLE}" -D "${library_path}"
            RESULT_VARIABLE install_name_result
            OUTPUT_VARIABLE install_name_output
            ERROR_VARIABLE install_name_error)
        if(NOT install_name_result EQUAL 0 OR NOT install_name_output MATCHES
           "lib${library_name}\\.${UNIGUI_METADATA_ABI_VERSION}\\.dylib")
            message(FATAL_ERROR
                "Invalid Mach-O install name for ${library_path}: ${install_name_output}${install_name_error}")
        endif()
        string(REPLACE "\n" ";" install_name_lines "${install_name_output}")
        list(FILTER install_name_lines EXCLUDE REGEX "^$")
        list(LENGTH install_name_lines install_name_line_count)
        if(NOT install_name_line_count EQUAL 2)
            message(FATAL_ERROR
                "Unexpected otool -D output for ${library_path}: ${install_name_output}")
        endif()
        list(GET install_name_lines 1 actual_install_name)
        string(STRIP "${actual_install_name}" actual_install_name)
        if(NOT actual_install_name STREQUAL
           "@rpath/lib${library_name}.${UNIGUI_METADATA_ABI_VERSION}.dylib")
            message(FATAL_ERROR
                "Unexpected LC_ID_DYLIB for ${library_path}: ${actual_install_name}")
        endif()
        execute_process(
            COMMAND "${OTOOL_EXECUTABLE}" -L "${library_path}"
            RESULT_VARIABLE versions_result
            OUTPUT_VARIABLE versions_output
            ERROR_VARIABLE versions_error)
        if(NOT versions_result EQUAL 0 OR NOT versions_output MATCHES
           "compatibility version ${UNIGUI_METADATA_ABI_VERSION}\\.0\\.0, current version ${UNIGUI_METADATA_VERSION_CORE}")
            message(FATAL_ERROR
                "Invalid Mach-O compatibility/current version for ${library_path}: ${versions_output}${versions_error}")
        endif()
    endforeach()
elseif(NOT EXPECT_STATIC AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    get_filename_component(PACKAGE_CMAKE_DIR "${PACKAGE_CONFIG_FILE}" DIRECTORY)
    get_filename_component(PACKAGE_CMAKE_PARENT "${PACKAGE_CMAKE_DIR}" DIRECTORY)
    get_filename_component(INSTALL_LIBRARY_DIR "${PACKAGE_CMAKE_PARENT}" DIRECTORY)
    foreach(library_name IN ITEMS UniGUI UniGUI-imgui UniGUI-implot)
        set(versioned_name "${library_name}-${UNIGUI_METADATA_ABI_VERSION}")
        foreach(library_path IN ITEMS
                "${INSTALL_PREFIX}/bin/${versioned_name}.dll"
                "${INSTALL_LIBRARY_DIR}/${versioned_name}.lib")
            if(NOT EXISTS "${library_path}" OR IS_DIRECTORY "${library_path}")
                message(FATAL_ERROR "Installed Windows ABI artifact is missing: ${library_path}")
            endif()
        endforeach()
    endforeach()
endif()

message(STATUS
    "Installed UniGUI ${UNIGUI_METADATA_VERSION} package metadata and documentation are relocatable and verified")
