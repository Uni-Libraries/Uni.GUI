if(NOT DEFINED ARCHIVE_DIRECTORY OR NOT DEFINED ARCHIVE_EXTENSION OR
   NOT DEFINED EXTRACT_DIR OR NOT DEFINED SOURCE_DIR OR NOT DEFINED BUILD_DIR OR
   NOT DEFINED REFERENCE_INSTALL_PREFIX OR NOT DEFINED EXPECTED_PLATFORM_SUFFIX OR
   NOT DEFINED EXPECT_STATIC)
    message(FATAL_ERROR
        "ARCHIVE_DIRECTORY, ARCHIVE_EXTENSION, EXTRACT_DIR, SOURCE_DIR, BUILD_DIR, REFERENCE_INSTALL_PREFIX, EXPECTED_PLATFORM_SUFFIX, and EXPECT_STATIC are required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/VerifyVersionMetadata.cmake")

set(package_archive
    "${ARCHIVE_DIRECTORY}/UniGUI-${UNIGUI_METADATA_VERSION}-${EXPECTED_PLATFORM_SUFFIX}.${ARCHIVE_EXTENSION}")
if(NOT EXISTS "${package_archive}" OR IS_DIRECTORY "${package_archive}")
    message(FATAL_ERROR
        "Expected platform archive does not exist: ${package_archive}")
endif()

file(REMOVE_RECURSE "${EXTRACT_DIR}")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar xvf "${package_archive}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_output
    ERROR_VARIABLE extract_error
)
if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR
        "Could not extract ${package_archive}:\n${extract_output}${extract_error}")
endif()

file(GLOB extracted_entries LIST_DIRECTORIES TRUE "${EXTRACT_DIR}/*")
list(LENGTH extracted_entries extracted_entry_count)
if(NOT extracted_entry_count EQUAL 1)
    message(FATAL_ERROR
        "Binary archive must contain exactly one top-level entry: ${extracted_entries}")
endif()
list(GET extracted_entries 0 package_root)
if(NOT IS_DIRECTORY "${package_root}")
    message(FATAL_ERROR "Binary archive top-level entry is not a directory: ${package_root}")
endif()

function(unigui_collect_package_tree root output_variable)
    file(GLOB_RECURSE tree_entries
        LIST_DIRECTORIES FALSE
        RELATIVE "${root}"
        "${root}/*")
    list(SORT tree_entries)
    set("${output_variable}" "${tree_entries}" PARENT_SCOPE)
endfunction()

unigui_collect_package_tree("${REFERENCE_INSTALL_PREFIX}" reference_tree)
unigui_collect_package_tree("${package_root}" archive_tree)
if(NOT archive_tree STREQUAL reference_tree)
    message(FATAL_ERROR
        "Binary archive file list differs from the validated staging install\n"
        "Staging: ${reference_tree}\nArchive: ${archive_tree}")
endif()

foreach(relative_path IN LISTS reference_tree)
    set(reference_path "${REFERENCE_INSTALL_PREFIX}/${relative_path}")
    set(archive_path "${package_root}/${relative_path}")
    if(IS_SYMLINK "${reference_path}" OR IS_SYMLINK "${archive_path}")
        if(NOT IS_SYMLINK "${reference_path}" OR NOT IS_SYMLINK "${archive_path}")
            message(FATAL_ERROR "Binary archive changed symlink type: ${relative_path}")
        endif()
        file(READ_SYMLINK "${reference_path}" reference_link)
        file(READ_SYMLINK "${archive_path}" archive_link)
        if(NOT archive_link STREQUAL reference_link)
            message(FATAL_ERROR
                "Binary archive changed symlink ${relative_path}: ${reference_link} -> ${archive_link}")
        endif()
    else()
        file(SHA256 "${reference_path}" reference_hash)
        file(SHA256 "${archive_path}" archive_hash)
        if(NOT archive_hash STREQUAL reference_hash)
            message(FATAL_ERROR "Binary archive changed file content: ${relative_path}")
        endif()
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINSTALL_PREFIX=${package_root}"
        "-DSOURCE_DIR=${SOURCE_DIR}"
        "-DBUILD_DIR=${BUILD_DIR}"
        "-DEXPECT_STATIC=${EXPECT_STATIC}"
        -P "${CMAKE_CURRENT_LIST_DIR}/ValidateInstalledPackage.cmake"
    RESULT_VARIABLE validation_result
    OUTPUT_VARIABLE validation_output
    ERROR_VARIABLE validation_error
)
if(NOT validation_result EQUAL 0)
    message(FATAL_ERROR
        "Binary archive validation failed:\n${validation_output}${validation_error}")
endif()

list(LENGTH archive_tree archive_file_count)
message(STATUS
    "Validated exact binary archive ${package_archive} (${archive_file_count} staged files)")
