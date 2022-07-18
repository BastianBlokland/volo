# --------------------------------------------------------------------------------------------------
# Ecs library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "> library: ecs")

add_library(lib_ecs STATIC
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
target_include_directories(lib_ecs PUBLIC include)
target_link_libraries(lib_ecs PRIVATE lib_jobs)
target_link_libraries(lib_ecs PRIVATE lib_log)
target_link_libraries(lib_ecs PUBLIC lib_core)

add_executable(lib_ecs_test
  test/app.c
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
target_link_libraries(lib_ecs_test PRIVATE lib_app_check)
target_link_libraries(lib_ecs_test PRIVATE lib_ecs)
target_link_libraries(lib_ecs_test PRIVATE lib_jobs)

configure_test(lib_ecs_test SHORT_NAME ecs)
configure_debuggable(lib_ecs_test)
