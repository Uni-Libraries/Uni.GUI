foreach(required_argument IN ITEMS ABIDIFF_EXECUTABLE NM_EXECUTABLE READELF_EXECUTABLE
        BASELINE_ABI BASELINE_SYMBOLS CURRENT_LIBRARY)
    if(NOT DEFINED "${required_argument}" OR "${${required_argument}}" STREQUAL "")
        message(FATAL_ERROR "${required_argument} is required")
    endif()
endforeach()
foreach(required_file IN ITEMS BASELINE_ABI BASELINE_SYMBOLS CURRENT_LIBRARY)
    if(NOT EXISTS "${${required_file}}" OR IS_DIRECTORY "${${required_file}}")
        message(FATAL_ERROR "${required_file} does not name a file: ${${required_file}}")
    endif()
endforeach()

execute_process(
    COMMAND "${READELF_EXECUTABLE}" -SW "${CURRENT_LIBRARY}"
    RESULT_VARIABLE debug_result
    OUTPUT_VARIABLE section_output
    ERROR_VARIABLE section_error)
if(NOT debug_result EQUAL 0 OR NOT section_output MATCHES "[.]debug_info")
    message(FATAL_ERROR
        "Current ABI library has no DWARF debug information: ${CURRENT_LIBRARY}\n${section_error}")
endif()

execute_process(
    COMMAND "${ABIDIFF_EXECUTABLE}"
        --no-default-suppression
        --no-added-syms
        "${BASELINE_ABI}"
        "${CURRENT_LIBRARY}"
    RESULT_VARIABLE abi_result
    OUTPUT_VARIABLE abi_output
    ERROR_VARIABLE abi_error)
if(NOT abi_result EQUAL 0)
    message(FATAL_ERROR
        "ABI compatibility check failed for ${CURRENT_LIBRARY}:\n${abi_output}${abi_error}")
endif()

execute_process(
    COMMAND "${NM_EXECUTABLE}" -D --defined-only --with-symbol-versions
        "${CURRENT_LIBRARY}"
    RESULT_VARIABLE nm_result
    OUTPUT_VARIABLE nm_output
    ERROR_VARIABLE nm_error)
if(NOT nm_result EQUAL 0)
    message(FATAL_ERROR "Could not read symbols from ${CURRENT_LIBRARY}: ${nm_error}")
endif()
string(REPLACE "\n" ";" nm_lines "${nm_output}")
set(current_symbols "")
foreach(nm_line IN LISTS nm_lines)
    if(nm_line MATCHES
       "^[0-9A-Fa-f]+[ \t]+([^ \t]+)[ \t]+([^ \t]+)$")
        list(APPEND current_symbols "${CMAKE_MATCH_1} ${CMAKE_MATCH_2}")
    endif()
endforeach()
list(REMOVE_DUPLICATES current_symbols)
list(SORT current_symbols)

file(STRINGS "${BASELINE_SYMBOLS}" baseline_symbols)
foreach(baseline_symbol IN LISTS baseline_symbols)
    if(baseline_symbol STREQUAL "")
        continue()
    endif()
    list(FIND current_symbols "${baseline_symbol}" symbol_index)
    if(symbol_index EQUAL -1)
        message(FATAL_ERROR
            "Released symbol is missing from ${CURRENT_LIBRARY}: ${baseline_symbol}")
    endif()
endforeach()

message(STATUS
    "Verified ABI and released symbol baseline for ${CURRENT_LIBRARY}")
