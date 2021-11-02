# --------------------------------------------------------------------------------------------------
#  Renderer demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: rend_clear")

add_executable(volo_demo_rend_clear clear.c)
target_link_libraries(volo_demo_rend_clear PRIVATE volo_rend)
target_link_libraries(volo_demo_rend_clear PRIVATE volo_cli)
target_link_libraries(volo_demo_rend_clear PRIVATE volo_gap)
target_link_libraries(volo_demo_rend_clear PRIVATE volo_jobs)
target_link_libraries(volo_demo_rend_clear PRIVATE volo_log)

configure_debuggable(volo_demo_rend_clear)

# Register convenience targets for launching the demos.
add_custom_target(demo.rend.clear COMMAND volo_demo_rend_clear VERBATIM USES_TERMINAL)
