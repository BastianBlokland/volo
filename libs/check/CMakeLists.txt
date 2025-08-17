# --------------------------------------------------------------------------------------------------
# Check library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(check STATIC
  src/def.c
  src/output_log.c
  src/output_mocha.c
  src/output_pretty.c
  src/output.c
  src/result.c
  src/runner.c
  src/spec.c
  )
target_include_directories(check PUBLIC include)
target_link_libraries(check PUBLIC core)
target_link_libraries(check PRIVATE jobs json log trace)

add_executable(test_check
  test/config.c
  test/test_dynarray.c
  test/test_fizzbuzz.c
  )
target_link_libraries(test_check PRIVATE app_check check)

configure_test(test_check SHORT_NAME check)
configure_debuggable(test_check)
