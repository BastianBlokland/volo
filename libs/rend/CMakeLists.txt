# --------------------------------------------------------------------------------------------------
# Renderer library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: rend")

add_library(volo_rend STATIC
  src/dummy.c)
target_include_directories(volo_rend PUBLIC include)
target_link_libraries(volo_rend PUBLIC volo_core)
target_link_libraries(volo_rend PUBLIC volo_gap)
target_link_libraries(volo_rend PUBLIC volo_ecs)
target_link_libraries(volo_rend PUBLIC volo_log)
