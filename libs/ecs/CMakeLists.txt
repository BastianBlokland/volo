# --------------------------------------------------------------------------------------------------
# Ecs library cmake file.
# --------------------------------------------------------------------------------------------------

add_library(ecs STATIC
  src/archetype.c
  src/buffer.c
  src/comp.c
  src/def.c
  src/entity_allocator.c
  src/entity.c
  src/finalizer.c
  src/iterator.c
  src/module.c
  src/runner.c
  src/storage.c
  src/utils.c
  src/view.c
  src/world.c
  )
target_include_directories(ecs PUBLIC include)
target_link_libraries(ecs PUBLIC core jobs)
target_link_libraries(ecs PRIVATE log trace)

add_executable(ecs_test
  test/config.c
  test/test_affinity.c
  test/test_combinator.c
  test/test_def.c
  test/test_destruct.c
  test/test_entity.c
  test/test_graph.c
  test/test_runner.c
  test/test_storage.c
  test/test_utils.c
  test/test_view.c
  test/test_world.c
  )
target_link_libraries(ecs_test PRIVATE app_check ecs)
