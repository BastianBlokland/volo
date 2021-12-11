# --------------------------------------------------------------------------------------------------
#  Gui Application Protocol demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: gap_multi_window")

add_executable(lib_gap_demo_multi_window multi_window.c)
target_link_libraries(lib_gap_demo_multi_window PRIVATE lib_cli)
target_link_libraries(lib_gap_demo_multi_window PRIVATE lib_gap)
target_link_libraries(lib_gap_demo_multi_window PRIVATE lib_jobs)
target_link_libraries(lib_gap_demo_multi_window PRIVATE lib_log)

configure_debuggable(lib_gap_demo_multi_window)

# Register convenience targets for launching the demos.
add_custom_target(demo.gap.multi_window COMMAND lib_gap_demo_multi_window VERBATIM USES_TERMINAL)
