# --------------------------------------------------------------------------------------------------
# Root cmake file for the Volo project.
# --------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.15)

project(Volo VERSION 0.1.0 LANGUAGES C)

# Diagnostic information.
message(STATUS "Configuring Volo")
message(STATUS "* Host system: ${CMAKE_HOST_SYSTEM}")
message(STATUS "* Host processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "* CMake version: ${CMAKE_VERSION}")
message(STATUS "* Source path: ${PROJECT_SOURCE_DIR}")
message(STATUS "* Build path: ${PROJECT_BINARY_DIR}")
message(STATUS "* Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "* Generator: ${CMAKE_GENERATOR}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
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

message(STATUS "Configuring libraries")
add_subdirectory(libs)

message(STATUS "Configuring applications")
add_subdirectory(apps)

message(STATUS "Configuring utility targets")
configure_test_target()
configure_dbgsetup_target()
