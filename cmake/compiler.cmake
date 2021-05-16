# --------------------------------------------------------------------------------------------------
# CMake compiler utilities.
# --------------------------------------------------------------------------------------------------

# Detect the current compiler
# Sets 'VOLO_COMPILER' to either:
# * gcc
# * clang
# * msvc
#
macro(detect_compiler)
  if(${CMAKE_C_COMPILER_ID} STREQUAL "GNU")
    message(STATUS "Detected gcc compiler")
    set(VOLO_COMPILER "gcc")
  elseif(${CMAKE_C_COMPILER_ID} STREQUAL "Clang")
    message(STATUS "Detected clang compiler")
    set(VOLO_COMPILER "clang")
  elseif(${CMAKE_C_COMPILER_ID} STREQUAL "MSVC")
    message(STATUS "Detected msvc compiler")
    set(VOLO_COMPILER "msvc")
  else()
    message(FATAL_ERROR "Unsupported compiler")
  endif()
endmacro(detect_compiler)

# Set gcc specific defines
macro(set_gcc_defines)
  add_definitions(-DVOLO_GCC)
endmacro(set_gcc_defines)

# Set clang specific defines
macro(set_clang_defines)
  add_definitions(-DVOLO_CLANG)
endmacro(set_clang_defines)

# Set msvc specific defines
macro(set_msvc_defines)
  add_definitions(-DVOLO_MSVC)
endmacro(set_msvc_defines)

# Set compiler specific defines
# Requires 'VOLO_COMPILER' to be configured
macro(set_compiler_defines)
  if(${VOLO_COMPILER} STREQUAL "gcc")
    set_gcc_defines()
  elseif(${VOLO_COMPILER} STREQUAL "clang")
    set_clang_defines()
  elseif(${VOLO_COMPILER} STREQUAL "msvc")
    set_msvc_defines()
  else()
    message(FATAL_ERROR "Unknown compiler")
  endif()
endmacro(set_compiler_defines)

# Set gcc specific compile options
macro(set_gcc_compile_options)
  add_compile_options(-Wall -Wextra -Werror)
  # TODO: Tie these debug options to a configuration knob.
  add_compile_options(-g -fno-omit-frame-pointer)
endmacro(set_gcc_compile_options)

# Set clang specific compile options
macro(set_clang_compile_options)
  add_compile_options(-Wall -Wextra -Werror)
  # TODO: Tie these debug options to a configuration knob.
  add_compile_options(-g -fno-omit-frame-pointer)
endmacro(set_clang_compile_options)

# Set msvc specific compile options
macro(set_msvc_compile_options)
  add_definitions(/W4 /WX)
  # TODO: Tie these debug options to a configuration knob.
  add_compile_options(/Z7)
endmacro(set_msvc_compile_options)

# Set compile options
# Requires 'VOLO_COMPILER' to be configured
macro(set_compile_options)
  if(${VOLO_COMPILER} STREQUAL "gcc")
    set_gcc_compile_options()
  elseif(${VOLO_COMPILER} STREQUAL "clang")
    set_clang_compile_options()
  elseif(${VOLO_COMPILER} STREQUAL "msvc")
    set_msvc_compile_options()
  else()
    message(FATAL_ERROR "Unknown compiler")
  endif()
endmacro(set_compile_options)
