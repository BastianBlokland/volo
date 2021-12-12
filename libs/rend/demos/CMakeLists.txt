# --------------------------------------------------------------------------------------------------
#  Renderer demos cmake file.
# --------------------------------------------------------------------------------------------------

message(STATUS ">> demo: rend_triangle")

add_executable(lib_rend_demo_triangle triangle.c)
target_link_libraries(lib_rend_demo_triangle PRIVATE lib_rend)
target_link_libraries(lib_rend_demo_triangle PRIVATE lib_asset)
target_link_libraries(lib_rend_demo_triangle PRIVATE lib_cli)
target_link_libraries(lib_rend_demo_triangle PRIVATE lib_gap)
target_link_libraries(lib_rend_demo_triangle PRIVATE lib_jobs)
target_link_libraries(lib_rend_demo_triangle PRIVATE lib_log)
target_link_libraries(lib_rend_demo_triangle PRIVATE lib_scene)

add_dependencies(lib_rend_demo_triangle volo_assets)

configure_debuggable(lib_rend_demo_triangle)

# Register convenience targets for launching the demos.
add_custom_target(demo.rend.triangle COMMAND lib_rend_demo_triangle
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
