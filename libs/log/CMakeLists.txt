# --------------------------------------------------------------------------------------------------
# Log library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(log STATIC
  src/init.c
  src/logger.c
  src/sink_json.c
  src/sink_pretty.c
  )
target_include_directories(log PUBLIC include)
target_link_libraries(log PRIVATE json)
target_link_libraries(log PUBLIC core)

add_executable(test_log
  test/config.c
  test/test_logger.c
  test/test_sink_json.c
  test/test_sink_pretty.c
  )
target_link_libraries(test_log PRIVATE app_check json log)

configure_test(test_log SHORT_NAME log)
configure_debuggable(test_log)
