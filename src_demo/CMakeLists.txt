#
# Application
#

if(WIN32)
  add_executable(uni.gui.demo WIN32)
else()
  add_executable(uni.gui.demo)
endif()

target_sources(uni.gui.demo PRIVATE
  "main.cpp"
  "window_demo.cpp"
)

# includes
target_include_directories(uni.gui.demo PRIVATE .)

# dependencies
target_link_libraries(uni.gui.demo PRIVATE UniGUI::Main)
target_compile_features(uni.gui.demo PRIVATE cxx_std_23)
target_compile_definitions(uni.gui.demo PRIVATE
  "UNIGUI_DEMO_TITLE=\"Uni.GUI Example v${UNIGUI_VERSION}\""
)

if(APPLE)
  set_property(TARGET uni.gui.demo PROPERTY INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
  set_property(TARGET uni.gui.demo PROPERTY INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()



#
# Installation
#

if(UNIGUI_INSTALL)
  install(
    TARGETS uni.gui.demo
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  )

  if(WIN32)
    if(MSVC)
      install(
        FILES "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:$<TARGET_PDB_FILE:uni.gui.demo>>"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
      )
    endif()

    install(
      FILES "$<TARGET_RUNTIME_DLLS:uni.gui.demo>"
      DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )

    add_custom_command(TARGET uni.gui.demo POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:uni.gui.demo>" "$<TARGET_RUNTIME_DLLS:uni.gui.demo>"
      COMMAND_EXPAND_LISTS
    )
  endif()
endif()
