# --------------------------------------------------------------------------------------------------
# Ecs library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: ecs")

add_library(volo_ecs STATIC
  src/archetype.c
  src/buffer.c
  src/comp.c
  src/def.c
  src/entity_allocator.c
  src/entity.c
  src/iterator.c
  src/module.c
  src/runner.c
  src/storage.c
  src/utils.c
  src/view.c
  src/world.c)
target_include_directories(volo_ecs PUBLIC include)
target_link_libraries(volo_ecs PUBLIC volo_core)
target_link_libraries(volo_ecs PRIVATE volo_jobs)
target_link_libraries(volo_ecs PRIVATE volo_log)

add_executable(volo_ecs_test
  test/main.c
  test/test_affinity.c
  test/test_def.c
  test/test_destruct.c
  test/test_entity.c
  test/test_graph.c
  test/test_runner.c
  test/test_storage.c
  test/test_utils.c
  test/test_view.c
  test/test_world.c)
target_link_libraries(volo_ecs_test PRIVATE volo_check)
target_link_libraries(volo_ecs_test PRIVATE volo_ecs)
target_link_libraries(volo_ecs_test PRIVATE volo_jobs)
target_link_libraries(volo_ecs_test PRIVATE volo_log)

configure_test(volo_ecs_test ecs)
configure_debuggable(volo_ecs_test)

message(STATUS "> demos: ecs")
add_subdirectory(demos)
