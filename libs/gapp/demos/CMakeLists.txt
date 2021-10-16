# --------------------------------------------------------------------------------------------------
#  GuiApp demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: gapp_multi_windows")

add_executable(volo_gapp_multi_windows multi_windows.c)
target_link_libraries(volo_gapp_multi_windows PRIVATE volo_cli)
target_link_libraries(volo_gapp_multi_windows PRIVATE volo_gapp)

configure_debuggable(volo_gapp_multi_windows)

# Register a convenience target for launching the demo.
add_custom_target(demo.gapp.multi_windows COMMAND volo_gapp_multi_windows VERBATIM USES_TERMINAL)
