# --------------------------------------------------------------------------------------------------
# Log library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: log")

add_library(volo_log STATIC
  src/init.c
  src/logger.c
  src/sink_json.c
  src/sink_pretty.c)
target_include_directories(volo_log PUBLIC include)
target_link_libraries(volo_log PUBLIC volo_core)
target_link_libraries(volo_log PUBLIC volo_json)

add_executable(volo_log_test
  test/main.c
  test/test_logger.c
  test/test_sink_json.c
  test/test_sink_pretty.c)
target_link_libraries(volo_log_test PRIVATE volo_check)
target_link_libraries(volo_log_test PRIVATE volo_log)

configure_test(volo_log_test log)
configure_debuggable(volo_log_test)
