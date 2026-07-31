add_library(unigui_nlohmann_json INTERFACE)

target_include_directories(unigui_nlohmann_json INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>"
)
