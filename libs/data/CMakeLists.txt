# --------------------------------------------------------------------------------------------------
# Data library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(data STATIC
  src/init.c
  src/jsonschema.c
  src/read_bin.c
  src/read_json.c
  src/registry.c
  src/utils_clone.c
  src/utils_destroy.c
  src/utils_equal.c
  src/utils_hash.c
  src/utils_visit.c
  src/write_bin.c
  src/write_json.c
  )
target_include_directories(data PUBLIC include)
target_link_libraries(data PRIVATE json)
target_link_libraries(data PUBLIC core)

add_executable(data_test
  test/config.c
  test/test_bin.c
  test/test_jsonschema.c
  test/test_read_json.c
  test/test_registry.c
  test/test_utils_clone.c
  test/test_utils_destroy.c
  test/test_utils_equal.c
  test/test_utils_hash.c
  test/test_utils_visit.c
  test/test_write_json.c
  )
target_link_libraries(data_test PRIVATE app_check data)
