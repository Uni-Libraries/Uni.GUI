if(NOT DEFINED VERSION_FILE OR NOT DEFINED REQUEST_VERSION OR
   NOT DEFINED EXPECTED_VERSION OR NOT DEFINED EXPECT_COMPATIBLE OR
   NOT DEFINED EXPECT_EXACT)
    message(FATAL_ERROR
        "VERSION_FILE, REQUEST_VERSION, EXPECTED_VERSION, EXPECT_COMPATIBLE, and EXPECT_EXACT are required")
endif()
if(NOT EXISTS "${VERSION_FILE}" OR IS_DIRECTORY "${VERSION_FILE}")
    message(FATAL_ERROR "Package version file does not exist: ${VERSION_FILE}")
endif()

string(REPLACE "." ";" REQUEST_COMPONENTS "${REQUEST_VERSION}")
list(LENGTH REQUEST_COMPONENTS REQUEST_COMPONENT_COUNT)
if(REQUEST_COMPONENT_COUNT LESS 1 OR REQUEST_COMPONENT_COUNT GREATER 4)
    message(FATAL_ERROR "Invalid package request version: ${REQUEST_VERSION}")
endif()
foreach(REQUEST_COMPONENT IN LISTS REQUEST_COMPONENTS)
    if(NOT REQUEST_COMPONENT MATCHES "^(0|[1-9][0-9]*)$")
        message(FATAL_ERROR "Invalid package request version: ${REQUEST_VERSION}")
    endif()
endforeach()

set(PACKAGE_FIND_VERSION "${REQUEST_VERSION}")
set(PACKAGE_FIND_VERSION_COUNT "${REQUEST_COMPONENT_COUNT}")
set(component_index 0)
foreach(component_name IN ITEMS MAJOR MINOR PATCH TWEAK)
    if(component_index LESS REQUEST_COMPONENT_COUNT)
        list(GET REQUEST_COMPONENTS "${component_index}" component_value)
    else()
        set(component_value 0)
    endif()
    set("PACKAGE_FIND_VERSION_${component_name}" "${component_value}")
    math(EXPR component_index "${component_index} + 1")
endforeach()
unset(PACKAGE_FIND_VERSION_RANGE)
unset(PACKAGE_VERSION)
unset(PACKAGE_VERSION_COMPATIBLE)
unset(PACKAGE_VERSION_EXACT)
unset(PACKAGE_VERSION_UNSUITABLE)

include("${VERSION_FILE}")

if(NOT PACKAGE_VERSION STREQUAL EXPECTED_VERSION)
    message(FATAL_ERROR
        "Package reports ${PACKAGE_VERSION}; expected ${EXPECTED_VERSION}")
endif()
if(EXPECT_COMPATIBLE)
    if(NOT PACKAGE_VERSION_COMPATIBLE)
        message(FATAL_ERROR
            "Package ${PACKAGE_VERSION} must satisfy request ${REQUEST_VERSION}")
    endif()
elseif(PACKAGE_VERSION_COMPATIBLE)
    message(FATAL_ERROR
        "Package ${PACKAGE_VERSION} must reject request ${REQUEST_VERSION}")
endif()
if(EXPECT_EXACT)
    if(NOT PACKAGE_VERSION_EXACT)
        message(FATAL_ERROR
            "Package ${PACKAGE_VERSION} must exactly match request ${REQUEST_VERSION}")
    endif()
elseif(PACKAGE_VERSION_EXACT)
    message(FATAL_ERROR
        "Package ${PACKAGE_VERSION} unexpectedly exactly matches request ${REQUEST_VERSION}")
endif()

message(STATUS
    "Verified package ${PACKAGE_VERSION} request ${REQUEST_VERSION}: compatible=${EXPECT_COMPATIBLE}, exact=${EXPECT_EXACT}")
