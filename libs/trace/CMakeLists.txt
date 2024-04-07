# --------------------------------------------------------------------------------------------------
# Trace library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: trace")

add_library(lib_trace STATIC
  src/event.c
  src/init.c
  )
target_include_directories(lib_trace PUBLIC include)
target_link_libraries(lib_trace PUBLIC lib_core)
