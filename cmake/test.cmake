# --------------------------------------------------------------------------------------------------
# CMake test utilities.
# --------------------------------------------------------------------------------------------------

# Configure a 'test' target.
# Any test that is registered using 'configure_test()' will be added as a dependency of this target.
# This allows running all tests by invoking the 'test' target.
#
function(configure_test_target)
  message(STATUS "Configuring test target")
  add_custom_target(test)
endfunction(configure_test_target)

# Configure a 'test.[shortName]' target that will execute the given test executable when invoked.
#
function(configure_test target shortName)
  message(STATUS "Configuring ${shortName} test")

  set(testTargetName "test.${shortName}")
  add_custom_target(${testTargetName} COMMAND ${target} VERBATIM USES_TERMINAL)
  add_dependencies(${testTargetName} ${target})

  if(TARGET test)
    add_dependencies(test ${testTargetName})
  endif()
endfunction(configure_test)
