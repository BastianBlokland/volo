# --------------------------------------------------------------------------------------------------
#  Renderer demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: rend_clear")

add_executable(lib_rend_demo_clear clear.c)
target_link_libraries(lib_rend_demo_clear PRIVATE lib_rend)
target_link_libraries(lib_rend_demo_clear PRIVATE lib_asset)
target_link_libraries(lib_rend_demo_clear PRIVATE lib_cli)
target_link_libraries(lib_rend_demo_clear PRIVATE lib_gap)
target_link_libraries(lib_rend_demo_clear PRIVATE lib_jobs)
target_link_libraries(lib_rend_demo_clear PRIVATE lib_log)
target_link_libraries(lib_rend_demo_clear PRIVATE lib_scene)

add_dependencies(lib_rend_demo_clear volo_assets)

configure_debuggable(lib_rend_demo_clear)

# Register convenience targets for launching the demos.
add_custom_target(demo.rend.clear COMMAND lib_rend_demo_clear
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
