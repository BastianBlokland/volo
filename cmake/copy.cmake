# --------------------------------------------------------------------------------------------------
# CMake copy utilities.
# --------------------------------------------------------------------------------------------------

#
# Configure a target that consists of files to be copied.
#
function(configure_copies target)
  message(STATUS "> Configuring copies: ${target}")

  foreach(filePath IN LISTS ARGN)
    configure_file(${filePath} ${CMAKE_CURRENT_BINARY_DIR}/${filePath} COPYONLY)
    list(APPEND artifacts ${CMAKE_CURRENT_BINARY_DIR}/${filePath})
  endforeach()

  add_custom_target(${target} DEPENDS ${artifacts})
endfunction(configure_copies)
