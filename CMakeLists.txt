# --------------------------------------------------------------------------------------------------
# Root cmake file for the Volo project.
# --------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.19)

if(${CMAKE_VERSION} VERSION_GREATER "3.20")
  cmake_policy(SET CMP0116 NEW) # Support Ninja dep-files in subdirectories.
endif()

project(Volo VERSION 0.1.0 LANGUAGES C)

# Custom options.
option(VOLO_FAST     "Fast mode, disables various runtime validations"   Off)
option(VOLO_SIMD     "SIMD support, enables usage of SIMD instructions"  On)
option(VOLO_TRACE    "Trace mode, enables runtime performance tracing"   On)
option(VOLO_LTO      "Link time optimization"                            Off)
option(VOLO_SANITIZE "Should sanitizer instrumentation be enabled"       Off)
option(VOLO_LABEL    "Label to be associated with the build"             "")

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
message(STATUS "* Label: '${VOLO_LABEL}'")
message(STATUS "* Fast: ${VOLO_FAST}")
message(STATUS "* Simd: ${VOLO_SIMD}")
message(STATUS "* Trace: ${VOLO_TRACE}")
message(STATUS "* Lto: ${VOLO_LTO}")
message(STATUS "* Sanitize: ${VOLO_SANITIZE}")

# --------------------------------------------------------------------------------------------------
# Global setup.
# --------------------------------------------------------------------------------------------------

include(cmake/compiler.cmake)
include(cmake/debug.cmake)
include(cmake/test.cmake)

# --------------------------------------------------------------------------------------------------
# Register targets.
# --------------------------------------------------------------------------------------------------

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

# --------------------------------------------------------------------------------------------------
# Helpers.
# --------------------------------------------------------------------------------------------------

add_custom_target(run.volo
  COMMAND fetch "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL
  COMMAND volo "-w" "--assets" "${CMAKE_SOURCE_DIR}/assets" VERBATIM USES_TERMINAL)

add_custom_target(run.schemasetup
  COMMAND schemasetup "${CMAKE_SOURCE_DIR}/assets/schemas/" VERBATIM USES_TERMINAL)

add_custom_target(run.fetch
  COMMAND fetch "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL)

add_custom_target(run.fetch.force
  COMMAND fetch --force "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL)

add_custom_target(run.pack
  COMMAND fetch "${CMAKE_SOURCE_DIR}/assets/fetch.json" VERBATIM USES_TERMINAL
  COMMAND pack
    "--assets" "${CMAKE_SOURCE_DIR}/assets"
    "--output" "${CMAKE_BINARY_DIR}/assets.blob"
    "${CMAKE_SOURCE_DIR}/assets/pack.json"
    VERBATIM USES_TERMINAL)

add_custom_target(run.repl
  COMMAND repl VERBATIM USES_TERMINAL)

add_custom_target(run.vkgen
  COMMAND vkgen "${CMAKE_SOURCE_DIR}/libs/rend/src/rvk/vulkan_api" VERBATIM USES_TERMINAL)
