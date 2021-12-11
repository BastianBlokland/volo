# --------------------------------------------------------------------------------------------------
#  Ecs demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: ecs_snake")

add_executable(lib_ecs_demo_snake snake.c)
target_link_libraries(lib_ecs_demo_snake PRIVATE lib_cli)
target_link_libraries(lib_ecs_demo_snake PRIVATE lib_ecs)
target_link_libraries(lib_ecs_demo_snake PRIVATE lib_jobs)
target_link_libraries(lib_ecs_demo_snake PRIVATE lib_log)

configure_debuggable(lib_ecs_demo_snake)

# Register a convenience target for launching the demo.
add_custom_target(demo.ecs.snake COMMAND lib_ecs_demo_snake VERBATIM USES_TERMINAL)
