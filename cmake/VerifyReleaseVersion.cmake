if(NOT DEFINED EXPECTED_TAG OR NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "EXPECTED_TAG and SOURCE_DIR are required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/VerifyVersionMetadata.cmake")

if(NOT EXPECTED_TAG STREQUAL UNIGUI_METADATA_TAG)
    message(FATAL_ERROR
        "Release tag ${EXPECTED_TAG} does not match canonical version tag ${UNIGUI_METADATA_TAG}")
endif()

message(STATUS
    "Release tag ${UNIGUI_METADATA_TAG} and repository version metadata are synchronized")
