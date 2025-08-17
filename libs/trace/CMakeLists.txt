# --------------------------------------------------------------------------------------------------
# Trace library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(trace STATIC
  src/dump_eventtrace.c
  src/init.c
  src/sink_store.c
  src/sink_superluminal.c
  src/tracer.c
  )
target_include_directories(trace PUBLIC include)
target_link_libraries(trace PUBLIC core)
target_link_libraries(trace PRIVATE log)

add_executable(trace_test
  test/config.c
  test/test_dump_eventtrace.c
  test/test_sink_store.c
  test/test_tracer.c
  )
target_link_libraries(trace_test PRIVATE app_check trace json)
