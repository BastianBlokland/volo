# --------------------------------------------------------------------------------------------------
# Renderer library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: rend")

add_library(volo_rend STATIC
  src/canvas.c
  src/platform.c
  src/register.c)
target_include_directories(volo_rend PUBLIC include)
target_link_libraries(volo_rend PUBLIC volo_core)
target_link_libraries(volo_rend PUBLIC volo_ecs)
target_link_libraries(volo_rend PRIVATE volo_gap)
target_link_libraries(volo_rend PRIVATE volo_log)

message(STATUS "> demos: rend")
add_subdirectory(demos)
