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
target_link_libraries(uni.gui.demo PRIVATE uni.gui)



#
# Installation
#

install(
  TARGETS uni.gui.demo
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

if(WIN32)
  if(MSVC)
    install(
      FILES "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:$<TARGET_PDB_FILE:uni.gui.demo>>"
      DESTINATION "bin"
    )
  endif()

  install(
    FILES "$<TARGET_RUNTIME_DLLS:uni.gui.demo>"
    DESTINATION "bin"
  )

  add_custom_command(TARGET uni.gui.demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:uni.gui.demo>" "$<TARGET_RUNTIME_DLLS:uni.gui.demo>"
    COMMAND_EXPAND_LISTS
  )
endif()
