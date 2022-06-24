# --------------------------------------------------------------------------------------------------
# Debug library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: debug")

add_library(lib_debug STATIC
  src/animation.c
  src/asset.c
  src/camera.c
  src/ecs.c
  src/grid.c
  src/interface.c
  src/log_viewer.c
  src/menu.c
  src/physics.c
  src/register.c
  src/rend.c
  src/shape.c
  src/stats.c
  src/text.c
  )
target_include_directories(lib_debug PUBLIC include)
target_link_libraries(lib_debug PRIVATE lib_gap)
target_link_libraries(lib_debug PRIVATE lib_log)
target_link_libraries(lib_debug PRIVATE lib_rend)
target_link_libraries(lib_debug PRIVATE lib_input)
target_link_libraries(lib_debug PRIVATE lib_ui)
target_link_libraries(lib_debug PUBLIC lib_core)
target_link_libraries(lib_debug PUBLIC lib_ecs)
target_link_libraries(lib_debug PUBLIC lib_scene)
