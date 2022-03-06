# --------------------------------------------------------------------------------------------------
# Scene library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: scene")

add_library(lib_scene STATIC
  src/bounds.c
  src/camera.c
  src/grid.c
  src/lifetime.c
  src/register.c
  src/renderable.c
  src/tag.c
  src/text.c
  src/time.c
  src/transform.c
  src/velocity.c
  )
target_include_directories(lib_scene PUBLIC include)
target_link_libraries(lib_scene PRIVATE lib_gap)
target_link_libraries(lib_scene PRIVATE lib_log)
target_link_libraries(lib_scene PUBLIC lib_asset)
target_link_libraries(lib_scene PUBLIC lib_core)
target_link_libraries(lib_scene PUBLIC lib_ecs)
target_link_libraries(lib_scene PUBLIC lib_geo)
