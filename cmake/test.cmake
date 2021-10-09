# --------------------------------------------------------------------------------------------------
# CMake test utilities.
# --------------------------------------------------------------------------------------------------

#
# Configure a 'test' target.
# Any test that is registered using 'configure_test()' will be added as a dependency of this target.
# This allows running all tests by invoking the 'test' target.
#
function(configure_test_target)
  message(STATUS "> target: test")

  if(TARGET test)
    message(FATAL_ERROR "test target already configured")
  endif()
  add_custom_target(test)

  get_property(testTargets GLOBAL PROPERTY TEST_TARGETS)
  foreach(testTarget ${testTargets})
    add_dependencies(test ${testTarget})
  endforeach()

endfunction(configure_test_target)

#
# Configure a 'test.[shortName]' target that will execute the given test executable when invoked.
# Also adds it to the 'TEST_TARGETS' global property.
#
function(configure_test target shortName)
  message(STATUS "> test: ${target}")

  if(NOT TARGET ${target})
    message(FATAL_ERROR "Unknown target")
  endif()

  set(testTargetName "test.${shortName}")
  if(TARGET ${testTargetName})
    message(FATAL_ERROR "${testTargetName} target already configured")
  endif()
  add_custom_target(${testTargetName} COMMAND ${target} VERBATIM USES_TERMINAL)
  add_dependencies(${testTargetName} ${target})

  get_property(testTargets GLOBAL PROPERTY TEST_TARGETS)
  list(APPEND testTargets ${testTargetName})
  set_property(GLOBAL PROPERTY TEST_TARGETS ${testTargets})

endfunction(configure_test)
