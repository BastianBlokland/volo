# --------------------------------------------------------------------------------------------------
# Data library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: data")

add_library(lib_data STATIC
  src/read_json.c
  src/registry.c
  src/utils_clone.c
  src/utils_destroy.c
  src/write_json.c
  )
target_include_directories(lib_data PUBLIC include)
target_link_libraries(lib_data PRIVATE lib_json)
target_link_libraries(lib_data PUBLIC lib_core)

add_executable(lib_data_test
  test/main.c
  test/test_read_json.c
  test/test_registry.c
  test/test_utils_clone.c
  test/test_utils_destroy.c
  test/test_write_json.c
  )
target_link_libraries(lib_data_test PRIVATE lib_check)
target_link_libraries(lib_data_test PRIVATE lib_data)
target_link_libraries(lib_data_test PRIVATE lib_jobs)
target_link_libraries(lib_data_test PRIVATE lib_log)

configure_test(lib_data_test SHORT_NAME data)
configure_debuggable(lib_data_test)
