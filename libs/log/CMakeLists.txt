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
target_link_libraries(lib_log PRIVATE lib_json)
target_link_libraries(lib_log PUBLIC lib_core)

add_executable(test_lib_log
  test/app.c
  test/test_logger.c
  test/test_sink_json.c
  test/test_sink_pretty.c
  )
target_link_libraries(test_lib_log PRIVATE lib_app_check lib_json lib_log)

configure_test(test_lib_log SHORT_NAME log)
configure_debuggable(test_lib_log)
