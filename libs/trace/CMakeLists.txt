# --------------------------------------------------------------------------------------------------
# Trace library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: trace")

add_library(lib_trace STATIC
  src/event.c
  src/init.c
  src/sink_buffer.c
  src/sink_superluminal.c
  )
target_include_directories(lib_trace PUBLIC include)
target_link_libraries(lib_trace PUBLIC lib_core)
target_link_libraries(lib_trace PRIVATE lib_log)
