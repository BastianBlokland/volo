# --------------------------------------------------------------------------------------------------
# Applications cmake file.
# --------------------------------------------------------------------------------------------------

#
# Pedestal app.
#
add_executable(app_pedestal pedestal.c)
target_link_libraries(app_pedestal PRIVATE lib_asset)
target_link_libraries(app_pedestal PRIVATE lib_cli)
target_link_libraries(app_pedestal PRIVATE lib_gap)
target_link_libraries(app_pedestal PRIVATE lib_jobs)
target_link_libraries(app_pedestal PRIVATE lib_log)
target_link_libraries(app_pedestal PRIVATE lib_rend)
target_link_libraries(app_pedestal PRIVATE lib_scene)
add_dependencies(app_pedestal volo_assets)
configure_debuggable(app_pedestal)

#
# Convienience run targets.
#
add_custom_target(run.pedestal COMMAND app_pedestal
  "--assets" "$<TARGET_PROPERTY:volo_assets,path>" VERBATIM USES_TERMINAL)
