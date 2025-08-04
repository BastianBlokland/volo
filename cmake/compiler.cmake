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
# Set generic defines
#
macro(set_generic_defines)
  if(${FAST})
    message(STATUS "Enabling fast mode")
    add_definitions(-DVOLO_FAST)
  endif()
  if(${SIMD})
    message(STATUS "Enabling simd mode")
    add_definitions(-DVOLO_SIMD)
  endif()
  if(${TRACE})
    message(STATUS "Enabling trace mode")
    add_definitions(-DVOLO_TRACE)
  endif()
endmacro(set_generic_defines)

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
  set_generic_defines()
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
                      -Wno-override-init-side-effects -Wno-enum-conversion)

  # Optimization settings.
  add_compile_options(-O2) # Optimization level 2.
  # add_compile_options(-march=native) # Optimize for the native cpu architecture (non portable).
  add_compile_options(-fno-strict-aliasing) # Allow aliasing types; use 'restrict' when needed.
  add_compile_options(-fno-stack-protector)
  add_compile_options(-fno-math-errno) # Disable errno setting behavior for math functions.
  add_compile_options(-mf16c) # Enable output of f16c (f32 <-> f16 conversions)
  # add_compile_options(-mfma) # Enable output of 'fused multiply-add' instructions.
  add_compile_options(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0) # Disable fortification.

  if(${VOLO_PLATFORM} STREQUAL "win32")
    add_link_options(-municode) # Entry point with unicode support.
  endif()

  if(NOT ${SIMD})
    message(STATUS "Disabling auto-vectorization")
    add_compile_options(-fno-tree-vectorize)
  endif()

  # Debug options.
  add_compile_options(-g) # Enable debug symbols.
  add_compile_options(-fno-omit-frame-pointer) # Include frame-pointers for fast stack-traces.

  # Link time optimization.
  if(${LTO})
    message(STATUS "Enabling link-time-optimization")
    add_compile_options(-flto -fno-fat-lto-objects)
    add_link_options(-flto -fwhole-program -O2 -mf16c)
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
                      -Wno-implicit-int-conversion -Wno-missing-field-initializers
                      -Wno-enum-enum-conversion)

  # Use the LLD linker (https://lld.llvm.org/).
  add_link_options(-fuse-ld=lld)

  # Optimization settings.
  add_compile_options(-O2) # Optimization level 2.
  # add_compile_options(-march=native) # Optimize for the native cpu architecture (non portable).
  add_compile_options(-fno-strict-aliasing) # Allow aliasing types; use 'restrict' when needed.
  add_compile_options(-fno-stack-protector)
  add_compile_options(-fno-math-errno) # Disable errno setting behavior for math functions.
  add_compile_options(-mf16c) # Enable output of f16c (f32 <-> f16 conversions)
  # add_compile_options(-mfma) # Enable output of 'fused multiply-add' instructions.
  add_compile_options(-fmerge-all-constants)
  add_compile_options(-fcf-protection=none) # Disable 'Control Flow Guard' (CFG).
  add_compile_options(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0) # Disable fortification.

  if(NOT ${SIMD})
    message(STATUS "Disabling auto-vectorization")
    add_compile_options(-fno-vectorize -fno-slp-vectorize -ffp-exception-behavior=maytrap)
  endif()

  # Debug options.
  add_compile_options(-g) # Enable debug symbols.
  add_link_options(-g) # Output debug symbols.
  add_compile_options(-fno-omit-frame-pointer) # Include frame-pointers for fast stack-traces.

  if(${VOLO_PLATFORM} STREQUAL "win32")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded") # Statically link the runtime library.
    add_compile_options(-Xclang -fdefault-calling-conv=vectorcall) # Use the 'vectorcall' call conv.
    add_compile_options(-Wno-microsoft-enum-forward-reference) # Forward declare enum as int.
    add_compile_options(-fms-compatibility-version=0)
    add_link_options(/ENTRY:wmainCRTStartup) # Entry point with unicode support.
    add_link_options(--for-linker=/OPT:REF,ICF=2) # Remove functions and data that are never referenced.
    add_link_options(--for-linker=/GUARD:NO) # Disable 'Control Flow Guard' (CFG).
  endif()

  # Enable various clang sanitizers on supported platforms.
  if(${SANITIZE} AND ${VOLO_PLATFORM} STREQUAL "linux")
    set(SANITIZERS "address,alignment,builtin,bounds,integer-divide-by-zero,float-divide-by-zero,undefined,unreachable")
    set(SANITIZERS_DISABLED "pointer-overflow,shift-base,shift-exponent,function")

    message(STATUS "Configuring clang sanitizers: ${SANITIZERS}")
    add_compile_options(-fsanitize=${SANITIZERS} -fno-sanitize=${SANITIZERS_DISABLED})
    add_link_options(-fsanitize=${SANITIZERS})
    add_definitions(-DVOLO_ASAN)
  endif()

  # Link time optimization.
  if(${LTO})
    message(STATUS "Enabling link-time-optimization")
    add_compile_options(-flto=full)
    add_link_options(-flto=full -O2 -mf16c)
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
                      /wd4324 /wd4100 /wd4703 /wd4152 /wd5286 /wd5287)

  # Ignore unused local variable warning,
  # Current MSVC version (19.29.30037) reports false positives on compiler generated vars ($SXX).
  add_compile_options(/wd4189)

  # Enabling the conformant c-preprocessor. More info:
  # https://devblogs.microsoft.com/cppblog/announcing-full-support-for-a-c-c-conformant-preprocessor-in-msvc/
  add_compile_options(/Zc:preprocessor)

  # Use synchronous pdb writes, reason is Ninja spawns multiple compiler processes that can end up
  # writing to the same pdb.
  add_compile_options(/FS)

  # Optimization settings.
  add_compile_options(/O2) # Optimization level 2.
  add_compile_options(/Oi) # Enable intrinsic functions.
  add_compile_options(/Gv) # Use the 'vectorcall' calling convention.
  add_compile_options(/GS-) # Disable 'Buffer Security Check'.
  add_compile_options(/guard:cf-) # Disable 'Control Flow Guard' (CFG).

  if(NOT ${SIMD})
    message(STATUS "Disabling auto-vectorization")
    add_compile_options(/d2Qvec-)
  endif()

  # Debug options.
  add_compile_options(/Zi) # Debug symbols in separate pdb files.

  # Statically link the runtime library.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

  # Linker options.
  add_link_options(/ENTRY:wmainCRTStartup) # Entry point with unicode support.
  add_link_options(/INCREMENTAL:NO) # No incremental linking.
  add_link_options(/OPT:REF,ICF=2) # Remove functions and data that are never referenced.
  add_link_options(/GUARD:NO) # Disable 'Control Flow Guard' (CFG).

  # Link time optimization.
  if(${LTO})
    message(STATUS "Enabling link-time-optimization")
    add_compile_options(/GL)
    add_link_options(/LTCG)
  endif()

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
  set(CMAKE_STATIC_LIBRARY_PREFIX_C "") # Manually prefix libraries for clarity.

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
