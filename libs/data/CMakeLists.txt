# --------------------------------------------------------------------------------------------------
# Data library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: data")

add_library(volo_data STATIC
  src/read_json.c
  src/registry.c
  src/utils_clone.c
  src/utils_destroy.c
  src/write_json.c)
target_include_directories(volo_data PUBLIC include)
target_link_libraries(volo_data PUBLIC volo_core)
target_link_libraries(volo_data PRIVATE volo_json)

add_executable(volo_data_test
  test/main.c
  test/test_read_json.c
  test/test_registry.c
  test/test_utils_clone.c
  test/test_utils_destroy.c)
target_link_libraries(volo_data_test PRIVATE volo_check)
target_link_libraries(volo_data_test PRIVATE volo_data)
target_link_libraries(volo_data_test PRIVATE volo_jobs)
target_link_libraries(volo_data_test PRIVATE volo_log)

configure_test(volo_data_test data)
configure_debuggable(volo_data_test)
