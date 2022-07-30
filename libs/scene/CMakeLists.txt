# --------------------------------------------------------------------------------------------------
# Scene library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: scene")

add_library(lib_scene STATIC
  src/bounds.c
  src/camera.c
  src/collision.c
  src/lifetime.c
  src/locomotion.c
  src/name.c
  src/register.c
  src/renderable.c
  src/selection.c
  src/skeleton.c
  src/tag.c
  src/time.c
  src/transform.c
  )
target_include_directories(lib_scene PUBLIC include)
target_link_libraries(lib_scene PUBLIC lib_asset lib_core lib_ecs lib_geo)
target_link_libraries(lib_scene PRIVATE lib_log)
