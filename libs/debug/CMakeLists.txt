# --------------------------------------------------------------------------------------------------
# Debug library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: debug")

add_library(lib_debug STATIC
  src/menu.c
  src/register.c
  )
target_include_directories(lib_debug PUBLIC include)
target_link_libraries(lib_debug PRIVATE lib_gap)
target_link_libraries(lib_debug PRIVATE lib_log)
target_link_libraries(lib_debug PRIVATE lib_ui)
target_link_libraries(lib_debug PUBLIC lib_core)
target_link_libraries(lib_debug PUBLIC lib_ecs)
target_link_libraries(lib_debug PUBLIC lib_scene)
