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

#
# Configure a target that consists of remote files to be copied.
# Note: for license info see: https://www.bastian.tech/assets/license.txt
#
function(configure_copies_external target)
  message(STATUS "> Configuring external copies: ${target}")

  set(remoteUrl "https://www.bastian.tech/assets")
  set(cacheDir "${CMAKE_SOURCE_DIR}/.cache/external")

  foreach(filePath IN LISTS ARGN)
    set(cacheFilePath ${cacheDir}/${filePath})
    if(NOT EXISTS ${cacheFilePath})
      message(STATUS ">> Downloading: ${remoteUrl}/${filePath}")
      file(DOWNLOAD "${remoteUrl}/${filePath}" ${cacheFilePath})
    endif()

    configure_file(${cacheFilePath} ${CMAKE_CURRENT_BINARY_DIR}/${filePath} COPYONLY)
    list(APPEND artifacts ${CMAKE_CURRENT_BINARY_DIR}/${filePath})
  endforeach()

  add_custom_target(${target} DEPENDS ${artifacts})
endfunction(configure_copies_external)
