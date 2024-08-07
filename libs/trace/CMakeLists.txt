# --------------------------------------------------------------------------------------------------
# Trace library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: trace")

add_library(lib_trace STATIC
  src/dump_eventtrace.c
  src/init.c
  src/sink_store.c
  src/sink_superluminal.c
  src/tracer.c
  )
target_include_directories(lib_trace PUBLIC include)
target_link_libraries(lib_trace PUBLIC lib_core)
target_link_libraries(lib_trace PRIVATE lib_log)

add_executable(test_lib_trace
  test/config.c
  test/test_dump_eventtrace.c
  test/test_sink_store.c
  test/test_tracer.c
  )
target_link_libraries(test_lib_trace PRIVATE lib_app_check lib_trace lib_json)

configure_test(test_lib_trace SHORT_NAME trace)
configure_debuggable(test_lib_trace)
