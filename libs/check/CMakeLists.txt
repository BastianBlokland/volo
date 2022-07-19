# --------------------------------------------------------------------------------------------------
# Check library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: check")

add_library(lib_check STATIC
  src/def.c
  src/output_log.c
  src/output_mocha.c
  src/output_pretty.c
  src/output.c
  src/result.c
  src/runner.c
  src/spec.c
  )
target_include_directories(lib_check PUBLIC include)
target_link_libraries(lib_check PUBLIC lib_core)
target_link_libraries(lib_check PRIVATE lib_jobs lib_json lib_log)

add_executable(lib_check_test
  test/app.c
  test/test_dynarray.c
  test/test_fizzbuzz.c
  )
target_link_libraries(lib_check_test PRIVATE lib_app_check lib_check)

configure_test(lib_check_test SHORT_NAME check)
configure_debuggable(lib_check_test)
