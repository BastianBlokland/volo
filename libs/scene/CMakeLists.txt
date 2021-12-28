# --------------------------------------------------------------------------------------------------
# Scene library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: scene")

add_library(lib_scene STATIC
  src/camera.c
  src/register.c
  src/time.c
  src/transform.c)
target_include_directories(lib_scene PUBLIC include)
target_link_libraries(lib_scene PUBLIC lib_core)
target_link_libraries(lib_scene PUBLIC lib_ecs)
target_link_libraries(lib_scene PUBLIC lib_geo)
