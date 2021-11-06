# --------------------------------------------------------------------------------------------------
# Asset library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: asset")

add_library(volo_asset STATIC
  src/manager.c
  src/register.c
  src/repo_fs.c
  src/repo_mem.c
  src/repo.c)
target_include_directories(volo_asset PUBLIC include)
target_link_libraries(volo_asset PUBLIC volo_core)
target_link_libraries(volo_asset PUBLIC volo_ecs)
target_link_libraries(volo_asset PRIVATE volo_gap)
target_link_libraries(volo_asset PRIVATE volo_log)
