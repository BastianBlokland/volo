# --------------------------------------------------------------------------------------------------
# Check library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: check")

add_library(lib_check STATIC
  src/app.c
  src/def.c
  src/output.c
  src/output_log.c
  src/output_mocha.c
  src/output_pretty.c
  src/result.c
  src/runner.c
  src/spec.c)
target_include_directories(lib_check PUBLIC include)
target_link_libraries(lib_check PUBLIC lib_core)
target_link_libraries(lib_check PRIVATE lib_cli)
target_link_libraries(lib_check PRIVATE lib_jobs)
target_link_libraries(lib_check PRIVATE lib_json)
target_link_libraries(lib_check PRIVATE lib_log)

add_executable(lib_check_test
  test/main.c
  test/test_dynarray.c
  test/test_fizzbuzz.c)
target_link_libraries(lib_check_test PRIVATE lib_check)
target_link_libraries(lib_check_test PRIVATE lib_jobs)
target_link_libraries(lib_check_test PRIVATE lib_log)

configure_test(lib_check_test check)
configure_debuggable(lib_check_test)
