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
  if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
    message(STATUS "Detected gcc compiler")
    set(VOLO_COMPILER "gcc")
  elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
    message(STATUS "Detected clang compiler")
    set(VOLO_COMPILER "clang")
  elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
    message(STATUS "Detected msvc compiler")
    set(VOLO_COMPILER "msvc")
  else()
    message(FATAL_ERROR "Unsupported compiler: '${CMAKE_C_COMPILER_ID}'")
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
  message(STATUS "Configuring gcc compile options")

  # Setup warning flags.
  add_compile_options(-Wall -Wextra -Werror -Wno-override-init -Wgnu-empty-initializer -Wshadow)

  # TODO: Tie these debug options to a configuration knob.
  add_compile_options(-g -fno-omit-frame-pointer)
endmacro(set_gcc_compile_options)

# Set clang specific compile options
macro(set_clang_compile_options)
  message(STATUS "Configuring clang compile options")

  # Setup warning flags.
  add_compile_options(-Wall -Wextra -Werror -Wno-initializer-overrides -Wgnu-empty-initializer -Wshadow)

  # TODO: Tie these debug options to a configuration knob.
  add_compile_options(-g -fno-omit-frame-pointer)

  # Enable various clang sanitizers on supported platforms.
  # TODO: Add a configuration knob to enable / disable sanitizers.
  if(${VOLO_PLATFORM} STREQUAL "linux")
    set(SANITIZERS "address,alignment,builtin,bounds,integer-divide-by-zero")

    message(STATUS "Configuring clang sanitizers: ${SANITIZERS}")
    add_compile_options(-fsanitize=${SANITIZERS})
    add_link_options(-fsanitize=${SANITIZERS})
  endif()

endmacro(set_clang_compile_options)

# Set msvc specific compile options
macro(set_msvc_compile_options)
  message(STATUS "Configuring msvc compile options")

  # Use the c11 standard.
  add_compile_options(/TC /std:c11)

  # Use utf8 for both the source and the executable format.
  add_compile_options(/utf-8)

  # Setup warning flags.
  add_compile_options(/W4 /WX /wd4127 /wd5105 /wd4200 /wd4244 /wd4201)

  # Ignore unused local variable warning,
  # Current MSVC version (19.29.30037) reports allot of false positives on compiler generated
  # temporaries ($SXX variables).
  add_compile_options(/wd4189)

  # Enabling the conformant pre-preprocessor. More info:
  # https://devblogs.microsoft.com/cppblog/announcing-full-support-for-a-c-c-conformant-preprocessor-in-msvc/
  add_compile_options(/Zc:preprocessor)

  # Use syncronous pdb writes, reason is Ninja spawns multiple compiler processes that can end up
  # writing to the same pdb.
  add_compile_options(/FS)
endmacro(set_msvc_compile_options)

# Set compile options
# Requires 'VOLO_COMPILER' to be configured
macro(set_compile_options)

  # Clear the default compiler options.
  set(CMAKE_C_FLAGS_DEBUG "")
  set(CMAKE_C_FLAGS_RELEASE "")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
  set(CMAKE_C_FLAGS_MINSIZEREL "")

  # Enable pthread threading on linux.
  if(${VOLO_PLATFORM} STREQUAL "linux")
    message(STATUS "Configuring pthread threading")
    add_compile_options(-pthread)
    add_link_options(-pthread)
  endif()

  # Set our custom compiler options.
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
