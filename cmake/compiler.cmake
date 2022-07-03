# --------------------------------------------------------------------------------------------------
# CMake compiler utilities.
# --------------------------------------------------------------------------------------------------

#
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

#
# Set gcc specific defines
#
macro(set_gcc_defines)
  add_definitions(-DVOLO_GCC)
endmacro(set_gcc_defines)

#
# Set clang specific defines
#
macro(set_clang_defines)
  add_definitions(-DVOLO_CLANG)
endmacro(set_clang_defines)

#
# Set msvc specific defines
#
macro(set_msvc_defines)
  add_definitions(-DVOLO_MSVC)
endmacro(set_msvc_defines)

#
# Set compiler specific defines
# Requires 'VOLO_COMPILER' to be configured
#
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

#
# Set gcc specific compile options
#
macro(set_gcc_compile_options)
  message(STATUS "Configuring gcc compile options")

  # Setup warning flags.
  add_compile_options(-Wall -Wextra -Werror -Wshadow)
  add_compile_options(-Wno-missing-field-initializers -Wno-override-init -Wno-implicit-fallthrough
                      -Wno-clobbered -Wno-missing-braces -Wno-type-limits -Wno-maybe-uninitialized
                      -Wno-override-init-side-effects)

  # Disable strict aliasing as its a bit dangerous (TODO: Investigate the perf impact).
  add_compile_options(-fno-strict-aliasing)

  # Optimization settings.
  add_compile_options(-O3)
  # add_compile_options(-ffast-math) # Enable (potentially lossy) floating point optimizations.
  add_compile_options(-mf16c) # Enable output of f16c (f32 <-> f16 converions)
  add_compile_options(-mfma) # Enable output of 'fused multiply-add' instructions.

  # Debug options.
  add_compile_options(-g)
  if(NOT ${VOLO_PLATFORM} STREQUAL "win32")
    # NOTE: The MinGW GCC port fails code-gen with the 'no-omit-frame-pointer' option.
    # Open issue: https://github.com/msys2/MINGW-packages/issues/4409
    add_compile_options(-fno-omit-frame-pointer)
  endif()

endmacro(set_gcc_compile_options)

#
# Set clang specific compile options
#
macro(set_clang_compile_options)
  message(STATUS "Configuring clang compile options")

  # Setup warning flags.
  add_compile_options(-Wall -Wextra -Werror -Wshadow -Wgnu-empty-initializer -Wconversion)
  add_compile_options(-Wno-initializer-overrides -Wno-unused-value -Wno-missing-braces
                      -Wno-sign-conversion -Wno-implicit-int-float-conversion
                      -Wno-implicit-int-conversion -Wno-missing-field-initializers)

  # Disable strict aliasing as its a bit dangerous (TODO: Investigate the perf impact).
  add_compile_options(-fno-strict-aliasing)

  # Optimization settings.
  add_compile_options(-O3)
  # add_compile_options(-ffast-math) # Enable (potentially lossy) floating point optimizations.
  add_compile_options(-mf16c) # Enable output of f16c (f32 <-> f16 converions)
  add_compile_options(-mfma) # Enable output of 'fused multiply-add' instructions.
  # add_compile_options(-flto=full) # Enable link-time optimization.
  # add_link_options(-fuse-ld=lld -flto=full) # Enable link-time optimization.

  # Debug options.
  add_compile_options(-g -fno-omit-frame-pointer)

  # Enable various clang sanitizers on supported platforms.
  if(${SANITIZE} AND ${VOLO_PLATFORM} STREQUAL "linux")
    set(SANITIZERS "address,alignment,builtin,bounds,integer-divide-by-zero")

    message(STATUS "Configuring clang sanitizers: ${SANITIZERS}")
    add_compile_options(-fsanitize=${SANITIZERS})
    add_link_options(-fsanitize=${SANITIZERS})
    add_definitions(-DVOLO_ASAN)
  endif()

endmacro(set_clang_compile_options)

#
# Set msvc specific compile options
#
macro(set_msvc_compile_options)
  message(STATUS "Configuring msvc compile options")

  # Use the c11 standard.
  add_compile_options(/TC /std:c11)

  # Use utf8 for both the source and the executable format.
  add_compile_options(/utf-8)

  # Setup warning flags.
  add_compile_options(/W4 /WX /wd4127 /wd5105 /wd4200 /wd4244 /wd4201 /wd4210 /wd4701 /wd4706
                      /wd4324 /wd4100 /wd4703)

  # Ignore unused local variable warning,
  # Current MSVC version (19.29.30037) reports allot of false positives on compiler generated
  # temporaries ($SXX variables).
  add_compile_options(/wd4189)

  # Enabling the conformant c-preprocessor. More info:
  # https://devblogs.microsoft.com/cppblog/announcing-full-support-for-a-c-c-conformant-preprocessor-in-msvc/
  add_compile_options(/Zc:preprocessor)

  # Use syncronous pdb writes, reason is Ninja spawns multiple compiler processes that can end up
  # writing to the same pdb.
  add_compile_options(/FS)

  # Optimization settings.
  add_compile_options(/O2)
  # add_compile_options(/fp:fast)  # Enable (potentially lossy) floating point optimizations.
  add_compile_options(/GS-) # Disable 'Buffer Security Check'.
  # add_compile_options(/GL) # Enable link-time optimization.

  # Debug options.
  add_compile_options(/Zi)

  # Statically link the runtime library.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

endmacro(set_msvc_compile_options)

#
# Set compile options
# Requires 'VOLO_COMPILER' to be configured
#
macro(set_compile_options)

  # Clear the default compiler options.
  set(CMAKE_C_FLAGS "")
  set(CMAKE_C_FLAGS_DEBUG "")
  set(CMAKE_C_FLAGS_RELEASE "")
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "")
  set(CMAKE_C_FLAGS_MINSIZEREL "")

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
