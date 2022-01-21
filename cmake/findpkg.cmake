# --------------------------------------------------------------------------------------------------
# Helpers to find libraries using 'pkg-config'.
# More info: https://linux.die.net/man/1/pkg-config
# --------------------------------------------------------------------------------------------------

find_package(PkgConfig)
include(FindPackageHandleStandardArgs)

#
# Helper function to find a system library using pkg-config.
#
function(findpkg)
  cmake_parse_arguments(PARSE_ARGV 0 ARG "" "LIB;HEADER" "")

  message(STATUS "findpkg: Finding system package (lib: ${ARG_LIB}, header: ${ARG_HEADER})")

  pkg_check_modules(PC_${ARG_LIB} QUIET ${ARG_LIB})

  find_path(${ARG_LIB}_INCLUDE_DIR NAMES ${ARG_HEADER}
    HINTS
    ${PC_${ARG_LIB}_INCLUDEDIR}
    ${PC_${ARG_LIB}_INCLUDE_DIRS}
    )

  find_library(${ARG_LIB}_LIBRARY NAMES ${ARG_LIB}
    HINTS
    ${PC_${ARG_LIB}_LIBDIR}
    ${PC_${ARG_LIB}_LIBRARY_DIRS}
    )

  find_package_handle_standard_args(${ARG_LIB}
    FOUND_VAR ${ARG_LIB}_FOUND
    REQUIRED_VARS ${ARG_LIB}_INCLUDE_DIR ${ARG_LIB}_LIBRARY
    )

  mark_as_advanced(${ARG_LIB}_INCLUDE_DIR ${ARG_LIB}_LIBRARY)

endfunction(findpkg)
