# --------------------------------------------------------------------------------------------------
# Log library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: log")

add_library(lib_log STATIC
  src/init.c
  src/logger.c
  src/sink_json.c
  src/sink_pretty.c
  )
target_include_directories(lib_log PUBLIC include)
target_link_libraries(lib_log PUBLIC lib_core)
target_link_libraries(lib_log PRIVATE lib_json)

add_executable(lib_log_test
  test/main.c
  test/test_logger.c
  test/test_sink_json.c
  test/test_sink_pretty.c
  )
target_link_libraries(lib_log_test PRIVATE lib_check)
target_link_libraries(lib_log_test PRIVATE lib_log)
target_link_libraries(lib_log_test PRIVATE lib_json)
target_link_libraries(lib_log_test PRIVATE lib_jobs)

configure_test(lib_log_test SHORT_NAME log)
configure_debuggable(lib_log_test)
