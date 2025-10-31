# --------------------------------------------------------------------------------------------------
# CMake compiler utilities.
# --------------------------------------------------------------------------------------------------

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Generate a 'compile_commands.json' for intellisense.

# Clear default compiler flags.
set(CMAKE_C_FLAGS "" CACHE STRING "Compiler flags" FORCE)
set(CMAKE_C_FLAGS_DEBUG "" CACHE STRING  "Compiler flags" FORCE)
set(CMAKE_C_FLAGS_RELEASE "" CACHE STRING "Compiler flags" FORCE)

# Clear default executable linker flags.
set(CMAKE_EXE_LINKER_FLAGS "" CACHE STRING "Executable linker flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "" CACHE STRING "Executable linker flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "" CACHE STRING "Executable linker flags" FORCE)

# Clear default library linker flags.
set(CMAKE_STATIC_LINKER_FLAGS "" CACHE STRING "Library linker flags" FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "" CACHE STRING "Library linker flags" FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "" CACHE STRING "Library linker flags" FORCE)

# --------------------------------------------------------------------------------------------------
# Options setup.
# --------------------------------------------------------------------------------------------------

add_compile_definitions(
  $<$<CONFIG:Release>:VOLO_RELEASE>
  $<$<BOOL:${VOLO_SIMD}>:VOLO_SIMD>
  $<$<BOOL:${VOLO_TRACE}>:VOLO_TRACE>
  )

# --------------------------------------------------------------------------------------------------
# Platform setup.
# --------------------------------------------------------------------------------------------------

if(UNIX AND NOT APPLE)
  set(VOLO_PLATFORM "linux")
  add_compile_definitions(
    VOLO_LINUX
    _GNU_SOURCE # Enable GNU extensions.
    NDEBUG # Disable lib-c assertions (our own assertions are independent of this).
    )
  add_compile_options(
    -pthread # Enable pthread threading.
    )
  add_link_options(
    -pthread # Enable pthread threading.
    )
elseif(WIN32)
  set(VOLO_PLATFORM "win32")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded") # Statically link the runtime library.
  add_compile_definitions(
    VOLO_WIN32
    WINVER=0x0603 _WIN32_WINNT=0x0603 # Target windows '8.1'.
    WIN32_LEAN_AND_MEAN # Use a subset of the windows header.
    NOMINMAX # Avoid the windows header defining the min / max macros.
    UNICODE # Enable unicode support.
    NDEBUG # Disable lib-c assertions (our own assertions are independent of this).
  )
else()
  message(FATAL_ERROR "Unsupported platform")
endif()

# --------------------------------------------------------------------------------------------------
# Compiler setup.
# --------------------------------------------------------------------------------------------------

if("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
  set(VOLO_COMPILER "gcc")
  add_compile_definitions(VOLO_GCC)
  add_compile_options(
    -std=gnu11
    $<$<BOOL:${VOLO_WERROR}>:-Werror> # Warnings as errors.
    -Wall -Wextra  -Wshadow

    -Wno-missing-field-initializers -Wno-override-init -Wno-implicit-fallthrough
    -Wno-clobbered -Wno-missing-braces -Wno-type-limits -Wno-maybe-uninitialized
    -Wno-override-init-side-effects -Wno-enum-conversion

    $<$<CONFIG:Debug>:-O1> # Optimization level 1 in Debug.
    $<$<CONFIG:Release>:-O3> # Optimization level 3 in Release.
    -g # Enable debug symbols.
    -fno-omit-frame-pointer # Include frame-pointers for fast stack-traces.
    -fno-strict-aliasing # Allow aliasing types; use 'restrict' when needed.
    -fno-stack-protector
    -fno-math-errno # Disable errno setting behavior for math functions.
    -mf16c # Enable output of f16c (f32 <-> f16 conversions).
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 # Disable fortification.
    $<$<NOT:$<BOOL:${VOLO_SIMD}>>:-fno-tree-vectorize> # No vectorization when SIMD is disabled.
    $<$<BOOL:${VOLO_LTO}>:-flto> # Link time optimization.
    $<$<BOOL:${VOLO_LTO}>:-fno-fat-lto-objects>
    $<$<BOOL:${WIN32}>:-Wa,-muse-unaligned-vector-move> # To support Win32 SEH, see: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412
    )
  add_link_options(
    -g # Enable debug symbols.
    $<$<NOT:$<BOOL:${WIN32}>>:-no-pie> # Disable 'Position Independent Executables'.
    $<$<BOOL:${WIN32}>:-municode> # Entry point with unicode support on windows.

    $<$<BOOL:${VOLO_LTO}>:-flto> # Link time optimization.
    $<$<BOOL:${VOLO_LTO}>:-fwhole-program> # Link time optimization.
    $<$<BOOL:${VOLO_LTO}>:-O2> # Optimization level 2.
    $<$<BOOL:${VOLO_LTO}>:-mf16c> # Enable output of f16c (f32 <-> f16 conversions).
    )
elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
  set(VOLO_COMPILER "clang")
  set(SANITIZERS "address,alignment,builtin,bounds,integer-divide-by-zero,float-divide-by-zero,undefined,unreachable")
  set(SANITIZERS_DISABLED "pointer-overflow,shift-base,shift-exponent,function")

  add_compile_definitions(
    VOLO_CLANG
    $<$<BOOL:${VOLO_SANITIZE}>:VOLO_ASAN>
    )
  add_compile_options(
    -std=gnu11
    $<$<BOOL:${VOLO_WERROR}>:-Werror> # Warnings as errors.
    -Wall -Wextra -Wshadow -Wgnu-empty-initializer -Wconversion

    -Wno-initializer-overrides -Wno-unused-value -Wno-missing-braces
    -Wno-sign-conversion -Wno-implicit-int-float-conversion -Wno-implicit-int-conversion
    -Wno-missing-field-initializers -Wno-enum-enum-conversion

    $<$<CONFIG:Debug>:-O1> # Optimization level 1 in Debug.
    $<$<CONFIG:Release>:-O3> # Optimization level 3 in Release.
    -g # Enable debug symbols.
    -fno-omit-frame-pointer # Include frame-pointers for fast stack-traces.
    -fno-strict-aliasing # Allow aliasing types; use 'restrict' when needed.
    -fno-stack-protector
    -fno-math-errno # Disable errno setting behavior for math functions.
    -mf16c # Enable output of f16c (f32 <-> f16 conversions).
    -fmerge-all-constants
    -fcf-protection=none # Disable 'Control Flow Guard' (CFG).
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 # Disable fortification.

    $<$<NOT:$<BOOL:${VOLO_SIMD}>>:-fno-vectorize> # No vectorization when SIMD is disabled.
    $<$<NOT:$<BOOL:${VOLO_SIMD}>>:-fno-slp-vectorize> # No vectorization when SIMD is disabled.
    $<$<NOT:$<BOOL:${VOLO_SIMD}>>:-ffp-exception-behavior=maytrap>

    $<$<BOOL:${VOLO_LTO}>:-flto=full> # Link time optimization.

    $<$<BOOL:${VOLO_SANITIZE}>:-fsanitize=${SANITIZERS}> # Enable supported sanitizers.
    $<$<BOOL:${VOLO_SANITIZE}>:-fno-sanitize=${SANITIZERS_DISABLED}> # Disable unsupported sanitizers.

    $<$<BOOL:${WIN32}>:-Xclang=-fdefault-calling-conv=vectorcall> # Use the 'vectorcall' call conv.
    $<$<BOOL:${WIN32}>:-Wno-microsoft-enum-forward-reference> # Forward declare enum as int.
    $<$<BOOL:${WIN32}>:-fms-compatibility-version=0>
    )
  add_link_options(
    -fuse-ld=lld # Use the LLD linker (https://lld.llvm.org/).
    -g # Enable debug symbols.
    $<$<NOT:$<BOOL:${WIN32}>>:-no-pie> # Disable 'Position Independent Executables'.

    $<$<BOOL:${VOLO_LTO}>:-flto=full> # Link time optimization.
    $<$<BOOL:${VOLO_LTO}>:-O2> # Optimization level 2.
    $<$<BOOL:${VOLO_LTO}>:-mf16c> # Enable output of f16c (f32 <-> f16 conversions).

    $<$<BOOL:${VOLO_SANITIZE}>:-fsanitize=${SANITIZERS}> # Enable supported sanitizers.

    $<$<BOOL:${WIN32}>:--for-linker=/ENTRY:wmainCRTStartup> # Entry point with unicode support.
    $<$<BOOL:${WIN32}>:--for-linker=/OPT:REF,ICF=2> # Remove unneeded functions and data.
    $<$<BOOL:${WIN32}>:--for-linker=/GUARD:NO> # Disable 'Control Flow Guard' (CFG).
    )
elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")
  set(VOLO_COMPILER "msvc")
  add_compile_definitions(VOLO_MSVC)
  add_compile_options(
    /TC /std:c11 # Use the c11 standard.
    /utf-8 # Use utf8 for both the source and the executable format.
    /Zc:preprocessor # Enable the conformant c-preprocessor.
    /FS # Use synchronous pdb writes.

    $<$<BOOL:${VOLO_WERROR}>:/WX> # Warnings as errors.
    /W4 /wd4127 /wd5105 /wd4200 /wd4244 /wd4201 /wd4210 /wd4701 /wd4706 /wd4324 /wd4100 /wd4703
    /wd4152 /wd5286 /wd5287 /wd4189 /wd4245

    $<$<CONFIG:Debug>:/Od> # No optimizations in Debug.
    $<$<CONFIG:Release>:/O2> # Optimization level 2 in Release.
    /Zi # Debug symbols in separate pdb files.
    /Oi # Enable intrinsic functions.
    /Gv # Use the 'vectorcall' calling convention.
    /GS- # Disable 'Buffer Security Check'.
    /guard:cf- # Disable 'Control Flow Guard' (CFG).

    $<$<NOT:$<BOOL:${VOLO_SIMD}>>:/d2Qvec-> # No vectorization when SIMD is disabled.
    $<$<BOOL:${VOLO_LTO}>:/GL> # Link time optimization.
  )
  add_link_options(
    /machine:x64
    /ENTRY:wmainCRTStartup # Entry point with unicode support.
    /INCREMENTAL:NO # No incremental linking.
    /DEBUG # Generate a 'Program Database' file with debug symbols.
    /OPT:REF,ICF=2 # Remove functions and data that are never referenced.
    /GUARD:NO # Disable 'Control Flow Guard' (CFG).
    $<$<BOOL:${VOLO_WERROR}>:/WX> # Warnings as errors.
    $<$<BOOL:${VOLO_LTO}>:/LTCG> # Link time optimization.
  )
else()
  message(FATAL_ERROR "Unsupported compiler: '${CMAKE_C_COMPILER_ID}'")
endif()
