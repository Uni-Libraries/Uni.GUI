#
# Includes
#

include(GenerateExportHeader)



#
# Application
#

add_library(uni.gui)
target_sources(uni.gui PRIVATE
    "ui_app.cpp"
    "ui_font.cpp"
    "ui_winsys_sdl.cpp"
    "ui_renderer_sdl.cpp"
    "ui_renderer_sdlgpu.cpp"
)

# includes
target_include_directories(uni.gui PUBLIC .)

# dependencies
target_link_libraries(uni.gui PUBLIC imgui)
target_link_libraries(uni.gui PUBLIC implot)
set_target_properties(uni.gui PROPERTIES POSITION_INDEPENDENT_CODE ON)
generate_export_header(uni.gui)
target_include_directories(uni.gui PUBLIC ${CMAKE_CURRENT_BINARY_DIR})



#
# Main
#

add_library(uni.gui.main INTERFACE)
target_sources(uni.gui.main INTERFACE
        "ui_main.cpp"
)
target_link_libraries(uni.gui.main INTERFACE uni.gui)
generate_export_header(uni.gui.main)
target_include_directories(uni.gui.main INTERFACE ${CMAKE_CURRENT_BINARY_DIR})



#
# Installation
#

install(
    TARGETS uni.gui uni.gui.main
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

if(WIN32)
    #if(BUILD_SHARED_LIBS)
    #    install(
    #        FILES "$<TARGET_RUNTIME_DLLS:uni.gui>"
    #        DESTINATION "bin"
    #    )
    #
    #    add_custom_command(TARGET uni.gui POST_BUILD
    #        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:uni.gui> $<TARGET_FILE_DIR:uni.gui>
    #        COMMAND_EXPAND_LISTS
    #    )
    #endif()
endif()
