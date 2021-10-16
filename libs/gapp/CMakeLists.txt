# --------------------------------------------------------------------------------------------------
# GuiApp library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: gapp")

add_library(volo_gapp STATIC
  src/register.c
  src/window.c)
target_include_directories(volo_gapp PUBLIC include)
target_link_libraries(volo_gapp PUBLIC volo_core)
target_link_libraries(volo_gapp PUBLIC volo_ecs)
target_link_libraries(volo_gapp PUBLIC volo_log)

message(STATUS "> demos: gapp")
add_subdirectory(demos)
