# --------------------------------------------------------------------------------------------------
# Scene library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: scene")

add_library(volo_scene STATIC
  src/camera.c
  src/graphic.c
  src/register.c
  src/transform.c)
target_include_directories(volo_scene PUBLIC include)
target_link_libraries(volo_scene PUBLIC volo_core)
target_link_libraries(volo_scene PUBLIC volo_ecs)
target_link_libraries(volo_scene PUBLIC volo_geo)
