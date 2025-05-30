# --------------------------------------------------------------------------------------------------
# Data library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: data")

add_library(lib_data STATIC
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
target_include_directories(lib_data PUBLIC include)
target_link_libraries(lib_data PRIVATE lib_json)
target_link_libraries(lib_data PUBLIC lib_core)

add_executable(test_lib_data
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
target_link_libraries(test_lib_data PRIVATE lib_app_check lib_data)

configure_test(test_lib_data SHORT_NAME data)
configure_debuggable(test_lib_data)
