#
# Application
#

if(WIN32)
  add_executable(uni.gui.example WIN32)
else()
  add_executable(uni.gui.example)
endif()

target_sources(uni.gui.example PRIVATE
  "main.cpp"
  "window_demo.cpp"
)

# includes
target_include_directories(uni.gui.example PRIVATE .)

# dependencies
target_link_libraries(uni.gui.example PRIVATE uni.gui)



#
# Installation
#

install(
  TARGETS uni.gui.example
  RUNTIME DESTINATION bin
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)

if(WIN32)
  if(MSVC)
    install(
      FILES "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:$<TARGET_PDB_FILE:uni.gui.example>>"
      DESTINATION "bin"
    )
  endif()

  install(
    FILES "$<TARGET_RUNTIME_DLLS:uni.gui.example>"
    DESTINATION "bin"
  )

  add_custom_command(TARGET uni.gui.example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t "$<TARGET_FILE_DIR:uni.gui.example>" "$<TARGET_RUNTIME_DLLS:uni.gui.example>"
    COMMAND_EXPAND_LISTS
  )
endif()
