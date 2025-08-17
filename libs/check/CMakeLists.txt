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

add_executable(check_test
  test/config.c
  test/test_dynarray.c
  test/test_fizzbuzz.c
  )
target_link_libraries(check_test PRIVATE app_check check)
