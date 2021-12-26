# --------------------------------------------------------------------------------------------------
#  Renderer demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: rend_cube")

add_executable(lib_rend_demo_cube cube.c)
target_link_libraries(lib_rend_demo_cube PRIVATE lib_rend)
target_link_libraries(lib_rend_demo_cube PRIVATE lib_asset)
target_link_libraries(lib_rend_demo_cube PRIVATE lib_cli)
target_link_libraries(lib_rend_demo_cube PRIVATE lib_gap)
target_link_libraries(lib_rend_demo_cube PRIVATE lib_jobs)
target_link_libraries(lib_rend_demo_cube PRIVATE lib_log)
target_link_libraries(lib_rend_demo_cube PRIVATE lib_scene)

add_dependencies(lib_rend_demo_cube volo_assets)

configure_debuggable(lib_rend_demo_cube)

# Register convenience targets for launching the demos.
add_custom_target(demo.rend.cube COMMAND lib_rend_demo_cube
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
