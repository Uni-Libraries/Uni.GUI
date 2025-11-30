#
# Application
#

add_library(uni.gui)

target_sources(uni.gui PRIVATE
    "ui_app.cpp"
    "ui_font.cpp"
    "ui_std.cpp"
    "ui_main.cpp"
    "ui_winsys_sdl.cpp"
    "ui_renderer_sdl.cpp"
)

# includes
target_include_directories(uni.gui PUBLIC .)

# dependencies
target_link_libraries(uni.gui PUBLIC imgui)
target_link_libraries(uni.gui PUBLIC implot)
set_target_properties(uni.gui PROPERTIES POSITION_INDEPENDENT_CODE ON)

#
# Installation
#

install(
    TARGETS uni.gui
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
