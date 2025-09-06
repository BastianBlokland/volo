# --------------------------------------------------------------------------------------------------
# Root CMake file for the Volo project.
# --------------------------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.19)

if(${CMAKE_VERSION} VERSION_GREATER "3.20")
  cmake_policy(SET CMP0116 NEW) # Support Ninja dep-files in subdirectories.
endif()

project(Volo
  VERSION 0.4.0
  DESCRIPTION "Volo Game"
  HOMEPAGE_URL "https://github.com/bastianblokland/volo"
  LANGUAGES C
  )

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configuration types" FORCE)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Custom options.
option(VOLO_SIMD                  "SIMD support"                    On)
option(VOLO_TRACE                 "Runtime performance tracing"     On)
option(VOLO_LTO                   "Link time optimization"          Off)
option(VOLO_SANITIZE              "Sanitizer instrumentation"       Off)
option(VOLO_WERROR                "Warnings as errors"              Off)
set(   VOLO_LABEL "" CACHE STRING "Label to be associated with the build")

# Diagnostic information.
message(STATUS "Host system: ${CMAKE_HOST_SYSTEM}")
message(STATUS "Host processor: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMake version: ${CMAKE_VERSION}")
message(STATUS "Project version: ${PROJECT_VERSION}")
message(STATUS "Source path: ${PROJECT_SOURCE_DIR}")
message(STATUS "Build path: ${PROJECT_BINARY_DIR}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Version: ${CMAKE_PROJECT_VERSION}")
message(STATUS "Label: '${VOLO_LABEL}'")
message(STATUS "Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Simd: ${VOLO_SIMD}")
message(STATUS "Trace: ${VOLO_TRACE}")
message(STATUS "Lto: ${VOLO_LTO}")
message(STATUS "Sanitize: ${VOLO_SANITIZE}")

# --------------------------------------------------------------------------------------------------
# Global setup.
# --------------------------------------------------------------------------------------------------

include(cmake/compiler.cmake)

# --------------------------------------------------------------------------------------------------
# Setup targets.
# --------------------------------------------------------------------------------------------------

add_subdirectory(game)
add_subdirectory(utilities)
add_subdirectory(libs/app)
add_subdirectory(libs/asset)
add_subdirectory(libs/check)
add_subdirectory(libs/cli)
add_subdirectory(libs/core)
add_subdirectory(libs/data)
add_subdirectory(libs/dev)
add_subdirectory(libs/ecs)
add_subdirectory(libs/gap)
add_subdirectory(libs/geo)
add_subdirectory(libs/input)
add_subdirectory(libs/jobs)
add_subdirectory(libs/json)
add_subdirectory(libs/loc)
add_subdirectory(libs/log)
add_subdirectory(libs/net)
add_subdirectory(libs/rend)
add_subdirectory(libs/scene)
add_subdirectory(libs/script)
add_subdirectory(libs/snd)
add_subdirectory(libs/trace)
add_subdirectory(libs/ui)
add_subdirectory(libs/vfx)
add_subdirectory(libs/xml)

# --------------------------------------------------------------------------------------------------
# Testing.
# --------------------------------------------------------------------------------------------------

add_custom_target(test.asset  COMMAND asset_test VERBATIM USES_TERMINAL)
add_custom_target(test.check  COMMAND check_test VERBATIM USES_TERMINAL)
add_custom_target(test.cli    COMMAND cli_test VERBATIM USES_TERMINAL)
add_custom_target(test.core   COMMAND core_test VERBATIM USES_TERMINAL)
add_custom_target(test.data   COMMAND data_test VERBATIM USES_TERMINAL)
add_custom_target(test.ecs    COMMAND ecs_test VERBATIM USES_TERMINAL)
add_custom_target(test.geo    COMMAND geo_test VERBATIM USES_TERMINAL)
add_custom_target(test.jobs   COMMAND jobs_test VERBATIM USES_TERMINAL)
add_custom_target(test.json   COMMAND json_test VERBATIM USES_TERMINAL)
add_custom_target(test.log    COMMAND log_test VERBATIM USES_TERMINAL)
add_custom_target(test.net    COMMAND net_test VERBATIM USES_TERMINAL)
add_custom_target(test.scene  COMMAND scene_test VERBATIM USES_TERMINAL)
add_custom_target(test.script COMMAND script_test VERBATIM USES_TERMINAL)
add_custom_target(test.trace  COMMAND trace_test VERBATIM USES_TERMINAL)
add_custom_target(test.vfx    COMMAND vfx_test VERBATIM USES_TERMINAL)
add_custom_target(test.xml    COMMAND xml_test VERBATIM USES_TERMINAL)

add_custom_target(test DEPENDS
  test.asset
  test.check
  test.cli
  test.core
  test.data
  test.ecs
  test.geo
  test.jobs
  test.json
  test.log
  test.net
  test.scene
  test.script
  test.trace
  test.vfx
  test.xml
  )

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
  COMMAND volo "-wd" "--assets" "${CMAKE_SOURCE_DIR}/assets" VERBATIM USES_TERMINAL)

add_custom_target(run.schemagen
  COMMAND schemagen "${CMAKE_SOURCE_DIR}/assets/schemas/" VERBATIM USES_TERMINAL)

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

add_custom_target(run.idgen
  COMMAND idgen "${CMAKE_SOURCE_DIR}/libs/scene/id.json" VERBATIM USES_TERMINAL)

add_custom_target(run.dbggen
  COMMAND dbggen
    "--debugger" "$<IF:$<STREQUAL:${VOLO_COMPILER},msvc>,cppvsdbg,lldb>"
    "--workspace" "${PROJECT_SOURCE_DIR}"
    "--targets" "$<TARGET_FILE:volo>"
                "$<TARGET_FILE:schemagen>"
                "$<TARGET_FILE:fetch>"
                "$<TARGET_FILE:pack>"
                "$<TARGET_FILE:repl>"
                "$<TARGET_FILE:lsp>"
                "$<TARGET_FILE:bcu>"
                "$<TARGET_FILE:blob2j>"
                "$<TARGET_FILE:zipu>"
                "$<TARGET_FILE:httpu>"
                "$<TARGET_FILE:vkgen>"
                "$<TARGET_FILE:asset_test>"
                "$<TARGET_FILE:check_test>"
                "$<TARGET_FILE:cli_test>"
                "$<TARGET_FILE:core_test>"
                "$<TARGET_FILE:data_test>"
                "$<TARGET_FILE:ecs_test>"
                "$<TARGET_FILE:geo_test>"
                "$<TARGET_FILE:jobs_test>"
                "$<TARGET_FILE:json_test>"
                "$<TARGET_FILE:log_test>"
                "$<TARGET_FILE:net_test>"
                "$<TARGET_FILE:scene_test>"
                "$<TARGET_FILE:script_test>"
                "$<TARGET_FILE:trace_test>"
                "$<TARGET_FILE:vfx_test>"
                "$<TARGET_FILE:xml_test>"
    VERBATIM USES_TERMINAL)
