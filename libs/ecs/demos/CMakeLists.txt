# --------------------------------------------------------------------------------------------------
#  Ecs demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: ecs_snake")

add_executable(volo_demo_ecs_snake snake.c)
target_link_libraries(volo_demo_ecs_snake PRIVATE volo_cli)
target_link_libraries(volo_demo_ecs_snake PRIVATE volo_ecs)
target_link_libraries(volo_demo_ecs_snake PRIVATE volo_jobs)
target_link_libraries(volo_demo_ecs_snake PRIVATE volo_log)

configure_debuggable(volo_demo_ecs_snake)

# Register a convenience target for launching the demo.
add_custom_target(demo.ecs.snake COMMAND volo_demo_ecs_snake VERBATIM USES_TERMINAL)
