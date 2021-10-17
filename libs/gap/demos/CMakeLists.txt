# --------------------------------------------------------------------------------------------------
#  GuiApp demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: gap_windows")

add_executable(volo_gap_windows windows.c)
target_link_libraries(volo_gap_windows PRIVATE volo_cli)
target_link_libraries(volo_gap_windows PRIVATE volo_gap)

configure_debuggable(volo_gap_windows)

# Register a convenience target for launching the demo.
add_custom_target(demo.gap.windows COMMAND volo_gap_windows VERBATIM USES_TERMINAL)
