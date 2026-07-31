if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

function(unigui_require_metadata_file relative_path)
    set(absolute_path "${SOURCE_DIR}/${relative_path}")
    if(NOT EXISTS "${absolute_path}" OR IS_DIRECTORY "${absolute_path}")
        message(FATAL_ERROR "Version metadata file is missing: ${relative_path}")
    endif()
    file(SIZE "${absolute_path}" file_size)
    if(file_size EQUAL 0)
        message(FATAL_ERROR "Version metadata file is empty: ${relative_path}")
    endif()
endfunction()

function(unigui_validate_release_date release_date)
    if(NOT release_date MATCHES
       "^[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
        message(FATAL_ERROR "Invalid release date: ${release_date}")
    endif()
    string(SUBSTRING "${release_date}" 0 4 year_number)
    string(SUBSTRING "${release_date}" 5 2 month_number)
    string(SUBSTRING "${release_date}" 8 2 day_number)
    string(REGEX REPLACE "^0" "" month_number "${month_number}")
    string(REGEX REPLACE "^0" "" day_number "${day_number}")
    if(month_number STREQUAL "")
        set(month_number 0)
    endif()
    if(day_number STREQUAL "")
        set(day_number 0)
    endif()
    if(month_number LESS 1 OR month_number GREATER 12)
        message(FATAL_ERROR "Invalid release month in ${release_date}")
    endif()

    set(maximum_day 31)
    if(month_number EQUAL 4 OR month_number EQUAL 6 OR
       month_number EQUAL 9 OR month_number EQUAL 11)
        set(maximum_day 30)
    elseif(month_number EQUAL 2)
        math(EXPR divisible_by_4 "${year_number} % 4")
        math(EXPR divisible_by_100 "${year_number} % 100")
        math(EXPR divisible_by_400 "${year_number} % 400")
        if(divisible_by_4 EQUAL 0 AND
           (NOT divisible_by_100 EQUAL 0 OR divisible_by_400 EQUAL 0))
            set(maximum_day 29)
        else()
            set(maximum_day 28)
        endif()
    endif()
    if(day_number LESS 1 OR day_number GREATER maximum_day)
        message(FATAL_ERROR "Invalid release day in ${release_date}")
    endif()
endfunction()

set(UNIGUI_METADATA_FILES
    CMakeLists.txt
    README.md
    CHANGELOG.md
    docs/ABI_POLICY.md
    src/CMakeLists.txt
    src/unigui.map.in
    3rdparty/imgui.cmake
    3rdparty/implot.cmake
)
foreach(metadata_file IN LISTS UNIGUI_METADATA_FILES)
    unigui_require_metadata_file("${metadata_file}")
endforeach()

file(STRINGS "${SOURCE_DIR}/CMakeLists.txt" VERSION_DECLARATIONS
    REGEX "^set\\(UNIGUI_VERSION ")
file(STRINGS "${SOURCE_DIR}/CMakeLists.txt" RELEASE_DATE_DECLARATIONS
    REGEX "^set\\(UNIGUI_RELEASE_DATE ")
list(LENGTH VERSION_DECLARATIONS VERSION_DECLARATION_COUNT)
list(LENGTH RELEASE_DATE_DECLARATIONS RELEASE_DATE_DECLARATION_COUNT)
if(NOT VERSION_DECLARATION_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one canonical UNIGUI_VERSION declaration")
endif()
if(NOT RELEASE_DATE_DECLARATION_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one canonical UNIGUI_RELEASE_DATE declaration")
endif()
list(GET VERSION_DECLARATIONS 0 VERSION_DECLARATION)
if(NOT VERSION_DECLARATION MATCHES
   "^set\\(UNIGUI_VERSION \"(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\"\\)$")
    message(FATAL_ERROR
        "Canonical UNIGUI_VERSION must be a final major.minor.patch release")
endif()
set(UNIGUI_METADATA_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(UNIGUI_METADATA_VERSION_MINOR "${CMAKE_MATCH_2}")
set(UNIGUI_METADATA_VERSION_PATCH "${CMAKE_MATCH_3}")
set(UNIGUI_METADATA_VERSION
    "${UNIGUI_METADATA_VERSION_MAJOR}.${UNIGUI_METADATA_VERSION_MINOR}.${UNIGUI_METADATA_VERSION_PATCH}")
set(UNIGUI_METADATA_VERSION_CORE "${UNIGUI_METADATA_VERSION}")
set(UNIGUI_METADATA_ABI_VERSION "${UNIGUI_METADATA_VERSION_MAJOR}")
set(UNIGUI_METADATA_TAG "v${UNIGUI_METADATA_VERSION}")
list(GET RELEASE_DATE_DECLARATIONS 0 RELEASE_DATE_DECLARATION)
if(NOT RELEASE_DATE_DECLARATION MATCHES
   "^set\\(UNIGUI_RELEASE_DATE \"([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])\"\\)$")
    message(FATAL_ERROR "Canonical UNIGUI_RELEASE_DATE is invalid")
endif()
set(UNIGUI_METADATA_RELEASE_DATE "${CMAKE_MATCH_1}")
unigui_validate_release_date("${UNIGUI_METADATA_RELEASE_DATE}")

file(READ "${SOURCE_DIR}/CMakeLists.txt" ROOT_CMAKE_TEXT)
foreach(required_cmake_contract IN ITEMS
        [=[set(UNIGUI_ABI_VERSION "${PROJECT_VERSION_MAJOR}")]=]
        [=[COMPATIBILITY SameMajorVersion]=])
    string(FIND "${ROOT_CMAKE_TEXT}" "${required_cmake_contract}" contract_position)
    if(contract_position EQUAL -1)
        message(FATAL_ERROR
            "CMake version metadata is missing contract: ${required_cmake_contract}")
    endif()
endforeach()

foreach(target_file IN ITEMS src/CMakeLists.txt 3rdparty/imgui.cmake 3rdparty/implot.cmake)
    file(READ "${SOURCE_DIR}/${target_file}" target_text)
    string(FIND "${target_text}"
        [=[SOVERSION "${UNIGUI_ABI_VERSION}"]=] soversion_position)
    if(soversion_position EQUAL -1)
        message(FATAL_ERROR
            "Shared-library target metadata in ${target_file} must use UNIGUI_ABI_VERSION as SOVERSION")
    endif()
endforeach()

file(READ "${SOURCE_DIR}/src/unigui.map.in" VERSION_SCRIPT_TEXT)
string(FIND "${VERSION_SCRIPT_TEXT}"
    [=[UNIGUI_@UNIGUI_ABI_VERSION@ {]=] version_node_position)
if(version_node_position EQUAL -1)
    message(FATAL_ERROR
        "ELF version script must derive its node from UNIGUI_ABI_VERSION")
endif()

file(READ "${SOURCE_DIR}/README.md" README_TEXT)
string(FIND "${README_TEXT}"
    "current version is `${UNIGUI_METADATA_VERSION}`, released on ${UNIGUI_METADATA_RELEASE_DATE}"
    readme_version_position)
if(readme_version_position EQUAL -1)
    message(FATAL_ERROR
        "README.md must identify ${UNIGUI_METADATA_VERSION} and its canonical release date")
endif()

file(READ "${SOURCE_DIR}/docs/ABI_POLICY.md" ABI_TEXT)
foreach(required_abi_contract IN ITEMS
        "Release `${UNIGUI_METADATA_VERSION}`"
        "SOVERSION is `${UNIGUI_METADATA_ABI_VERSION}`"
        "ELF symbol version node is `UNIGUI_${UNIGUI_METADATA_ABI_VERSION}`"
        "SameMajorVersion")
    string(FIND "${ABI_TEXT}" "${required_abi_contract}" abi_contract_position)
    if(abi_contract_position EQUAL -1)
        message(FATAL_ERROR
            "docs/ABI_POLICY.md is missing contract: ${required_abi_contract}")
    endif()
endforeach()

string(REPLACE "." "\\." VERSION_REGEX "${UNIGUI_METADATA_VERSION}")
file(STRINGS "${SOURCE_DIR}/CHANGELOG.md" CHANGELOG_LINES)
set(RELEASE_SECTION_COUNT 0)
set(RELEASE_LINK_COUNT 0)
set(UNRELEASED_LINK_COUNT 0)
set(UNRELEASED_SECTION_COUNT 0)
set(UNRELEASED_FOUND FALSE)
set(UNRELEASED_NEXT_CONTENT "")
set(RELEASE_DATE "")
set(EXPECTED_RELEASE_LINK
    "[${UNIGUI_METADATA_VERSION}]: https://github.com/Uni-Libraries/Uni.GUI/releases/tag/${UNIGUI_METADATA_TAG}")
set(EXPECTED_UNRELEASED_LINK
    "[Unreleased]: https://github.com/Uni-Libraries/Uni.GUI/compare/${UNIGUI_METADATA_TAG}...HEAD")
foreach(CHANGELOG_LINE IN LISTS CHANGELOG_LINES)
    if(CHANGELOG_LINE STREQUAL "## [Unreleased]")
        set(UNRELEASED_FOUND TRUE)
        math(EXPR UNRELEASED_SECTION_COUNT "${UNRELEASED_SECTION_COUNT} + 1")
    elseif(UNRELEASED_FOUND AND UNRELEASED_NEXT_CONTENT STREQUAL "" AND
           NOT CHANGELOG_LINE STREQUAL "")
        set(UNRELEASED_NEXT_CONTENT "${CHANGELOG_LINE}")
    endif()
    if(CHANGELOG_LINE MATCHES
       "^## \\[${VERSION_REGEX}\\] - ([0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9])$")
        math(EXPR RELEASE_SECTION_COUNT "${RELEASE_SECTION_COUNT} + 1")
        set(RELEASE_DATE "${CMAKE_MATCH_1}")
    endif()
    if(CHANGELOG_LINE STREQUAL EXPECTED_RELEASE_LINK)
        math(EXPR RELEASE_LINK_COUNT "${RELEASE_LINK_COUNT} + 1")
    endif()
    if(CHANGELOG_LINE STREQUAL EXPECTED_UNRELEASED_LINK)
        math(EXPR UNRELEASED_LINK_COUNT "${UNRELEASED_LINK_COUNT} + 1")
    endif()
endforeach()

if(NOT UNRELEASED_SECTION_COUNT EQUAL 1 OR
   NOT UNRELEASED_NEXT_CONTENT MATCHES
       "^## \\[${VERSION_REGEX}\\] - [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$")
    message(FATAL_ERROR
        "CHANGELOG.md must have one empty Unreleased section immediately before ${UNIGUI_METADATA_VERSION}")
endif()
if(NOT RELEASE_SECTION_COUNT EQUAL 1)
    message(FATAL_ERROR
        "CHANGELOG.md must contain exactly one release section for ${UNIGUI_METADATA_VERSION}")
endif()
unigui_validate_release_date("${RELEASE_DATE}")
if(NOT RELEASE_DATE STREQUAL UNIGUI_METADATA_RELEASE_DATE)
    message(FATAL_ERROR
        "CHANGELOG.md release date ${RELEASE_DATE} does not match canonical date ${UNIGUI_METADATA_RELEASE_DATE}")
endif()
if(NOT RELEASE_LINK_COUNT EQUAL 1 OR NOT UNRELEASED_LINK_COUNT EQUAL 1)
    message(FATAL_ERROR
        "CHANGELOG.md must contain exactly one canonical release link and one Unreleased comparison link")
endif()

message(STATUS
    "Verified release metadata ${UNIGUI_METADATA_VERSION}, ABI ${UNIGUI_METADATA_ABI_VERSION}, and ${UNIGUI_METADATA_TAG}")
