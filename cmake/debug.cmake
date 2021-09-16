# --------------------------------------------------------------------------------------------------
# CMake debug utilities.
# --------------------------------------------------------------------------------------------------

#
# Configure a 'dbgsetup' target that generates debugger configuration files (eg. VsCode launch.json)
# for all debuggable targets (targets that are added to the 'DEBUGGABLE_TARGETS' global property).
#
# Note: Call this after configuring all debuggable targets with 'configure_debuggable()'.
#
function(configure_dbgsetup_target)
  message(STATUS "Configuring dbgsetup target")

  if(TARGET dbgsetup)
    message(FATAL_ERROR "dbgsetup target already configured")
  endif()

  get_property(debuggableTargets GLOBAL PROPERTY DEBUGGABLE_TARGETS)
  if(NOT debuggableTargets)
    message(FATAL_ERROR "No debuggable targets registered")
  endif()

  foreach(debuggableTarget ${debuggableTargets})
    set(targets "${targets},$<TARGET_FILE:${debuggableTarget}>")
  endforeach()

  if(${VOLO_COMPILER} STREQUAL "gcc")
    set(debugger "lldb") # TODO: Add a debugger configuration for gdb.
  elseif(${VOLO_COMPILER} STREQUAL "clang")
    set(debugger "lldb")
  elseif(${VOLO_COMPILER} STREQUAL "msvc")
    set(debugger "cppvsdbg")
  else()
    message(FATAL_ERROR "Unknown compiler")
  endif()

  add_custom_target(dbgsetup COMMAND volo_dbgsetup
    "--debugger" "${debugger}"
    "--workspace" "${PROJECT_SOURCE_DIR}"
    "--targets" "$<GENEX_EVAL:${targets}>" VERBATIM USES_TERMINAL)
endfunction(configure_dbgsetup_target)

#
# Mark a target as debuggable (adds it to the 'DEBUGGABLE_TARGETS' global property).
#
function(configure_debuggable target)
  message(STATUS "Configuring debuggable: ${target}")

  if(NOT TARGET ${target})
    message(FATAL_ERROR "Unknown target")
  endif()

  get_property(debuggableTargets GLOBAL PROPERTY DEBUGGABLE_TARGETS)
  list(APPEND debuggableTargets ${target})
  set_property(GLOBAL PROPERTY DEBUGGABLE_TARGETS ${debuggableTargets})

endfunction(configure_debuggable)
