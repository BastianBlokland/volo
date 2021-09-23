# --------------------------------------------------------------------------------------------------
# EntityComponentSystem library cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS "Configuring ecs library")

add_library(volo_ecs STATIC
  src/archetype.c
  src/def.c
  src/entity_allocator.c
  src/module.c
  src/storage.c
  src/world.c)
target_include_directories(volo_ecs PUBLIC include)
target_link_libraries(volo_ecs PUBLIC volo_core)
target_link_libraries(volo_ecs PUBLIC volo_jobs)

add_executable(volo_ecs_test
  test/main.c
  test/test_def.c
  test/test_storage.c
  test/test_world.c)
target_link_libraries(volo_ecs_test PRIVATE volo_check)
target_link_libraries(volo_ecs_test PRIVATE volo_ecs)
target_link_libraries(volo_ecs_test PRIVATE volo_log)

configure_test(volo_ecs_test ecs)
configure_debuggable(volo_ecs_test)
