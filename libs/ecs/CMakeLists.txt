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
target_link_libraries(lib_ecs PUBLIC lib_core)
target_link_libraries(lib_ecs PRIVATE lib_jobs lib_log)

add_executable(test_lib_ecs
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
target_link_libraries(test_lib_ecs PRIVATE lib_app_check lib_ecs lib_jobs)

configure_test(test_lib_ecs SHORT_NAME ecs)
configure_debuggable(test_lib_ecs)
