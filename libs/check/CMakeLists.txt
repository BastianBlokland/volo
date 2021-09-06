# --------------------------------------------------------------------------------------------------
# Check library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring check library")

add_library(volo_check STATIC
  src/def.c
  src/output.c
  src/output_log.c
  src/output_pretty.c
  src/result.c
  src/runner.c
  src/spec.c)
target_include_directories(volo_check PUBLIC include)
target_link_libraries(volo_check PUBLIC volo_core)
target_link_libraries(volo_check PUBLIC volo_jobs)
target_link_libraries(volo_check PUBLIC volo_log)

add_executable(volo_check_test
  test/main.c
  test/test_dynarray.c
  test/test_fizzbuzz.c)
target_link_libraries(volo_check_test PRIVATE volo_check)
target_link_libraries(volo_check_test PRIVATE volo_cli)

configure_test(volo_check_test check)
