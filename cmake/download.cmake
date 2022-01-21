# --------------------------------------------------------------------------------------------------
# CMake download utilities.
# --------------------------------------------------------------------------------------------------

#
# Download a set of external files if they do not exist locally yet.
# NOTE: Does not do any change detection; files are only downloaded once.
#
function(download_external_files)
  cmake_parse_arguments(PARSE_ARGV 0 ARG "" "PATH;REMOTE_URL" "FILES")

  foreach(file ${ARG_FILES})
    if(NOT EXISTS ${ARG_PATH}/${file})
      message(STATUS ">> Downloading: ${ARG_REMOTE_URL}/${file}")
      file(DOWNLOAD "${ARG_REMOTE_URL}/${file}" ${ARG_PATH}/${file})
    endif()
  endforeach()
endfunction(download_external_files)
