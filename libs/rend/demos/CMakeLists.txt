# --------------------------------------------------------------------------------------------------
#  Renderer demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: rend_cube")

add_executable(lib_rend_demo_pedestal pedestal.c)
target_link_libraries(lib_rend_demo_pedestal PRIVATE lib_rend)
target_link_libraries(lib_rend_demo_pedestal PRIVATE lib_asset)
target_link_libraries(lib_rend_demo_pedestal PRIVATE lib_cli)
target_link_libraries(lib_rend_demo_pedestal PRIVATE lib_gap)
target_link_libraries(lib_rend_demo_pedestal PRIVATE lib_jobs)
target_link_libraries(lib_rend_demo_pedestal PRIVATE lib_log)
target_link_libraries(lib_rend_demo_pedestal PRIVATE lib_scene)

add_dependencies(lib_rend_demo_pedestal volo_assets)

configure_debuggable(lib_rend_demo_pedestal)

# Register convenience targets for launching the demos.
add_custom_target(demo.rend.cube COMMAND lib_rend_demo_pedestal
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
