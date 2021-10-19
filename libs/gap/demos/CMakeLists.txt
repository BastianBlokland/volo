# --------------------------------------------------------------------------------------------------
#  GuiApp demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: gap_multi_window")

add_executable(volo_gap_multi_window multi_window.c)
target_link_libraries(volo_gap_multi_window PRIVATE volo_cli)
target_link_libraries(volo_gap_multi_window PRIVATE volo_gap)

configure_debuggable(volo_gap_multi_window)

# Register convenience targets for launching the demos.
add_custom_target(demo.gap.multi_window COMMAND volo_gap_multi_window VERBATIM USES_TERMINAL)
