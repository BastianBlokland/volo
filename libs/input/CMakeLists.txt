# --------------------------------------------------------------------------------------------------
# Input library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: input")

add_library(lib_input STATIC
  src/manager.c
  src/register.c
  src/resource.c
  )
target_include_directories(lib_input PUBLIC include)
target_link_libraries(lib_input PUBLIC lib_core lib_ecs)
target_link_libraries(lib_input PRIVATE lib_asset lib_log lib_gap)
