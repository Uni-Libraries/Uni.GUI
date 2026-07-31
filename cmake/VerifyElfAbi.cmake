if(NOT DEFINED CORE_LIBRARY OR NOT DEFINED IMGUI_LIBRARY OR
   NOT DEFINED IMPLOT_LIBRARY OR NOT DEFINED EXPECTED_ABI_VERSION)
    message(FATAL_ERROR
        "CORE_LIBRARY, IMGUI_LIBRARY, IMPLOT_LIBRARY, and EXPECTED_ABI_VERSION are required")
endif()
if(NOT EXPECTED_ABI_VERSION MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "EXPECTED_ABI_VERSION must be a non-zero major version: ${EXPECTED_ABI_VERSION}")
endif()

if(NOT DEFINED READELF_EXECUTABLE OR READELF_EXECUTABLE STREQUAL "")
    find_program(READELF_EXECUTABLE NAMES readelf llvm-readelf REQUIRED)
elseif(NOT EXISTS "${READELF_EXECUTABLE}")
    message(FATAL_ERROR "READELF_EXECUTABLE does not exist: ${READELF_EXECUTABLE}")
endif()

function(unigui_verify_elf_soname library expected_soname)
    if(NOT EXISTS "${library}" OR IS_DIRECTORY "${library}")
        message(FATAL_ERROR "ELF shared library is missing: ${library}")
    endif()
    execute_process(
        COMMAND "${READELF_EXECUTABLE}" -d "${library}"
        RESULT_VARIABLE readelf_result
        OUTPUT_VARIABLE dynamic_section
        ERROR_VARIABLE readelf_error
    )
    if(NOT readelf_result EQUAL 0)
        message(FATAL_ERROR
            "Could not inspect ${library}: ${readelf_error}")
    endif()
    string(REPLACE "." "\\." soname_regex "${expected_soname}")
    if(NOT dynamic_section MATCHES "\\(SONAME\\).*\\[${soname_regex}\\]")
        message(FATAL_ERROR
            "${library} does not declare expected SONAME ${expected_soname}")
    endif()
endfunction()

unigui_verify_elf_soname(
    "${CORE_LIBRARY}" "libUniGUI.so.${EXPECTED_ABI_VERSION}")
unigui_verify_elf_soname(
    "${IMGUI_LIBRARY}" "libUniGUI-imgui.so.${EXPECTED_ABI_VERSION}")
unigui_verify_elf_soname(
    "${IMPLOT_LIBRARY}" "libUniGUI-implot.so.${EXPECTED_ABI_VERSION}")

execute_process(
    COMMAND "${READELF_EXECUTABLE}" --version-info "${CORE_LIBRARY}"
    RESULT_VARIABLE version_info_result
    OUTPUT_VARIABLE version_info
    ERROR_VARIABLE version_info_error
)
if(NOT version_info_result EQUAL 0)
    message(FATAL_ERROR
        "Could not inspect ELF symbol versions in ${CORE_LIBRARY}: ${version_info_error}")
endif()
if(NOT version_info MATCHES
   "Name: UNIGUI_${EXPECTED_ABI_VERSION}([^A-Za-z0-9_.]|$)")
    message(FATAL_ERROR
        "${CORE_LIBRARY} does not define exact ELF symbol version node UNIGUI_${EXPECTED_ABI_VERSION}")
endif()
string(REGEX MATCHALL "Name: UNIGUI_[0-9]+(\\.[0-9]+)?" unigui_version_nodes
    "${version_info}")
list(REMOVE_DUPLICATES unigui_version_nodes)
foreach(unigui_version_node IN LISTS unigui_version_nodes)
    if(NOT unigui_version_node STREQUAL
       "Name: UNIGUI_${EXPECTED_ABI_VERSION}")
        message(FATAL_ERROR
            "${CORE_LIBRARY} contains incompatible ELF version node ${unigui_version_node}")
    endif()
endforeach()

message(STATUS
    "Verified ELF SONAME ${EXPECTED_ABI_VERSION} for UniGUI/imgui/implot and core node UNIGUI_${EXPECTED_ABI_VERSION}")
