# --------------------------------------------------------------------------------------------------
# Root cmake file for the Volo project.
# --------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.19)

if(${CMAKE_VERSION} VERSION_GREATER "3.20")
  cmake_policy(SET CMP0116 NEW) # Support Ninja dep-files in subdirectories.
endif()

project(Volo VERSION 0.1.0 LANGUAGES C)

# Custom options.
option(FAST     "Fast mode, disables various runtime validations"   Off)
option(SIMD     "SIMD support, enables usage of SIMD instructions"  On)
option(TRACE    "Trace mode, enables runtime performance tracing"   On)
option(LTO      "Link time optimization"                            Off)
option(SANITIZE "Should sanitizer instrumentation be enabled"       Off)
option(LABEL    "Label to be associated with the build"             "")

# Diagnostic information.
message(STATUS "Configuring Volo")
message(STATUS "* Host system: ${CMAKE_HOST_SYSTEM}")
message(STATUS "* Host processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "* CMake version: ${CMAKE_VERSION}")
message(STATUS "* Project version: ${PROJECT_VERSION}")
message(STATUS "* Source path: ${PROJECT_SOURCE_DIR}")
message(STATUS "* Build path: ${PROJECT_BINARY_DIR}")
message(STATUS "* Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "* Generator: ${CMAKE_GENERATOR}")
message(STATUS "* Label: '${LABEL}'")
message(STATUS "* Fast: ${FAST}")
message(STATUS "* Simd: ${SIMD}")
message(STATUS "* Trace: ${TRACE}")
message(STATUS "* Lto: ${LTO}")
message(STATUS "* Sanitize: ${SANITIZE}")

include(cmake/compiler.cmake)
include(cmake/debug.cmake)
include(cmake/platform.cmake)
include(cmake/test.cmake)

# Setup global platform specific defines.
detect_platform()
set_platform_defines()

# Setup global compiler specific defines and options.
detect_compiler()
set_compiler_defines()
set_compile_options()

# Generate a 'compile_commands.json' for intellisense (for example clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(libs)
add_subdirectory(volo)
add_subdirectory(utilities)

configure_test_target()
configure_dbgsetup_target()

# --------------------------------------------------------------------------------------------------
# Install.
# --------------------------------------------------------------------------------------------------

set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}") # Install to project directory itself.

install(TARGETS volo fetch pack repl lsp blob2j DESTINATION bin)

if(${VOLO_PLATFORM} STREQUAL "win32" AND NOT ${VOLO_COMPILER} STREQUAL "gcc")
  install(FILES
    $<TARGET_PDB_FILE:volo>
    $<TARGET_PDB_FILE:fetch>
    $<TARGET_PDB_FILE:pack>
    $<TARGET_PDB_FILE:repl>
    $<TARGET_PDB_FILE:lsp>
    $<TARGET_PDB_FILE:blob2j>
    DESTINATION bin)
endif()
