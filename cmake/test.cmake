# --------------------------------------------------------------------------------------------------
# CMake test utilities.
# --------------------------------------------------------------------------------------------------

#
# Configure a 'test' target.
# Any test that is registered using 'configure_test()' will be added as a dependency of this target.
# This allows running all tests by invoking the 'test' target.
#
function(configure_test_target)
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
# Configure a 'test.[SHORT_NAME]' target that will execute the given test executable when invoked.
# Also adds it to the 'TEST_TARGETS' global property.
#
function(configure_test target)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "SHORT_NAME" "")

  if(NOT TARGET ${target})
    message(FATAL_ERROR "Unknown target")
  endif()

  set(testTargetName "test.${ARG_SHORT_NAME}")
  if(TARGET ${testTargetName})
    message(FATAL_ERROR "${testTargetName} target already configured")
  endif()
  add_custom_target(${testTargetName} COMMAND ${target} VERBATIM USES_TERMINAL)
  add_dependencies(${testTargetName} ${target})

  get_property(testTargets GLOBAL PROPERTY TEST_TARGETS)
  list(APPEND testTargets ${testTargetName})
  set_property(GLOBAL PROPERTY TEST_TARGETS ${testTargets})

endfunction(configure_test)
