# --------------------------------------------------------------------------------------------------
# Input library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: input")

add_library(lib_input STATIC
  src/register.c
  )
target_include_directories(lib_input PUBLIC include)
target_link_libraries(lib_input PUBLIC lib_core)
target_link_libraries(lib_input PUBLIC lib_ecs)
